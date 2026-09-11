#!/usr/bin/env python3
"""Build an embedded CoreCLR host with the production static runtime.

Follow src/coreclr/pal/tests/libnx/threads/README.md for devkitPro/ICU,
pinned libnx SDK staging and CoreCLR cross-configuration. Export
ICU_NX_INSTALL_DIR to the source-built ICU installation. From the runtime root:

    ./build.sh clr.corelib -os libnx -arch arm64 -c Release /p:PublicSign=true
    cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
      --target coreclr_static -- -j6
    python3 src/coreclr/pal/tests/libnx/host/build.py

The repository build bootstraps its pinned SDK. The host helper links native
archives and compiles Probe.cs against the target CoreLib from this checkout.
Copy artifacts/libnx-coreclr-host/managed to /switch/coreclr-probe on the SD
card, preserving any existing files there. Logs under /switch are overwritten.
"""
import argparse
import json
import shutil
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
output = repo / 'artifacts/libnx-coreclr-host'
output.mkdir(parents=True, exist_ok=True)
compile_flags = flags['CXX_DEFINES'] + flags['CXX_INCLUDES'] + flags['CXX_FLAGS'] + ['-I' + str(repo/'src/coreclr/hosts/inc')]
objects = []
for unit in [source / 'main.cpp', source / 'trace.cpp']:
    obj = output / (unit.stem + '.o')
    unit_flags = compile_flags if unit.suffix == '.cpp' else flags['ASM_DEFINES'] + flags['ASM_INCLUDES'] + flags['ASM_FLAGS']
    subprocess.run([str(compiler), *unit_flags, '-c', str(unit), '-o', str(obj)], check=True)
    objects.append(str(obj))
# coreclr_static embeds VM, GC, JIT and object libraries. Real archive
# dependencies remain separate, as in the upstream target's link interface.
archives = [
    'dlls/mscoree/coreclr/libcoreclr_static.a',
    'pal/src/libcoreclrpal.a',
    'minipal/Unix/libcoreclrminipal.a',
    'shared_minipal/libminipal.a',
    'nativeresources/libnativeresourcestring.a',
    'libs-native/System.Globalization.Native/libSystem.Globalization.Native.a',
    'libs-native/System.IO.Compression.Native/libSystem.IO.Compression.Native.a',
    '_deps/fetchzlibng-build/libz.a',
]
icu = Path(os.environ['ICU_NX_INSTALL_DIR'])
objects += ['-Wl,--start-group', *[str(build / p) for p in archives], *[str(icu/'lib'/p) for p in ['libicui18n.a', 'libicuuc.a', 'libicudata.a']], '-Wl,--end-group']
target = output / 'coreclr-host-probe'
subprocess.run([str(compiler), '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIE',
                '-specs=' + str(libnx_root / 'switch.specs'), '-g', '-Wl,--gc-sections',
                '-Wl,-Map,' + str(target.with_suffix('.map')), '-Wl,--eh-frame-hdr', '-Wl,--wrap=VirtualProtect', '-Wl,--wrap=virtmemFindStack', '-Wl,--export-dynamic-symbol=coreclr_initialize',
                '-Wl,-T,' + str(repo / 'src/coreclr/nativeaot/Runtime/libnx/unwind-sections.ld'), *objects,
                '-L' + str(libnx_root / 'lib'), '-lnx', '-o', str(target.with_suffix('.elf'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/nacptool'), '--create', 'CoreCLR embedded host',
                'Runtime research', '1.0.0', str(target.with_suffix('.nacp'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/elf2nro'), str(target.with_suffix('.elf')),
                str(target.with_suffix('.nro')), '--nacp=' + str(target.with_suffix('.nacp'))], check=True)
print(target.with_suffix('.nro'))

# Compile a small IL-only application against this target's actual CoreLib.
corelib = repo / 'artifacts/bin/coreclr/libnx.arm64.Release/IL/System.Private.CoreLib.dll'
if not corelib.is_file():
    parser.error('Build clr.corelib -os libnx -arch arm64 -c Release /p:PublicSign=true first')
managed = output / 'managed'; managed.mkdir(exist_ok=True)
shutil.copyfile(corelib, managed / corelib.name)
sdk = json.loads((repo/'global.json').read_text())['sdk']['version']
subprocess.run([str(repo/'.dotnet/dotnet'), str(repo/'.dotnet/sdk'/sdk/'Roslyn/bincore/csc.dll'),
                '-nologo', '-noconfig', '-nostdlib+', '-target:exe', '-optimize+',
                '-r:' + str(corelib), '-out:' + str(managed/'Probe.dll'), str(source/'Probe.cs')], check=True)
