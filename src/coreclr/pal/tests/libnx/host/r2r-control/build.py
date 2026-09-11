#!/usr/bin/env python3
"""Build a source-owned Linux/ARM64 ReadyToRun input for the Horizon loader probe.

Requires Microsoft .NET SDK 10.0.111, Python dnfile and restore access for
.NET 10.0.12. See the parent README.md for source-built host/framework inputs,
--platform-specific output selection and the explicit r2r probe exception.
The generated DLL is an unsupported-format control, not an application input.
"""
import argparse, hashlib, json, shutil, subprocess
from pathlib import Path
import dnfile
here = Path(__file__).resolve().parent
repo = here.parents[6]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--platform-specific', action='store_true', help='Keep the RID-inferred ARM64 assembly identity')
args = parser.parse_args()
out = repo/('artifacts/libnx-r2r-control-architecture' if args.platform_specific else 'artifacts/libnx-r2r-control')
out.mkdir(parents=True, exist_ok=True)
for name in ('Directory.Build.props', 'Directory.Build.targets'): (out/name).write_text('<Project />\n')
(out/'global.json').write_text(json.dumps({'sdk': {'version': '10.0.111', 'rollForward': 'disable'}})+'\n')
(out/'OwnedReadyToRun.csproj').write_text('''<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>
<TargetFramework>net10.0</TargetFramework><OutputType>Library</OutputType>
<PublishReadyToRun>true</PublishReadyToRun><RuntimeFrameworkVersion>10.0.12</RuntimeFrameworkVersion>
<PlatformTarget>AnyCPU</PlatformTarget><UseAppHost>false</UseAppHost><PublishTrimmed>false</PublishTrimmed>
</PropertyGroup></Project>'''.replace('<PlatformTarget>AnyCPU</PlatformTarget>', '' if args.platform_specific else '<PlatformTarget>AnyCPU</PlatformTarget>'))
shutil.copyfile(here/'OwnedReadyToRun.cs', out/'OwnedReadyToRun.cs')
with (out/'build.log').open('w') as log:
    subprocess.run(['dotnet', 'publish', 'OwnedReadyToRun.csproj', '-c', 'Release', '-r', 'linux-arm64', '--self-contained', 'false', '-o', str(out/'publish')], cwd=out, stdout=log, stderr=subprocess.STDOUT, check=True)
assembly = out/'publish/OwnedReadyToRun.dll'
pe = dnfile.dnPE(str(assembly))
header = pe.net.struct
rva, size = header.ManagedNativeHeaderRva, header.ManagedNativeHeaderSize
if not rva or size < 16 or pe.get_data(rva, 4) != b'RTR\x00': raise SystemExit('Input has no ReadyToRun native header')
(out/'manifest.json').write_text(json.dumps({'rid': 'linux-arm64', 'platform_neutral_source': not args.platform_specific, 'runtime_framework': '10.0.12', 'readytorun_rva': rva, 'readytorun_size': size, 'pe_machine': pe.FILE_HEADER.Machine, 'r2r_flags': int.from_bytes(pe.get_data(rva + 8, 4), 'little'), 'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (assembly, here/'OwnedReadyToRun.cs', here/'build.py')}}, indent=2)+'\n')
print(assembly)
