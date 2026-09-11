#!/usr/bin/env python3
"""Build production PAL module lookup and startup with Horizon tests.

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
libnx_root = devkitpro / 'libnx'
for line in (build / 'CMakeCache.txt').read_text().splitlines():
    if line.startswith('LIBNX_ROOT:PATH='):
        libnx_root = Path(line.split('=', 1)[1])
output = repo / 'artifacts/libnx-coreclr-modules'
output.mkdir(parents=True, exist_ok=True)
compile_flags = flags['CXX_DEFINES'] + flags['CXX_INCLUDES'] + flags['CXX_FLAGS']
objects = []
for unit in [source / 'main.cpp']:
    obj = output / (unit.stem + '.o')
    unit_flags = compile_flags if unit.suffix == '.cpp' else flags['ASM_DEFINES'] + flags['ASM_INCLUDES'] + flags['ASM_FLAGS']
    subprocess.run([str(compiler), *unit_flags, '-c', str(unit), '-o', str(obj)], check=True)
    objects.append(str(obj))
# Use production objects from the current PAL build. Package them as an archive
# so only the real SEH/context/unwind dependencies are pulled into this NRO.
archive = output / 'libpal-boundary.a'
if archive.exists(): archive.unlink()
# Select objects from the current target only. All PAL objects must exist.
makefile = flags_file.with_name('build.make').read_text().splitlines()
units = sorted({build / line.split(': ', 1)[1] for line in makefile
                if line.startswith('coreclrpal_objects: ') and line.endswith('.obj')})
assert units and all(unit.is_file() for unit in units), "Build all PAL objects first"
subprocess.run([str(compiler.with_name('aarch64-none-elf-ar')), 'rcs', str(archive), *map(str, units)], check=True)
objects += ['-Wl,--start-group', str(archive), str(build / 'minipal/Unix/libcoreclrminipal.a'), str(build / 'shared_minipal/libminipal.a'), '-Wl,--end-group']
target = output / 'coreclr-module-probe'
subprocess.run([str(compiler), '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIE',
                '-specs=' + str(libnx_root / 'switch.specs'), '-g', '-Wl,--gc-sections',
                '-Wl,-Map,' + str(target.with_suffix('.map')), '-Wl,--eh-frame-hdr', '-Wl,--export-dynamic-symbol=ModuleProbeFunction', '-Wl,--export-dynamic-symbol=ModuleProbeData',
                '-Wl,-T,' + str(repo / 'src/coreclr/nativeaot/Runtime/libnx/unwind-sections.ld'), *objects,
                '-L' + str(libnx_root / 'lib'), '-lnx', '-o', str(target.with_suffix('.elf'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/nacptool'), '--create', 'CoreCLR native modules',
                'Runtime research', '1.0.0', str(target.with_suffix('.nacp'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/elf2nro'), str(target.with_suffix('.elf')),
                str(target.with_suffix('.nro')), '--nacp=' + str(target.with_suffix('.nacp'))], check=True)
print(target.with_suffix('.nro'))
