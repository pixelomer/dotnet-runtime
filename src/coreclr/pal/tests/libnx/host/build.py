#!/usr/bin/env python3
"""Build an embedded CoreCLR host with the production static runtime.

Follow src/coreclr/pal/tests/libnx/threads/README.md for devkitPro/ICU,
pinned libnx SDK staging and CoreCLR cross-configuration. Export
ICU_NX_INSTALL_DIR to the source-built ICU installation. Install dnfile,
pyelftools and pefile in the Python environment running this helper; see the sibling
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
parser.add_argument('--watchdog-seconds', type=int, help='Override integration watchdog; 0 disables it for interactive applications')
parser.add_argument('--application-entry', help='Deploy and execute an existing IL-only entry DLL directly instead of compiling a probe')
parser.add_argument('--managed-source', type=Path, help='Original integration probe source to compile in place of the built-in probe')
parser.add_argument('--managed-reference', type=Path, action='append', default=[], help='Additional IL-only reference to deploy')
parser.add_argument('--native-object', type=Path, action='append', default=[], help='Additional reviewed native integration object')
parser.add_argument('--native-library', type=Path, action='append', default=[], help='Additional static archive in the runtime dependency link group')
parser.add_argument('--export-symbol', action='append', default=[], help='Retain and export an explicit host integration entry point')
parser.add_argument('--wrap-symbol', action='append', default=[], help='Link a reviewed __wrap_NAME diagnostic provided by a native object')
parser.add_argument('--managed-directory', default='/switch/coreclr-probe', help='Absolute SD path without a device prefix')
parser.add_argument('--log-prefix', default='/switch/coreclr-host', help='Absolute SD log prefix without a device prefix')
parser.add_argument('--r2r-input', type=Path, help='Owned ReadyToRun control DLL for the r2r probe')
parser.add_argument('--framework', type=Path, help='Override compatible source-built framework assemblies')
parser.add_argument('--corelib', type=Path, help='Override CoreLib for explicit compatibility controls')
parser.add_argument('--dotnet-root', type=Path, help='Reuse an explicitly selected SDK installation of the source-pinned version')
parser.add_argument('--minopts', action='store_true', help='Exercise minimum-optimization JIT code generation')
parser.add_argument('--probe', choices=['basic', 'stress', 'suspension', 'bcl', 'sockets', 'suspension-flows', 'soak', 'r2r'], default='basic')
args = parser.parse_args()
if args.watchdog_seconds is not None and not 0 <= args.watchdog_seconds <= 3600: parser.error("watchdog-seconds must be 0..3600")
if args.application_entry:
    if (not args.application_entry.endswith('.dll') or
        not all(c.isascii() and (c.isalnum() or c in '._-') for c in args.application_entry) or
        args.application_entry in ('Probe.dll', 'System.Private.CoreLib.dll')):
        parser.error('application-entry must be a plain non-reserved DLL basename')
    if args.managed_source or args.probe != 'bcl':
        parser.error('application-entry requires --probe bcl and no managed-source')
    if not any(p.name == args.application_entry and p.is_file() for p in args.managed_reference):
        parser.error('application-entry must be supplied as a managed-reference')
for path in (args.managed_directory, args.log_prefix):
    if not path.startswith('/switch/') or any(c in path for c in '\n\r"\\:') or '..' in Path(path).parts:
        parser.error('Integration paths must be plain absolute paths under /switch')
for symbol in args.export_symbol + args.wrap_symbol:
    if not symbol or not all(c.isalnum() or c == '_' for c in symbol): parser.error('Invalid export symbol')
for archive in args.native_library:
    if not archive.is_file() or archive.suffix != '.a': parser.error('Native libraries must be existing static archives')

if args.probe == 'r2r' and (args.r2r_input is None or not args.r2r_input.is_file()):
    parser.error('--r2r-input is required for the r2r probe')
build = repo / 'artifacts/obj/coreclr/libnx.arm64.Release' / args.configuration
flags_file = build / 'pal/src/CMakeFiles/coreclrpal_objects.dir/flags.make'
if not flags_file.is_file():
    parser.error("Cross-configure CoreCLR first; see this script's docstring")
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
compile_flags += ['-DHOST_MANAGED_DIR="' + args.managed_directory + '"', '-DHOST_LOG_PREFIX="' + args.log_prefix + '"']
if args.application_entry:
    compile_flags += ['-DHOST_APPLICATION_ENTRY', '-DHOST_ENTRY_ASSEMBLY="' + args.application_entry + '"']
if args.watchdog_seconds is not None:
    compile_flags += ["-DHOST_WATCHDOG_SECONDS=" + str(args.watchdog_seconds)]
if args.jit_trace:
    compile_flags += ["-DHOST_JIT_TRACE"]
if args.probe in ('suspension', 'suspension-flows', 'soak'):
    compile_flags += ['-DHOST_SUSPENSION_PROBE']
if args.probe in ('bcl', 'sockets', 'suspension-flows', 'soak', 'r2r'):
    compile_flags += ['-DHOST_BCL_PROBE']
if args.probe == 'sockets':
    compile_flags += ['-DHOST_SOCKET_PROBE']
if args.probe == 'soak':
    compile_flags += ['-DHOST_SOAK_PROBE']
if args.probe == 'r2r':
    compile_flags += ['-DHOST_R2R_PROBE']
if args.minopts:
    compile_flags += ['-DHOST_MINOPTS']
objects = []
units = [source / 'main.cpp', source / 'trace.cpp']
if args.probe == 'sockets':
    units.append(source / 'SocketPollTrace.cpp')
for unit in units:
    obj = output / (unit.stem + '.o')
    unit_flags = compile_flags if unit.suffix == '.cpp' else flags['ASM_DEFINES'] + flags['ASM_INCLUDES'] + flags['ASM_FLAGS']
    subprocess.run([str(compiler), *unit_flags, '-c', str(unit), '-o', str(obj)], check=True)
    objects.append(str(obj))
objects += [str(p.resolve()) for p in args.native_object]
for symbol in args.export_symbol:
    objects += ['-Wl,--undefined=' + symbol, '-Wl,--export-dynamic-symbol=' + symbol]
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
icu = Path(os.environ.get('ICU_NX_INSTALL_DIR', str(repo/'artifacts/horizon/icu')))
if args.probe == 'sockets':
    objects.append('-Wl,--wrap=poll')
objects += ['-Wl,--wrap=' + symbol for symbol in args.wrap_symbol]
objects += ['-Wl,--start-group', *[str(build / p) for p in archives], *[str(icu/'lib'/p) for p in ['libicui18n.a', 'libicuuc.a', 'libicudata.a']], *[str(p.resolve()) for p in args.native_library], '-Wl,--end-group']
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
if args.probe in ('bcl', 'sockets', 'suspension-flows', 'soak', 'r2r'):
    framework = args.framework or repo / 'artifacts/bin/runtime/net10.0-libnx-Release-arm64'
    if not (framework / 'System.Runtime.dll').is_file():
        parser.error('Build libs.sfx for CoreCLR/libnx before the BCL probe')
    for assembly in sorted(framework.glob('*.dll')):
        if assembly.name in (corelib.name, 'Probe.dll'):
            continue
        shutil.copyfile(assembly, managed / assembly.name)
        references.append('-r:' + str(assembly))
for assembly in args.managed_reference:
    destination = managed/assembly.name
    if destination.exists() and destination.read_bytes() != assembly.read_bytes():
        parser.error('Managed reference collides with framework: ' + assembly.name)
    if assembly.name in (corelib.name, 'Probe.dll'): parser.error('Reserved managed reference name')
    shutil.copyfile(assembly, destination)
    references.append('-r:' + str(assembly.resolve()))
if args.probe == 'r2r':
    shutil.copyfile(args.r2r_input, managed/'OwnedReadyToRun.dll')
shutil.copyfile(corelib, managed / corelib.name)
if not args.application_entry:
    sdk = json.loads((repo/'global.json').read_text())['sdk']['version']
    dotnet_root = args.dotnet_root.resolve() if args.dotnet_root else repo/'.dotnet'
    subprocess.run([str(dotnet_root/'dotnet'), str(dotnet_root/'sdk'/sdk/'Roslyn/bincore/csc.dll'),
                    '-nologo', '-noconfig', '-nostdlib+', '-deterministic+', '-unsafe+', '-target:exe', '-optimize+',
                    '-r:' + str(corelib), *references, '-out:' + str(managed/'Probe.dll'),
                    str((args.managed_source.resolve() if args.managed_source else source / {'basic': 'Probe.cs', 'stress': 'Stress.cs', 'suspension': 'Suspension.cs', 'bcl': 'BclProbe.cs', 'sockets': 'SocketProbe.cs', 'suspension-flows': 'SuspensionFlows.cs', 'soak': 'Soak.cs', 'r2r': 'ReadyToRunProbe.cs'}[args.probe]))], check=True)
with (output/'qcall-validation.json').open('w') as result:
    subprocess.run([sys.executable, str(source/'validate-qcalls.py'), str(corelib),
                    str(target.with_suffix('.elf'))], stdout=result, check=True)

# The r2r probe is an explicit unsupported-format control, not a deployment.
validator = source/'validate-il.py'
subprocess.run(['python3', str(validator), *[str(p) for p in sorted(managed.glob('*.dll'))
    if not (args.probe == 'r2r' and p.name == 'OwnedReadyToRun.dll')]], check=True)

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

linked_inputs = {}
for line in target.with_suffix('.map').read_text().splitlines():
    if line.startswith('LOAD '):
        path = Path(line[5:].strip())
        if path.is_file():
            linked_inputs[str(path.resolve())] = digest(path)
managed_source = (args.managed_source.resolve() if args.managed_source else source / {'basic': 'Probe.cs', 'stress': 'Stress.cs', 'suspension': 'Suspension.cs', 'bcl': 'BclProbe.cs', 'sockets': 'SocketProbe.cs', 'suspension-flows': 'SuspensionFlows.cs', 'soak': 'Soak.cs', 'r2r': 'ReadyToRunProbe.cs'}[args.probe])
snapshot = output/'source-snapshot'
if snapshot.exists():
    shutil.rmtree(snapshot)
snapshot.mkdir()
source_inputs = [Path(__file__), validator, *units] + ([] if args.application_entry else [managed_source])
for path in source_inputs:
    shutil.copyfile(path, snapshot/path.name)
manifest = {
    'managed_directory': args.managed_directory, 'log_prefix': args.log_prefix, 'export_symbols': args.export_symbol,
    'native_objects': {str(p.resolve()): digest(p) for p in args.native_object},
    'native_libraries': {str(p.resolve()): digest(p) for p in args.native_library},
    'wrap_symbols': args.wrap_symbol,
    'source_base': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
    'probe': args.probe, 'application_entry': args.application_entry, 'watchdog_seconds': args.watchdog_seconds, 'jit_trace': args.jit_trace, 'minopts': args.minopts,
    'source_sha256': {str(path.relative_to(repo) if path.is_relative_to(repo) else path): digest(path) for path in source_inputs},
    'corelib_input': str(corelib),
    'managed_sha256': {path.name: digest(path) for path in sorted(managed.glob('*.dll'))},
    'linked_input_sha256': linked_inputs,
    'nro_sha256': digest(target.with_suffix('.nro')),
    'elf_sha256': digest(target.with_suffix('.elf')),
    'libnx_root': str(libnx_root),
}
(output/'build-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
(output/'source-diff.patch').write_bytes(subprocess.check_output(['git', 'diff', '--binary'], cwd=repo))
