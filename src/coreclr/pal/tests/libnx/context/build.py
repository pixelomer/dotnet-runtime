#!/usr/bin/env python3
"""Build the actual PAL context.cpp plus a small Horizon conversion test.

Requires the documented CoreCLR cross configure. No fake PAL/context types or
runtime-success stubs are used. Unreferenced PAL APIs are removed at link time.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

source = Path(__file__).resolve().parent
repo = source.parents[5]
parser = argparse.ArgumentParser()
parser.add_argument('--configuration', default='coreclr-probe')
args = parser.parse_args()
build = repo / 'artifacts/obj/coreclr/libnx.arm64.Release' / args.configuration
flags_file = build / 'pal/src/CMakeFiles/coreclrpal_objects.dir/flags.make'
if not flags_file.is_file():
    parser.error('Cross-configure CoreCLR first; see README.md')
flags = {}
for line in flags_file.read_text().splitlines():
    if ' = ' in line:
        name, value = line.split(' = ', 1)
        flags[name] = shlex.split(value)
compiler = Path(os.environ.get('DEVKITA64', '/opt/devkitpro/devkitA64')) / 'bin/aarch64-none-elf-g++'
devkitpro = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
output = repo / 'artifacts/libnx-coreclr-context'
output.mkdir(parents=True, exist_ok=True)
compile_flags = flags['CXX_DEFINES'] + flags['CXX_INCLUDES'] + flags['CXX_FLAGS']
objects = []
for unit in [source / 'main.cpp', repo / 'src/coreclr/pal/src/thread/context.cpp']:
    obj = output / (unit.stem + '.o')
    subprocess.run([str(compiler), *compile_flags, '-c', str(unit), '-o', str(obj)], check=True)
    objects.append(str(obj))
target = output / 'coreclr-context-probe'
subprocess.run([str(compiler), '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIE',
                '-specs=' + str(devkitpro / 'libnx/switch.specs'), '-g', '-Wl,--gc-sections',
                '-Wl,-Map,' + str(target.with_suffix('.map')), *objects,
                '-L' + str(devkitpro / 'libnx/lib'), '-lnx', '-o', str(target.with_suffix('.elf'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/nacptool'), '--create', 'CoreCLR context probe',
                'Runtime research', '1.0.0', str(target.with_suffix('.nacp'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/elf2nro'), str(target.with_suffix('.elf')),
                str(target.with_suffix('.nro')), '--nacp=' + str(target.with_suffix('.nacp'))], check=True)
print(target.with_suffix('.nro'))
