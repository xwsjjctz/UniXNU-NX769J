#!/usr/bin/env python3
"""Build the DarwinBoot trust cache bundle from a restore image's IM4P files.

The IPSW carries each trust cache wrapped in IM4P. DarwinBoot takes a bundle: a
u32 count, one u32 offset per cache, then the raw version 2 modules. This unwraps
the static and system caches unchanged and checks what DarwinBoot checks.

    scripts/make-ipsw-trust-bundle.py --ipsw restore.ipsw --output trust.bundle
"""
import argparse
import hashlib
import plistlib
import struct
import zipfile
from pathlib import Path

MEMBERS = ('StaticTrustCache', 'Cryptex1,SystemTrustCache')


def der(data, at):
    tag, length = data[at], data[at + 1]
    at += 2
    if length & 0x80:
        count = length & 0x7f
        length = int.from_bytes(data[at:at + count], 'big')
        at += count
    return tag, at, length


def payload(im4p):
    tag, at, length = der(im4p, 0)
    if tag != 0x30:
        raise SystemExit('Not an IM4P sequence')
    end = at + length
    fields = []
    while at < end:
        tag, body, length = der(im4p, at)
        fields.append((tag, im4p[body:body + length]))
        at = body + length
    if fields[0][1] != b'IM4P' or fields[3][0] != 0x04:
        raise SystemExit('Unexpected IM4P layout')
    return fields[3][1]


def check(module):
    if len(module) < 24 or struct.unpack_from('<I', module)[0] != 2:
        raise SystemExit('Not a version 2 trust cache')
    entries = struct.unpack_from('<I', module, 20)[0]
    if len(module) != 24 + entries * 24:
        raise SystemExit(f'{entries} entries do not fill {len(module)} bytes')
    hashes = [module[24 + 24 * index:24 + 24 * index + 20] for index in range(entries)]
    if hashes != sorted(hashes) or any(not 1 <= module[24 + 24 * index + 20] <= 4 for index in range(entries)):
        raise SystemExit('Entries are not sorted or a hash type is out of range')
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--ipsw', type=Path, required=True)
    parser.add_argument('--device-class', default='d22ap')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with zipfile.ZipFile(args.ipsw) as archive:
        manifest = plistlib.loads(archive.read('BuildManifest.plist'))
        identity = next(item for item in manifest['BuildIdentities']
                        if item['Info'].get('DeviceClass') == args.device_class)
        modules = [payload(archive.read(identity['Manifest'][name]['Info']['Path'])) for name in MEMBERS]
    for name, module in zip(MEMBERS, modules):
        print(f'{name}: {check(module)} entries, {len(module)} bytes')
    position = 4 + 4 * len(modules)
    offsets = []
    for module in modules:
        offsets.append(position)
        position += len(module)
    bundle = struct.pack('<I', len(modules)) + b''.join(struct.pack('<I', value) for value in offsets) + b''.join(modules)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(bundle)
    print(f'{args.output}: {len(bundle)} bytes, sha256 {hashlib.sha256(bundle).hexdigest()}')


if __name__ == '__main__':
    main()
