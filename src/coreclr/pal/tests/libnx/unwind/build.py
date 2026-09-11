#!/usr/bin/env python3
"""Build the production PAL DWARF unwinder with real AArch64 spill/return tests.

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
output = repo / 'artifacts/libnx-coreclr-unwind'
output.mkdir(parents=True, exist_ok=True)
compile_flags = flags['CXX_DEFINES'] + ['-D_LIBUNWIND_IS_NATIVE_ONLY', '-D_LIBUNWIND_DISABLE_ZERO_COST_APIS=1', '-I' + str(repo / 'src/native/external/llvm-libunwind/include')] + flags['CXX_INCLUDES'] + flags['CXX_FLAGS']
objects = []
for unit in [source / 'frames.S', source / 'main.cpp', repo / 'src/coreclr/pal/src/exception/libnx/unwind.cpp']:
    obj = output / (unit.stem + '.o')
    subprocess.run([str(compiler), *compile_flags, '-c', str(unit), '-o', str(obj)], check=True)
    objects.append(str(obj))
target = output / 'coreclr-unwind-probe'
subprocess.run([str(compiler), '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIE',
                '-specs=' + str(devkitpro / 'libnx/switch.specs'), '-g', '-Wl,--gc-sections',
                '-Wl,-Map,' + str(target.with_suffix('.map')), '-Wl,--eh-frame-hdr',
                '-Wl,-T,' + str(repo / 'src/coreclr/nativeaot/Runtime/libnx/unwind-sections.ld'), *objects,
                '-L' + str(devkitpro / 'libnx/lib'), '-lnx', '-o', str(target.with_suffix('.elf'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/nacptool'), '--create', 'CoreCLR unwind probe',
                'Runtime research', '1.0.0', str(target.with_suffix('.nacp'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/elf2nro'), str(target.with_suffix('.elf')),
                str(target.with_suffix('.nro')), '--nacp=' + str(target.with_suffix('.nacp'))], check=True)
print(target.with_suffix('.nro'))
