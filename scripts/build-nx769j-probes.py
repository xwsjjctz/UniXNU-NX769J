#!/usr/bin/env python3
"""Compile the NX769J CPU pieces into an archive the kernel links: package/cpu/SmpReport.cpp (secondary CPU state for
the heartbeat) and package/cpu/CpuDvfs.cpp (EPSS frequency governor).

They are compiled with the IOPlatformExpert kernel compile record, as build-arm-driver.py does, so the object matches the kernel ABI.

Output: build/nx769j-probes/libNX769JProbes.a, linked by CONFIG_XNU_EXTRA_LD_ARCHIVES.
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
SOURCES = (DEVICE / 'package/cpu/SmpReport.cpp', DEVICE / 'package/cpu/CpuDvfs.cpp')
OUT = ROOT / 'build/nx769j-probes'
RECIPE = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'


def compile_command(record, destination, source):
    """The IOPlatformExpert compile line with this source and output, warnings as errors."""
    arguments, command, index = record['arguments'], [], 0
    while index < len(arguments):
        value = arguments[index]
        if value == record['file']:
            command.append(str(source))
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
    for source in SOURCES:
        if not source.is_file():
            raise SystemExit(f'{source} missing')
    OUT.mkdir(parents=True, exist_ok=True)
    objects = []
    record = json.loads(RECIPE.read_text().strip().rstrip(','))
    for source in SOURCES:
        name = source.stem
        destination = OUT / f'{name}.o'
        command = compile_command(record, destination, source)
        (OUT / f'{name}-compile-command.json').write_text(json.dumps(command, indent=2) + '\n')
        result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (OUT / f'{name}-compile.log').write_text(result.stdout)
        if result.returncode:
            raise SystemExit(result.stdout[-3500:])
        check_object(destination.read_bytes(), name)
        objects.append(destination)
    archive = OUT / 'libNX769JProbes.a'
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', archive, *objects], check=True)
    print(f'NX769J probes: -Werror build and {TRIPLE} object audit passed')


if __name__ == '__main__':
    main()
