#!/usr/bin/env python3
"""Build an embedded CoreCLR host with the production static runtime.

Follow src/coreclr/pal/tests/libnx/threads/README.md for devkitPro/ICU,
pinned libnx SDK staging and CoreCLR cross-configuration. Export
ICU_NX_INSTALL_DIR to the source-built ICU installation. Install dnfile and
pyelftools in the Python environment running this helper; see the sibling
README.md for setup. From the runtime root:

    ./build.sh clr.corelib -os libnx -arch arm64 -c Release /p:PublicSign=true
    cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
      --target coreclr_static -- -j6
    python3 src/coreclr/pal/tests/libnx/host/build.py

The repository build bootstraps its pinned SDK. The host helper links native
archives and compiles the selected managed probe.
The default CoreLib comes from this checkout; framework-backed variants also
need the source-built libs.sfx framework. See README.md for those commands and options.
Use Python 3.11 or newer. Each build replaces managed and source-snapshot
subdirectories under its selected output; keep user inputs elsewhere.
--probe basic is the default. --jit-trace removes the existing
sdmc:/switch/coreclr-jit-disasm.txt before writing buffered disassembly.
Copy artifacts/libnx-coreclr-host/managed to /switch/coreclr-probe on the SD
card, preserving any existing files there. Logs under /switch are overwritten.
"""
import argparse
import hashlib
import json
import shutil
import os
from pathlib import Path
import shlex
import subprocess
import sys

source = Path(__file__).resolve().parent
repo = source.parents[5]
parser = argparse.ArgumentParser()
parser.add_argument('--configuration', default='coreclr-probe')
parser.add_argument('--output', type=Path, help='Keep a probe variant in a separate artifact directory')
parser.add_argument('--jit-trace', action='store_true')
parser.add_argument('--framework', type=Path, help='Override compatible source-built framework assemblies')
parser.add_argument('--corelib', type=Path, help='Override CoreLib for explicit compatibility controls')
parser.add_argument('--minopts', action='store_true', help='Exercise minimum-optimization JIT code generation')
parser.add_argument('--probe', choices=['basic', 'stress', 'suspension', 'bcl', 'sockets'], default='basic')
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
output = args.output.resolve() if args.output else repo / 'artifacts/libnx-coreclr-host'
output.mkdir(parents=True, exist_ok=True)
compile_flags = flags['CXX_DEFINES'] + flags['CXX_INCLUDES'] + flags['CXX_FLAGS'] + ['-I' + str(repo/'src/coreclr/hosts/inc')]
if args.jit_trace:
    compile_flags += ["-DHOST_JIT_TRACE"]
if args.probe == 'suspension':
    compile_flags += ['-DHOST_SUSPENSION_PROBE']
if args.probe in ('bcl', 'sockets'):
    compile_flags += ['-DHOST_BCL_PROBE']
if args.probe == 'sockets':
    compile_flags += ['-DHOST_SOCKET_PROBE']
if args.minopts:
    compile_flags += ['-DHOST_MINOPTS']
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
    'libs-native/System.Native/libSystem.Native.a',
    'libs-native/System.IO.Compression.Native/libSystem.IO.Compression.Native.a',
    '_deps/fetchzlibng-build/libz.a',
    '_deps/brotli-build/libbrotlienc.a',
    '_deps/brotli-build/libbrotlidec.a',
    '_deps/brotli-build/libbrotlicommon.a',
]
icu = Path(os.environ['ICU_NX_INSTALL_DIR'])
objects += ['-Wl,--start-group', *[str(build / p) for p in archives], *[str(icu/'lib'/p) for p in ['libicui18n.a', 'libicuuc.a', 'libicudata.a']], '-Wl,--end-group']
target = output / 'coreclr-host-probe'
subprocess.run([str(compiler), '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIE',
                '-specs=' + str(libnx_root / 'switch.specs'), '-g', '-Wl,--gc-sections',
                '-Wl,-Map,' + str(target.with_suffix('.map')), '-Wl,--eh-frame-hdr', '-Wl,--wrap=VirtualProtect', '-Wl,--wrap=virtmemFindStack', '-Wl,--wrap=CreateFileW', '-Wl,--wrap=WideCharToMultiByte', '-Wl,--wrap=abort', '-Wl,--wrap=PAL_LibnxBeginException', '-Wl,--wrap=exit', '-Wl,--wrap=_exit', '-Wl,--export-dynamic-symbol=coreclr_initialize',
                '-Wl,-T,' + str(repo / 'src/coreclr/nativeaot/Runtime/libnx/unwind-sections.ld'), *objects,
                '-L' + str(libnx_root / 'lib'), '-lnx', '-o', str(target.with_suffix('.elf'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/nacptool'), '--create', 'CoreCLR embedded host',
                'Runtime research', '1.0.0', str(target.with_suffix('.nacp'))], check=True)
subprocess.run([str(devkitpro / 'tools/bin/elf2nro'), str(target.with_suffix('.elf')),
                str(target.with_suffix('.nro')), '--nacp=' + str(target.with_suffix('.nacp'))], check=True)
print(target.with_suffix('.nro'))

# Compile a small IL-only application against this target's actual CoreLib.
corelib = args.corelib or repo / 'artifacts/bin/coreclr/libnx.arm64.Release/IL/System.Private.CoreLib.dll'
if not corelib.is_file():
    parser.error('Build clr.corelib -os libnx -arch arm64 -c Release /p:PublicSign=true first')
managed = output / 'managed'
if managed.exists():
    shutil.rmtree(managed)
managed.mkdir()
references = []
if args.probe in ('bcl', 'sockets'):
    framework = args.framework or repo / 'artifacts/bin/runtime/net10.0-libnx-Release-arm64'
    if not (framework / 'System.Runtime.dll').is_file():
        parser.error('Build libs.sfx for CoreCLR/libnx before the BCL probe')
    for assembly in sorted(framework.glob('*.dll')):
        if assembly.name in (corelib.name, 'Probe.dll'):
            continue
        shutil.copyfile(assembly, managed / assembly.name)
        references.append('-r:' + str(assembly))
shutil.copyfile(corelib, managed / corelib.name)
sdk = json.loads((repo/'global.json').read_text())['sdk']['version']
subprocess.run([str(repo/'.dotnet/dotnet'), str(repo/'.dotnet/sdk'/sdk/'Roslyn/bincore/csc.dll'),
                '-nologo', '-noconfig', '-nostdlib+', '-deterministic+', '-unsafe+', '-target:exe', '-optimize+',
                '-r:' + str(corelib), *references, '-out:' + str(managed/'Probe.dll'),
                str(source / {'basic': 'Probe.cs', 'stress': 'Stress.cs', 'suspension': 'Suspension.cs', 'bcl': 'BclProbe.cs', 'sockets': 'SocketProbe.cs'}[args.probe])], check=True)
with (output/'qcall-validation.json').open('w') as result:
    subprocess.run([sys.executable, str(source/'validate-qcalls.py'), str(corelib),
                    str(target.with_suffix('.elf'))], stdout=result, check=True)

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

linked_inputs = {}
for line in target.with_suffix('.map').read_text().splitlines():
    if line.startswith('LOAD '):
        path = Path(line[5:].strip())
        if path.is_file():
            linked_inputs[str(path.resolve())] = digest(path)
managed_source = source / {'basic': 'Probe.cs', 'stress': 'Stress.cs', 'suspension': 'Suspension.cs', 'bcl': 'BclProbe.cs', 'sockets': 'SocketProbe.cs'}[args.probe]
snapshot = output/'source-snapshot'
if snapshot.exists():
    shutil.rmtree(snapshot)
snapshot.mkdir()
for path in [Path(__file__), source/'main.cpp', source/'trace.cpp', managed_source]:
    shutil.copyfile(path, snapshot/path.name)
manifest = {
    'source_base': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
    'probe': args.probe, 'jit_trace': args.jit_trace, 'minopts': args.minopts,
    'source_sha256': {str(path.relative_to(repo)): digest(path) for path in [Path(__file__), source/'main.cpp', source/'trace.cpp', managed_source]},
    'corelib_input': str(corelib),
    'managed_sha256': {path.name: digest(path) for path in sorted(managed.glob('*.dll'))},
    'linked_input_sha256': linked_inputs,
    'nro_sha256': digest(target.with_suffix('.nro')),
    'elf_sha256': digest(target.with_suffix('.elf')),
    'libnx_root': str(libnx_root),
}
(output/'build-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
(output/'source-diff.patch').write_bytes(subprocess.check_output(['git', 'diff', '--binary'], cwd=repo))
