#!/usr/bin/env python3
"""Package a standalone workstation NativeAOT SDK from the runtime build outputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

here = Path(__file__).resolve().parent
repo = here.parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path, help='New SDK directory; existing paths are rejected')
args = parser.parse_args()
output = args.output.resolve()
if output.exists():
    raise SystemExit('Output already exists')
versions = ET.parse(repo/'eng/Versions.props').getroot()
version = '.'.join(versions.find('.//' + key).text for key in ('MajorVersion', 'MinorVersion', 'PatchVersion'))
major = int(version.split('.')[0])
sdk = Path('artifacts/bin/coreclr/libnx.arm64.Release/aotsdk')
native = Path(f'artifacts/bin/native/net{major}.0-libnx-Release-arm64')
platform = Path('src/coreclr/nativeaot/Runtime/libnx')
files = [Path('LICENSE.TXT'), Path('THIRD-PARTY-NOTICES.TXT')]
managed_names = ['System.Private.CoreLib.dll', 'System.Private.Reflection.Execution.dll',
                 'System.Private.StackTraceMetadata.dll', 'System.Private.TypeLoader.dll']
if major == 9:
    managed_names.insert(1, 'System.Private.DisabledReflection.dll')
files += [sdk/name for name in managed_names]
files += [sdk/name for name in ('libRuntime.WorkstationGC.a', 'libbootstrapperdll.o',
                               'libeventpipe-disabled.a', 'libstandalonegc-disabled.a')]
files += [native/name for name in ('libSystem.Native.a', 'libSystem.Globalization.Native.a',
                                  'libSystem.IO.Compression.Native.a')]
files += [platform/name for name in ('create-linker-script.py', 'validate-sdk.py')]
if major == 9:
    files += [Path('artifacts/bin/System.Net.Sockets/Release/net9.0-libnx/System.Net.Sockets.dll')]
if major >= 10:
    files += [sdk/'libaotminipal.a']
for relative in files:
    if not (repo/relative).is_file() or (repo/relative).stat().st_size == 0:
        raise SystemExit('Missing SDK input: '+str(relative))
revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
manifest = {
    'format': 1, 'target': 'libnx-arm64', 'runtime_version': version,
    'source_revision': revision, 'gc': 'workstation',
    'ilc_version': version, 'application_sdk_tested': '10.0.111',
    'devkitA64_tested': '15.2.0', 'libnx_tested': '4.12.0',
    'required_ilc_arguments': ['--noinlinetls'],
    'external_dependencies': ['devkitA64/libnx', 'Horizon ICU 77.1 with data', 'Horizon zlib'],
    'files': {str(p): hashlib.sha256((repo/p).read_bytes()).hexdigest() for p in files},
}
output.mkdir(parents=True)
for relative in files:
    (output/relative).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(repo/relative, output/relative)
(output/'sdk-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
(output/'README.md').write_text(f'''# Experimental Horizon NativeAOT SDK

This package contains the source-built .NET {version} workstation runtime, native
BCL and version-matched Horizon SDK assemblies. The .NET 9 package also
contains the separately built Horizon socket assembly. It contains
no commercial application inputs, ICU data, or toolchain. The manifest records
the target contract and file inventory. This is a subset, not a complete SDK.

From this package's root, verify its contents with:

```sh
python3 src/coreclr/nativeaot/Runtime/libnx/validate-sdk.py .
```

Consumers may use this directory where a built runtime root was required.
Use ILC {version}, the matching SDK DLLs together, --noinlinetls, the included Horizon
native archives and linker-script generator. Obtain the devkitA64 toolchain,
libnx and Switch zlib through [devkitPro](https://devkitpro.org/wiki/Getting_Started).
Build Horizon ICU 77.1 with the
[mono-nx ICU recipe](https://github.com/pixelomer/mono-nx/blob/main/icu/build_icu.sh).
These dependencies are not packaged here.

Link Horizon libstdc++ for ICU. Initialize ICU data before managed entry
and retain that storage, runtime and services until process exit. Use full
application memory and libnx 4.10.0+ on Horizon 21+.

System.Net.Sockets requires explicit replacement of the official Unix ILC
reference; copying it into aotsdk does not perform that replacement. BSD buffer
capacity is application-owned. The synchronous IPv4 probe configures
sb_efficiency=8. It does not cover async APIs or all BCL functionality.

The directory layout preserves existing build consumers. source_revision
identifies the source checkout used for packaging, not a reproducibility proof.
The package has no Git metadata. Keep both runtime license files with releases.
''')
print(output)
