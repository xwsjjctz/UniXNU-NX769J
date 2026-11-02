#!/usr/bin/env python3
"""Skip AMFI's developer-mode PMGR latch on a COPY of the emitted kernel.

AppleMobileFileIntegrity waits 30 seconds for an ApplePMGRNub service and panics
with "AMFI: No PMGR?" when none appears. SM8650 has RPMh and AOSS rather than an
Apple PMGR block, and this port publishes no such service yet.

The function already has a path past the lookup: a cbnz on its one-time flag that
branches to the code after it. This turns the first instruction of the lookup
into an unconditional branch to the same place. Remove it when a platform power
service publishes ApplePMGRNub.

    python3 scripts/patch-amfi-no-pmgr.py kernel.macho
"""
import re
import struct
import sys
from pathlib import Path

NAME = b'ApplePMGRNub\0'
MESSAGE = b'"AMFI: No PMGR?\\n" @%s:%d\0'


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    path = Path(sys.argv[1])
    data = bytearray(path.read_bytes())
    count = struct.unpack_from('<I', data, 16)[0]
    position, segments = 32, []
    for _ in range(count):
        command, size = struct.unpack_from('<II', data, position)
        if command == 0x19:
            name = bytes(data[position + 8:position + 24]).split(b'\0')[0].decode()
            va, _, fileoff, filesize = struct.unpack_from('<QQQQ', data, position + 24)
            segments.append((name, va, fileoff, filesize))
        position += size

    def to_va(offset):
        return next((va + offset - fileoff for _, va, fileoff, size in segments
                     if size and fileoff <= offset < fileoff + size), None)

    message_at = bytes(data).find(MESSAGE)
    if message_at < 0:
        raise SystemExit('The AMFI panic message is not in this kernel')
    # AMFI keeps its own copy of the service name just before its panic message.
    names = [match.start() for match in re.finditer(re.escape(NAME), bytes(data))]
    name_at = max((at for at in names if at < message_at), default=None)
    if name_at is None or message_at - name_at > 0x100:
        raise SystemExit('The AMFI service name is not next to its panic message')
    target = to_va(name_at)

    _, text_va, text_off, text_size = next(s for s in segments if s[0] == '__PLK_TEXT_EXEC')
    sites = []
    for at in range(0, text_size - 8, 4):
        adrp, add = struct.unpack_from('<2I', data, text_off + at)
        if adrp & 0x9f00001f != 0x90000000 or add & 0xffc003ff != 0x91000000:
            continue
        immediate = ((adrp >> 5 & 0x7ffff) << 2) | (adrp >> 29 & 3)
        if immediate & 0x100000:
            immediate -= 0x200000
        va = text_va + at
        if (va & ~4095) + immediate * 4096 + (add >> 10 & 4095) == target:
            sites.append(va)
    if len(sites) != 1:
        raise SystemExit(f'Expected one lookup of the AMFI service name, found {len(sites)}')
    site = sites[0]

    guard = struct.unpack_from('<I', data, text_off + site - text_va - 4)[0]
    if guard & 0xff000000 != 0x35000000:
        raise SystemExit(f'{site:#x} does not follow the expected cbnz')
    delta = guard >> 5 & 0x7ffff
    if delta & 0x40000:
        delta -= 0x80000
    destination = site - 4 + delta * 4
    if destination <= site:
        raise SystemExit('The cbnz does not branch past the lookup')
    word = 0x14000000 | (((destination - site) // 4) & 0x3ffffff)
    struct.pack_into('<I', data, text_off + site - text_va, word)
    path.write_bytes(data)
    print(f'patched {site:#x}: b {destination:#x} ({word:#010x}), skipping the ApplePMGRNub lookup')


if __name__ == '__main__':
    main()
