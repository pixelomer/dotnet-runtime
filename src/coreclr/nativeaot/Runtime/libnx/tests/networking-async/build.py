#!/usr/bin/env python3
"""Compile the same async socket scenarios with stable .NET 10 NativeAOT."""
import argparse, hashlib, json, os, re, shutil, subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
here = Path(__file__).resolve().parent
repo = here.parents[6]
parser = argparse.ArgumentParser()
parser.add_argument('--sdk', type=Path, required=True, help='Source-built Horizon .NET 10 NativeAOT SDK (see README.md)')
parser.add_argument('--sockets', type=Path, required=True, help='Exact Horizon System.Net.Sockets.dll to test')
parser.add_argument('--libnx-root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
a = parser.parse_args()
sdk, sockets, libnx, out = (p.resolve() for p in (a.sdk, a.sockets, a.libnx_root, a.output))
out.mkdir(parents=True, exist_ok=True)
for name in ('Directory.Build.props', 'Directory.Build.targets'):
    (out/name).write_text('<Project />\n')
(out/'global.json').write_text(json.dumps({'sdk': {'version': '10.0.111', 'rollForward': 'disable'}})+'\n')
project = ET.fromstring('''<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>
<TargetFramework>net10.0</TargetFramework><OutputType>Library</OutputType><PublishAot>true</PublishAot>
<NativeLib>Static</NativeLib><AllowUnsafeBlocks>true</AllowUnsafeBlocks><InvariantGlobalization>true</InvariantGlobalization>
<IlcPackageVersion>10.0.12</IlcPackageVersion><RuntimeFrameworkVersion>10.0.12</RuntimeFrameworkVersion>
<IlcOptimizationPreference>Speed</IlcOptimizationPreference><StripSymbols>false</StripSymbols><TrimmerSingleWarn>false</TrimmerSingleWarn>
</PropertyGroup><ItemGroup><IlcArg Include="--noinlinetls"/><DirectPInvoke Include="__Internal"/></ItemGroup></Project>''')
target = ET.SubElement(project, 'Target', Name='SelectHorizonSockets', BeforeTargets='WriteIlcRspFileForCompilation', DependsOnTargets='ComputeIlcCompileInputs')
group = ET.SubElement(target, 'ItemGroup')
ET.SubElement(group, 'IlcReference', Remove='@(IlcReference)', Condition="'%(IlcReference.Filename)' == 'System.Net.Sockets'")
ET.SubElement(group, 'IlcReference', Include=str(sockets))
ET.ElementTree(project).write(out/'AsyncSockets.csproj', encoding='unicode')
shared = repo/'src/coreclr/pal/tests/libnx/host/SocketProbe.cs'
for src in (shared, here/'Entry.cs'): shutil.copyfile(src, out/src.name)
with (out/'publish.log').open('w') as log:
    subprocess.run(['dotnet', 'publish', 'AsyncSockets.csproj', '-c', 'Release', '-r', 'linux-arm64', '--self-contained', '-p:IlcSdkPath='+str(sdk)+'/'], cwd=out, stdout=log, stderr=subprocess.STDOUT, check=True)
if re.search(r'warning IL\d+', (out/'publish.log').read_text()): raise SystemExit('AOT warnings remain')
obj = out/'obj/Release/net10.0/linux-arm64/native/AsyncSockets.o'
refs = [l for l in obj.with_suffix('.ilc.rsp').read_text().splitlines() if l.startswith('-r:') and l.endswith('/System.Net.Sockets.dll')]
if refs != ['-r:'+str(sockets)]: raise SystemExit('Wrong socket assembly selected')
dkp = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
asm = subprocess.check_output([str(dkp/'devkitA64/bin/aarch64-none-elf-objdump'), '-dr', str(obj)], text=True)
if re.search(r'\btpidr_el0\b|R_AARCH64_TLS', asm, re.I): raise SystemExit('Linux inline TLS remains')
build = repo/'artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe'
icu = Path(os.environ['ICU_NX_INSTALL_DIR'])
subprocess.run(['python3', str(here.parent.parent/'create-linker-script.py'), str(out/'switch.ld')], check=True)
specs = (libnx/'switch.specs').read_text()
anchor = '-T %:getenv(DEVKITPRO /libnx/switch.ld)'
if specs.count(anchor) != 1: raise SystemExit('Unsupported switch.specs')
(out/'switch.specs').write_text(specs.replace(anchor, '-T '+str(out/'switch.ld')))
archives = [build/'nativeaot/Runtime/Full/libRuntime.WorkstationGC.a', sdk/'libaotminipal.a', sdk/'libeventpipe-disabled.a', sdk/'libstandalonegc-disabled.a']
archives += [build/'libs-native'/name/('lib'+name+'.a') for name in ('System.Native', 'System.Globalization.Native', 'System.IO.Compression.Native')]
archives += [icu/'lib'/name for name in ('libicui18n.a', 'libicuuc.a', 'libicudata.a')]
archives += [build/'_deps/fetchzlibng-build/libz.a']
archives += [build/'_deps/brotli-build'/name for name in ('libbrotlienc.a', 'libbrotlidec.a', 'libbrotlicommon.a')]
elf, nro = out/'nativeaot-async-sockets.elf', out/'nativeaot-async-sockets.nro'
cmd = [str(dkp/'devkitA64/bin/aarch64-none-elf-g++'), '-g', '-O2', '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIE', '-ffunction-sections', '-fdata-sections', '-fno-rtti', '-fno-exceptions', '-D__SWITCH__', '-I'+str(libnx/'include'), str(here/'main.cpp'), str(obj), str(sdk/'libbootstrapperdll.o'), '-specs='+str(out/'switch.specs'), '-Wl,--eh-frame-hdr,-Map,'+str(out/'sockets.map'), '-Wl,--start-group', *map(str, archives), '-L'+str(libnx/'lib'), '-lnx', '-Wl,--end-group', '-o', str(elf)]
(out/'link-command.json').write_text(json.dumps(cmd, indent=2)+'\n')
with (out/'link.log').open('w') as log: subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
subprocess.run([str(dkp/'tools/bin/elf2nro'), str(elf), str(nro)], check=True)
inputs = [sockets, shared, here/'Entry.cs', here/'main.cpp', here/'build.py', obj, sdk/'libbootstrapperdll.o', *archives, *sorted(sdk.glob('*.dll')), libnx/'lib/libnx.a', nro]
(out/'manifest.json').write_text(json.dumps({'runtime_base': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(), 'ilc_version': '10.0.12', 'sdk': str(sdk), 'inline_linux_tls': False, 'il_warnings': False, 'sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}, indent=2)+'\n')
print(nro)
