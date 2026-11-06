#!/usr/bin/env python3
"""Pack the NX769J GPU userland (scripts/build-nx769j-metal.sh output) into a GPU addon for the boot payload, and the trust bundle
that lets AMFI run it.

  make-nx769j-gpu-addon.py TRUST_IN ADDON_OUT TRUST_OUT [--clients NAME,...]

The addon is UniXNU's NGPUADD2 file (scripts/make-gpu-addon.py layout: "NGPUADD2", u32 count, u32 0, u64 total bytes; per file
u32 path bytes with its NUL, u32 mode, u64 size, u64 compressed size, the path padded to 8, the zlib stream padded to 8). The
kernel (zzy-native-gpu-addon-mount, boot-arg sheng-gpu-addon=1) writes each file to /private/var/ngpu/<path> and mounts
/private/var/ngpu/addons over /System/Library/Extensions, where Metal's discovery loads NativeAdrenoMetal.bundle:

  addons/NativeAdrenoMetal.bundle/NativeAdrenoMetal, Info.plist, libvulkan_freedreno.dylib (Turnip, found next to the plugin)
  addons/NativeAdrenoMetal.clients   the processes the plugin gives a device to (default vktest,mtltest; backboardd is left out)
  bin/vktest, bin/mtltest             the test programs (nx769j-execswap runs vktest in place of /usr/sbin/bluetoothd)

TRUST_OUT is TRUST_IN (the owner trust bundle the payload already uses) with one more version-2 module holding the CDHashes of
the four executables (UniXNU scripts/trust-append-module.py layout)."""
import plistlib
import re
import struct
import subprocess
import sys
import uuid
import zlib
from pathlib import Path

DEVICE = Path(__file__).resolve().parents[1]
OUT = DEVICE / '.cache/gpu-user/out'


def pad(data):
    return data + bytes(-len(data) % 8)


def cdhash(path):
    detail = subprocess.run(['codesign', '--display', '--verbose=4', str(path)], capture_output=True, text=True, check=True).stderr
    return re.search(r'^CDHash=([a-f0-9]{40})$', detail, re.M)[1]


def main():
    args = sys.argv[1:]
    clients = 'vktest,mtltest'
    if '--clients' in args:
        at = args.index('--clients')
        clients = args[at + 1]
        del args[at:at + 2]
    if len(args) != 3:
        sys.exit(__doc__)
    trust_in, addon_out, trust_out = (Path(a) for a in args)
    bundle = OUT / 'NativeAdrenoMetal.bundle'
    executables = [bundle / 'NativeAdrenoMetal', bundle / 'libvulkan_freedreno.dylib', OUT / 'vktest', OUT / 'mtltest']
    for path in executables + [bundle / 'Info.plist']:
        if not path.is_file():
            sys.exit(f'missing {path}: run scripts/build-nx769j-metal.sh first')
    files = [
        ('addons/NativeAdrenoMetal.bundle/NativeAdrenoMetal', 0o755, executables[0]),
        ('addons/NativeAdrenoMetal.bundle/Info.plist', 0o644, bundle / 'Info.plist'),
        ('addons/NativeAdrenoMetal.bundle/libvulkan_freedreno.dylib', 0o755, executables[1]),
        ('bin/vktest', 0o755, executables[2]),
        ('bin/mtltest', 0o755, executables[3]),
    ]
    body = b''
    for name, mode, path in files:
        data = path.read_bytes()
        encoded = name.encode() + b'\0'
        packed = zlib.compress(data, 9)
        body += struct.pack('<IIQQ', len(encoded), mode, len(data), len(packed)) + pad(encoded) + pad(packed)
    listed = ('addons/NativeAdrenoMetal.clients', 0o644, (clients.replace(',', '\n') + '\n').encode())
    encoded = listed[0].encode() + b'\0'
    packed = zlib.compress(listed[2], 9)
    body += struct.pack('<IIQQ', len(encoded), listed[1], len(listed[2]), len(packed)) + pad(encoded) + pad(packed)
    count = len(files) + 1
    Path(addon_out).write_bytes(b'NGPUADD2' + struct.pack('<IIQ', count, 0, 24 + len(body)) + body)

    data = trust_in.read_bytes()
    n = struct.unpack_from('<I', data)[0]
    starts = list(struct.unpack_from(f'<{n}I', data, 4)) + [len(data)]
    modules = [data[starts[i]:starts[i + 1]] for i in range(n)]
    hashes = sorted({bytes.fromhex(cdhash(p)) for p in executables})
    ident = uuid.uuid5(uuid.NAMESPACE_URL, 'org.unixnu.nx769j.gpu:' + ''.join(h.hex() for h in hashes)).bytes
    modules.append(struct.pack('<I16sI', 2, ident, len(hashes)) + b''.join(h + bytes([2, 0, 0, 0]) for h in hashes))
    cursor, offsets = 4 + 4 * len(modules), []
    for m in modules:
        offsets.append(cursor)
        cursor += len(m)
    Path(trust_out).write_bytes(struct.pack('<I', len(modules)) + struct.pack(f'<{len(modules)}I', *offsets) + b''.join(modules))
    print(f'addon {addon_out}: {count} files; trust {trust_out}: {len(modules)} modules, owner GPU module',
          ' '.join(h.hex() for h in hashes))


if __name__ == '__main__':
    main()
