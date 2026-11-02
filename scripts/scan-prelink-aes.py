#!/usr/bin/env python3
"""Measure the original IOAESAccelerator bindings out of this OS's kernelcache.

SoftwareAES subclasses IOAESAccelerator, which ships in the prelinked
com.apple.iokit.IOCryptoAcceleratorFamily kext. Its inherited methods resolve to
that original code through absolute symbols. UniXNU records those symbols for the
iPadOS 17.7.11 kernelcache, and on any other kernelcache they point at unrelated
or zeroed memory: on iOS 16.5 the constructor call took an undefined instruction
exception.

This writes scripts/data/native-aes-original-bindings.S with the same symbol set,
measured from the kernelcache and moved by the profile's prelink slide. Slot names
come from the compiled NativeAES.o, whose vtable is the class being bound.

    scripts/scan-prelink-aes.py --kernelcache kernelcache.decompressed \\
        --object out/.../tree/build/software-aes/NativeAES.o --tree out/.../tree
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
DEVICE = HERE.parents[1]
KEXT = 'com.apple.iokit.IOCryptoAcceleratorFamily'
CLASS = 'IOAESAccelerator'
MANGLED = '16' + CLASS
LOCAL = '9NativeAES'
VTABLE = '__ZTV' + LOCAL
INSTANCE_SIZE = 0x120  # static_assert in SoftwareAES/OriginalAESABI.h
CONSTRUCTOR = f'__ZN{MANGLED}C2EPK11OSMetaClass'
BASE_DESTRUCTOR = f'__ZN{MANGLED}D2Ev'
METACLASS = f'__ZN{MANGLED}10gMetaClassE'


def fail(message: str) -> None:
    raise SystemExit('scan-prelink-aes: ' + message)


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def vtable_names(np, path: Path) -> list[str]:
    obj = path.read_bytes()
    image = np.macho(obj)
    symtab = next(at for command, at, _ in image['commands'] if command == 2)
    symoff, count, stroff = struct.unpack_from('<4I', obj, symtab + 8)[:3]
    symbols = []
    for index in range(count):
        name_at, _, _, _, value = struct.unpack_from('<IBBHQ', obj, symoff + index * 16)
        symbols.append((obj[stroff + name_at:obj.index(b'\0', stroff + name_at)].decode(), value))
    vtable = next((value for name, value in symbols if name == VTABLE), None)
    if vtable is None:
        fail(f'{path} does not define {VTABLE}')
    relocations = {}
    for segment in image['segments']:
        for section in segment['sections']:
            if section['va'] <= vtable < section['va'] + section['size']:
                reloff, relcount = struct.unpack_from('<II', obj, section['command_offset'] + 56)
                for index in range(relcount):
                    address, word = struct.unpack_from('<II', obj, reloff + index * 8)
                    if word >> 27 & 1 and word >> 28 == 0 and word >> 25 & 3 == 3:
                        relocations[section['va'] + address] = symbols[word & 0xFFFFFF][0]
    names = []
    while vtable + 16 + len(names) * 8 in relocations:
        names.append(relocations[vtable + 16 + len(names) * 8])
    if len(names) < 100 or not names[0].endswith('D1Ev'):
        fail('the compiled vtable does not look like an IOService subclass')
    return names


def page_target(address: int, adrp: int, add: int) -> int:
    immediate = ((adrp >> 5 & 0x7ffff) << 2) | (adrp >> 29 & 3)
    if immediate & 0x100000:
        immediate -= 0x200000
    return (address & ~4095) + immediate * 4096 + (add >> 10 & 4095)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--kernelcache', type=Path, required=True)
    parser.add_argument('--object', type=Path, required=True, help='the compiled NativeAES.o')
    parser.add_argument('--tree', type=Path, required=True, help='the assembled UniXNU build tree')
    parser.add_argument('--recorded', type=Path, help='the symbol set to reproduce, by default the tree copy')
    parser.add_argument('--collection-slide', default='-0x1000000')
    parser.add_argument('--output', type=Path, default=DEVICE / 'scripts/data/native-aes-original-bindings.S')
    args = parser.parse_args()

    np = load_module('native_prelink', args.tree / 'kits/NativePrelink/native_prelink.py')
    pins = load_module('pins', DEVICE / 'scripts/extract-ipsw-pins.py')
    fat = args.kernelcache.read_bytes()
    parent = np.macho(fat)
    entries = {entry.get('CFBundleIdentifier'): entry for entry in pins.prelink_info(fat)}
    if KEXT not in entries:
        fail(f'{KEXT} is not in this kernelcache')
    base = int(entries[KEXT]['_PrelinkExecutableLoadAddr'], 0)

    def read(address: int, length: int) -> bytes:
        offset = np.backed_offset(parent['segments'], address, length, len(fat))
        return fat[offset:offset + length]

    kext = np.macho(fat, np.backed_offset(parent['segments'], base, 64, len(fat)))
    segments = {segment['name']: segment for segment in kext['segments'] if segment['size']}
    text, const, cstring = segments['__TEXT_EXEC'], segments['__DATA_CONST'], segments.get('__TEXT')
    data = segments['__DATA']

    names = vtable_names(np, args.object)
    slots = len(names)
    export_at = {}
    for address, exported in np.exports(fat, parent).items():
        for name in exported:
            export_at.setdefault(name, address)
    checks = [index for index, name in enumerate(names) if name in export_at]
    if len(checks) < slots // 4:
        fail(f'only {len(checks)} of {slots} slot names are kernel exports')

    blob = read(const['va'], const['size'])
    found = []
    for at in range(0, len(blob) - (slots + 2) * 8 + 1, 8):
        if blob[at:at + 16] != bytes(16):
            continue
        pointers = struct.unpack_from('<' + 'Q' * slots, blob, at + 16)
        if all(pointers[index] == export_at[names[index]] for index in checks):
            own = [index for index, value in enumerate(pointers)
                   if text['va'] <= value < text['va'] + text['size'] and index >= 2]
            found.append((const['va'] + at, pointers, own))
    print(f'candidate tables: {[(hex(t), len(o)) for t, _, o in found]}')
    # The parent IOCryptoAccelerator table overrides nothing past the IOService slots.
    tail = [candidate for candidate in found
            if all(text['va'] <= candidate[1][index] < text['va'] + text['size']
                   for index in range(slots - 13, slots))]
    if len(tail) != 1:
        fail(f'found {len(tail)} {CLASS} dispatch tables in {KEXT}; expected exactly 1')
    table, pointers, _ = tail[0]

    getter_slot = next(index for index, name in enumerate(names) if 'getMetaClass' in name)
    adrp, add, ret = struct.unpack('<3I', read(pointers[getter_slot], 12))
    if not (adrp & 0x9f00001f == 0x90000000 and add & 0xffc003ff == 0x91000000 and ret == 0xd65f03c0):
        fail('the metaclass slot is not a metaclass getter')
    metaclass = page_target(pointers[getter_slot], adrp, add)
    if not data['va'] <= metaclass < data['va'] + data['size']:
        fail(f'the metaclass {metaclass:#x} is outside the kext __DATA')

    # The static initialiser names the class and states its instance size next to
    # the metaclass. Both confirm this table is the class, not a relative of it.
    code = read(text['va'], text['size'])
    words = struct.unpack('<' + 'I' * (len(code) // 4), code)
    confirmed = []
    for index in range(len(words) - 8):
        if words[index] & 0x9f000000 != 0x90000000 or (words[index + 1] >> 22) != 0x244:
            continue
        address = text['va'] + index * 4
        if page_target(address, words[index], words[index + 1]) != metaclass or words[index] & 31 != 0:
            continue
        name, size = None, None
        for near in range(max(0, index - 10), min(len(words) - 1, index + 11)):
            word = words[near]
            if word & 0x9f00001f == 0x90000001 and (words[near + 1] >> 22) == 0x244:
                target = page_target(text['va'] + near * 4, word, words[near + 1])
                try:
                    name = read(target, 32).split(b'\0')[0]
                except Exception:
                    pass
            if word & 0xffe0001f == 0x52800003:
                size = word >> 5 & 0xffff
        if name == CLASS.encode():
            confirmed.append(size)
    print(f'static initialiser sizes for {CLASS}: {[hex(s) if s else s for s in confirmed]}')
    if not confirmed or set(confirmed) != {INSTANCE_SIZE}:
        fail(f'{CLASS} is not confirmed at instance size {INSTANCE_SIZE:#x}: {confirmed}')

    # Two identical protected constructors store the address point into the object.
    address_point = table + 16
    constructors = []
    for index in range(len(words) - 8):
        form = words[index:index + 8]
        if (form[:2] != (0xa9bf7bfd, 0x910003fd) or form[2] & 0xfc000000 != 0x94000000 or
                form[3] & 0x9f00001f != 0x90000008 or form[4] & 0xffc003ff != 0x91000108 or
                form[5:] != (0xf9000008, 0xa8c17bfd, 0xd65f03c0)):
            continue
        if page_target(text['va'] + (index + 3) * 4, form[3], form[4]) == address_point:
            constructors.append(text['va'] + index * 4)
    print(f'protected constructors: {[hex(c) for c in constructors]}')
    if not constructors or len(constructors) > 2:
        fail(f'found {len(constructors)} protected constructors')

    # D2 is the tail call four bytes before D1, and both reach the same parent.
    def tail_call(address: int) -> int:
        word = struct.unpack('<I', read(address, 4))[0]
        if word & 0xfc000000 != 0x14000000:
            fail(f'{address:#x} is not a one-instruction destructor')
        delta = word & 0x3ffffff
        return address + (delta - (1 << 26) if delta & (1 << 25) else delta) * 4
    if tail_call(pointers[0] - 4) != tail_call(pointers[0]):
        fail('D1 and D2 do not reach the same parent destructor')

    bindings = {METACLASS: metaclass, CONSTRUCTOR: constructors[0], BASE_DESTRUCTOR: pointers[0] - 4}
    for index, name in enumerate(names):
        if name in export_at:
            continue
        own = name.replace(LOCAL, MANGLED)
        if MANGLED not in own:
            fail(f'slot {index} ({name}) is neither a kernel export nor this class')
        bindings[own] = pointers[index]

    recorded = args.recorded or (args.tree / 'scripts/data/native-aes-original-bindings.S')
    expected = {line.split()[1].rstrip(',') for line in recorded.read_text().splitlines() if line.startswith('.set')}
    if set(bindings) != expected:
        fail(f'symbol set differs: missing {sorted(expected - set(bindings))}, extra {sorted(set(bindings) - expected)}')

    slide = int(args.collection_slide, 16)
    lines = [f'// {CLASS} in {KEXT}, measured by scripts/scan-prelink-aes.py from the kernelcache',
             f'// with sha256 {hashlib.sha256(fat).hexdigest()}, moved by the prelink slide {args.collection_slide}.']
    for name, value in sorted(bindings.items()):
        lines += [f'.globl {name}', f'.set {name}, {value + slide:#x}']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines) + '\n')
    print(f'measured {len(bindings)} bindings: table {table:#x}, metaclass {metaclass:#x}, constructor {constructors[0]:#x}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
