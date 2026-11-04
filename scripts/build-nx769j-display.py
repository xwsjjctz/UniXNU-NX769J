#!/usr/bin/env python3
"""Compile the board display (package/display) into an archive the kernel links.

The sources are the houji port's built-in IOSurface and IOMobileFramebuffer (SweetSurface.cpp, SweetFramebuffer.cpp)
adapted to the GOP framebuffer, plus DisplayBoard.cpp for the hooks they expect. They are compiled with the kernel's own
compile record, as build-arm-driver.py does, with SWEET defined because the sources are guarded by it.

They replace com.apple.iokit.IOSurface and IOMobileGraphicsFamily, which the OS profile leaves out of the kernelcache.

Output: build/nx769j-display/libNX769JDisplay.a, linked by CONFIG_XNU_EXTRA_LD_ARCHIVES.
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
SOURCES = DEVICE / 'package/display'
UNITS = ('SweetSurface.cpp', 'SweetFramebuffer.cpp', 'SweetWatchdog.cpp', 'DisplayBoard.cpp')
OUT = ROOT / 'build/nx769j-display'
RECIPE = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
# The houji sources are written for its own warning set; keep -Werror for what the kernel itself enforces.
QUIET = ['-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-unused-function', '-Wno-unused-but-set-variable',
         '-Wno-sign-compare', '-Wno-missing-field-initializers', '-Wno-c99-designator', '-Wno-reorder-init-list',
         '-Wno-deprecated-enum-enum-conversion', '-Wno-unnecessary-virtual-specifier', '-Wno-shadow',
         '-Wno-implicit-fallthrough', '-Wno-unused-private-field', '-Wno-format']


def compile_command(record, source, destination):
    """The IOPlatformExpert compile line with this source and output."""
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
    return command + ['-Wall', '-Werror', *QUIET, '-DSWEET=1', '-I', str(SOURCES),
                      '-I', str(ROOT / '.cache/xnu-work/osfmk')]


def main():
    record = json.loads(RECIPE.read_text().strip().rstrip(','))
    OUT.mkdir(parents=True, exist_ok=True)
    objects, commands = [], {}
    for unit in UNITS:
        source = SOURCES / unit
        if not source.is_file():
            raise SystemExit(f'{source} missing')
        destination = OUT / (Path(unit).stem + '.o')
        command = compile_command(record, source, destination)
        commands[unit] = command
        result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (OUT / f'{Path(unit).stem}.log').write_text(result.stdout)
        if result.returncode:
            raise SystemExit(f'{unit}:\n' + result.stdout[-3500:])
        check_object(destination.read_bytes(), unit)
        objects.append(destination)
    (OUT / 'compile-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    archive = OUT / 'libNX769JDisplay.a'
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', archive, *objects], check=True)
    print(f'NX769J display: {len(objects)} objects, {TRIPLE} object audit passed')


if __name__ == '__main__':
    main()
