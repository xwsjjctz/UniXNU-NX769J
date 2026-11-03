#!/usr/bin/env python3
"""Compile package/KeyStoreBoard.cpp, the board side of AppleKeyStoreState, into an archive the kernel links.

drivers/AppleKeyStoreState (component:AppleKeyStoreState) builds the AppleKeyStore stand-in itself; it calls
native_aks_* hooks and needs someone to publish it. KeyStoreBoard.cpp supplies both. It is compiled with the same
kernel compile record build-arm-driver.py uses, so the object matches the kernel ABI.

Output: build/nx769j-keystore/libNX769JKeyStore.a, linked by CONFIG_XNU_EXTRA_LD_ARCHIVES.
"""
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from kernel_target import TRIPLE, check_object  # noqa: E402

DEVICE = Path(os.environ.get('UNIXNU_DEVICE') or ROOT.parent / 'UniXNU-NX769J')
SOURCE = DEVICE / 'package/KeyStoreBoard.cpp'
OUT = ROOT / 'build/nx769j-keystore'
RECIPE = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'


def compile_command(record, destination):
    """The IOPlatformExpert compile line with this source and output, warnings as errors."""
    arguments, command, index = record['arguments'], [], 0
    while index < len(arguments):
        value = arguments[index]
        if value == record['file']:
            command.append(str(SOURCE))
        elif value == '-o':
            command.extend(['-o', str(destination)])
            index += 1
        elif value == '-Xclang' and arguments[index + 1].startswith('-W'):
            index += 1
        elif not value.startswith('-W'):
            command.append(value)
        index += 1
    # The same warning set as build-arm-driver.py: XNU's own headers trip the rest.
    return command + ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-c99-designator',
                      '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
                      '-Wno-unnecessary-virtual-specifier', '-I', str(ROOT / '.cache/xnu-work/osfmk')]


def main():
    if not SOURCE.is_file():
        raise SystemExit(f'{SOURCE} missing')
    record = json.loads(RECIPE.read_text().strip().rstrip(','))
    OUT.mkdir(parents=True, exist_ok=True)
    destination = OUT / 'KeyStoreBoard.o'
    command = compile_command(record, destination)
    (OUT / 'compile-command.json').write_text(json.dumps(command, indent=2) + '\n')
    result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (OUT / 'compile.log').write_text(result.stdout)
    if result.returncode:
        raise SystemExit(result.stdout[-3500:])
    check_object(destination.read_bytes(), 'KeyStoreBoard')
    archive = OUT / 'libNX769JKeyStore.a'
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', archive, destination], check=True)
    print(f'KeyStoreBoard: -Werror build and {TRIPLE} object audit passed')


if __name__ == '__main__':
    main()
