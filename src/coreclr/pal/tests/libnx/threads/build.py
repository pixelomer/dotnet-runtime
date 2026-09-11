#!/usr/bin/env python3
"""Build production CoreCLR thread boundary and shared reaper with Horizon tests.

Follow src/coreclr/pal/tests/libnx/file-mapping/README.md for toolchain/ICU
prerequisites, libnx acquisition and complete SDK staging. Use libnx source
revision cb5645686f762feb9e3a8c21cd683b1044df615c (pthreadGetNativeHandle)
in that recipe instead of its file-mapping-only pin. Configure CoreCLR with
the staged LIBNX_ROOT, then run this script from the runtime root.
Only production platform/reaper sources are linked; unused PAL APIs are
removed at link time. This is not a full PAL thread-management test.
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
    parser.error('Cross-configure CoreCLR first; see this script's docstring')
flags = {}
for line in flags_file.read_text().splitlines():
    if ' = ' in line:
        name, value = line.split(' = ', 1)
        flags[name] = shlex.split(value)
compiler = Path(os.environ.get('DEVKITA64', '/opt/devkitpro/devkitA64')) / 'bin/aarch64-none-elf-g++'
devkitpro = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
libnx_root = devkitpro / 'libnx'
for line in (build / 'CMakeCache.txt').read_text().splitlines():
    if line.startswith('LIBNX_ROOT:PATH='):
        libnx_root = Path(line.split('=', 1)[1])
output = repo / 'artifacts/libnx-coreclr-threads'
output.mkdir(parents=True, exist_ok=True)
compile_flags = flags['CXX_DEFINES'] + flags['CXX_INCLUDES'] + flags['CXX_FLAGS']
objects = []
for unit in [source / 'main.cpp', repo / 'src/coreclr/pal/src/thread/libnx/threadplatform.cpp',
             repo / 'src/native/libs/Common/pal_threading_libnx.c']:
    obj = output / (unit.stem + '.o')
    cc = compiler if unit.suffix != '.c' else compiler.with_name('aarch64-none-elf-gcc')
    unit_flags = compile_flags if unit.suffix != '.c' else flags['C_DEFINES'] + flags['C_INCLUDES'] + flags['C_FLAGS']
    subprocess.run([str(cc), *unit_flags, '-c', str(unit), '-o', str(obj)], check=True)
    objects.append(str(obj))
target = output / 'coreclr-thread-probe'
subprocess.run([str(compiler), '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIE',
                '-specs=' + str(libnx_root / 'switch.specs'), '-g', '-Wl,--gc-sections',
                '-Wl,-Map,' + str(target.with_suffix('.map')), *objects,
                '-L' + str(libnx_root / 'lib'), '-lnx', '-o', str(target.with_suffix('.elf'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/nacptool'), '--create', 'CoreCLR thread bridge',
                'Runtime research', '1.0.0', str(target.with_suffix('.nacp'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/elf2nro'), str(target.with_suffix('.elf')),
                str(target.with_suffix('.nro')), '--nacp=' + str(target.with_suffix('.nacp'))], check=True)
print(target.with_suffix('.nro'))
