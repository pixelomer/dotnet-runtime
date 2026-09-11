#!/usr/bin/env python3
"""Build the Horizon managed networking object, then link only Horizon native libraries."""
from pathlib import Path
import os,subprocess,re,shutil,json,hashlib,argparse
import xml.etree.ElementTree as ET
here=Path(__file__).resolve().parent
repo=here.parents[6]
out=repo/'artifacts/libnx-networking-test'
out.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--unix-sockets-control',action='store_true',help='Use the official Unix socket assembly to reproduce ENOSYS')
args=parser.parse_args()
# Keep standalone SDK compilation separate from the runtime repository's Arcade build.
for name in ('Directory.Build.props','Directory.Build.targets'):
    (out/name).write_text('<Project />\n')
(out/'global.json').write_text(json.dumps({'sdk':{'version':'10.0.111','rollForward':'disable'}})+'\n')
dkp=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))
icu=Path(os.environ['ICU_NX_INSTALL_DIR'])
runtime_sdk=repo/'artifacts/bin/coreclr/libnx.arm64.Release/aotsdk'
sdk=out/'sdk'
if sdk.exists():shutil.rmtree(sdk)
shutil.copytree(runtime_sdk,sdk)
sockets=repo/'artifacts/bin/System.Net.Sockets/Release/net9.0-libnx/System.Net.Sockets.dll'
if not args.unix_sockets_control:
    if not sockets.is_file():raise SystemExit('Build the source Horizon System.Net.Sockets assembly first')
    shutil.copy2(sockets,sdk/'System.Net.Sockets.dll')
libs=repo/'artifacts/bin/native/net9.0-libnx-Release-arm64'
(out/'Networking.csproj').write_text('''<Project Sdk="Microsoft.NET.Sdk">
<PropertyGroup>
<TargetFramework>net9.0</TargetFramework><OutputType>Library</OutputType>
<PublishAot>true</PublishAot><NativeLib>Static</NativeLib><AllowUnsafeBlocks>true</AllowUnsafeBlocks>
<Nullable>enable</Nullable><IlcPackageVersion>9.0.3</IlcPackageVersion><RuntimeFrameworkVersion>9.0.3</RuntimeFrameworkVersion>
<IlcOptimizationPreference>Speed</IlcOptimizationPreference><StripSymbols>false</StripSymbols><TrimmerSingleWarn>false</TrimmerSingleWarn>
</PropertyGroup><ItemGroup><IlcArg Include="--noinlinetls"/><DirectPInvoke Include="__Internal"/></ItemGroup>
</Project>''')
if not args.unix_sockets_control:
    project=out/'Networking.csproj'
    tree=ET.parse(project)
    target=ET.SubElement(tree.getroot(),'Target',Name='SelectHorizonSockets',BeforeTargets='WriteIlcRspFileForCompilation',DependsOnTargets='ComputeIlcCompileInputs')
    group=ET.SubElement(target,'ItemGroup')
    ET.SubElement(group,'IlcReference',Remove='@(IlcReference)',Condition="'%(IlcReference.Filename)' == 'System.Net.Sockets'")
    ET.SubElement(group,'IlcReference',Include=str(sdk/'System.Net.Sockets.dll'))
    tree.write(project,encoding='unicode')
shutil.copy2(here/'Networking.cs',out/'Networking.cs')
with (out/'publish.log').open('w') as log:
    subprocess.run(['dotnet','publish',str(out/'Networking.csproj'),'-c','Release','-r','linux-arm64','--self-contained','-p:IlcSdkPath='+str(sdk)+'/'],cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
if re.search(r'warning IL\d+', (out/'publish.log').read_text()):raise SystemExit('AOT warnings remain')
obj=out/'obj/Release/net9.0/linux-arm64/native/Networking.o'
if not args.unix_sockets_control:
    response=obj.with_suffix('.ilc.rsp').read_text().splitlines()
    refs=[line for line in response if line.startswith('-r:') and line.endswith('/System.Net.Sockets.dll')]
    if refs != ['-r:'+str(sdk/'System.Net.Sockets.dll')]:raise SystemExit('Horizon socket assembly was not selected exclusively')
asm=subprocess.check_output(['aarch64-none-elf-objdump','-dr',str(obj)],text=True)
if re.search(r'\btpidr_el0\b|R_AARCH64_TLS',asm,re.I):raise SystemExit('Linux TLS code remains')
subprocess.run(['python3',str(here.parent.parent/'create-linker-script.py'),str(out/'switch.ld')],check=True)
flags=['-g','-O2','-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE','-ffunction-sections','-fdata-sections','-fno-rtti','-fno-exceptions','-D__SWITCH__','-I'+str(dkp/'libnx/include')]
# Replace only the script reference; switch.specs always injects its own -T.
specs=(dkp/'libnx/switch.specs').read_text()
anchor='-T %:getenv(DEVKITPRO /libnx/switch.ld)'
if specs.count(anchor)!=1:raise SystemExit('Unsupported switch.specs layout')
(out/'switch.specs').write_text(specs.replace(anchor,'-T '+str(out/'switch.ld')))
cmd=[str(dkp/'devkitA64/bin/aarch64-none-elf-g++'),*flags,str(here/'main.cpp'),str(obj),str(sdk/'libbootstrapperdll.o'),'-specs='+str(out/'switch.specs'),'-Wl,--eh-frame-hdr,-Map,'+str(out/'networking.map'),'-Wl,--start-group',str(sdk/'libRuntime.WorkstationGC.a'),str(sdk/'libeventpipe-disabled.a'),str(sdk/'libstandalonegc-disabled.a'),str(libs/'libSystem.Native.a'),str(libs/'libSystem.Globalization.Native.a'),str(libs/'libSystem.IO.Compression.Native.a'),str(icu/'lib/libicui18n.a'),str(icu/'lib/libicuuc.a'),str(icu/'lib/libicudata.a'),'-L'+str(dkp/'portlibs/switch/lib'),'-L'+str(dkp/'libnx/lib'),'-lz','-lnx','-Wl,--end-group','-o',str(out/'nativeaot-networking-test.elf')]
(out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
with (out/'link.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
subprocess.run([str(dkp/'tools/bin/elf2nro'),str(out/'nativeaot-networking-test.elf'),str(out/'nativeaot-networking-test.nro')],check=True)
inputs=[*sorted(sdk.glob('*.dll')),obj,sdk/'libRuntime.WorkstationGC.a',sdk/'libbootstrapperdll.o',
        sdk/'libeventpipe-disabled.a',sdk/'libstandalonegc-disabled.a',
        *sorted(libs.glob('*.a')),out/'nativeaot-networking-test.nro',out/'switch.ld',out/'switch.specs']
manifest={
    'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
    'ilc_version':'9.0.3', 'managed_sdk':'10.0.111',
    'horizon_socket_assembly':not args.unix_sockets_control,
    'compiler':subprocess.check_output([str(dkp/'devkitA64/bin/aarch64-none-elf-g++'),'--version'],text=True).splitlines()[0],
    'inputs':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
    'source_sha256':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest()
        for p in [here/'build.py',here/'main.cpp',here/'Networking.cs',here.parent.parent/'create-linker-script.py']},
    'managed_linux_tls_instructions_or_relocations':0,
    'managed_il_warnings':0,
}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out/'nativeaot-networking-test.nro')
