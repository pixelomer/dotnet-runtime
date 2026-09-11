#!/usr/bin/env python3
"""Build the managed stress object, then link only Horizon native libraries."""
from pathlib import Path
import os,subprocess,re,shutil,json,hashlib
here=Path(__file__).resolve().parent
repo=here.parents[6]
out=repo/'artifacts/libnx-managed-stress'
out.mkdir(parents=True,exist_ok=True)
# Keep standalone SDK compilation separate from the runtime repository's Arcade build.
for name in ('Directory.Build.props','Directory.Build.targets'):
    (out/name).write_text('<Project />\n')
(out/'global.json').write_text(json.dumps({'sdk':{'version':'10.0.111','rollForward':'disable'}})+'\n')
dkp=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))
icu=Path(os.environ['ICU_NX_INSTALL_DIR'])
sdk=repo/'artifacts/bin/coreclr/libnx.arm64.Release/aotsdk'
libs=repo/'artifacts/bin/native/net10.0-libnx-Release-arm64'
if not (sdk/'System.Private.CoreLib.dll').is_file():
    raise SystemExit('Build clr.nativeaotlibs for Horizon first; see MANAGED_LIBRARIES.md')
(out/'Stress.csproj').write_text('''<Project Sdk="Microsoft.NET.Sdk">
<PropertyGroup>
<TargetFramework>net10.0</TargetFramework><OutputType>Library</OutputType>
<PublishAot>true</PublishAot><NativeLib>Static</NativeLib><AllowUnsafeBlocks>true</AllowUnsafeBlocks>
<Nullable>enable</Nullable><IlcPackageVersion>10.0.12</IlcPackageVersion><RuntimeFrameworkVersion>10.0.12</RuntimeFrameworkVersion>
<IlcOptimizationPreference>Speed</IlcOptimizationPreference><StripSymbols>false</StripSymbols><TrimmerSingleWarn>false</TrimmerSingleWarn>
</PropertyGroup><ItemGroup><IlcArg Include="--noinlinetls"/><DirectPInvoke Include="__Internal"/></ItemGroup>
</Project>''')
shutil.copy2(here/'Stress.cs',out/'Stress.cs')
with (out/'publish.log').open('w') as log:
    subprocess.run(['dotnet','publish',str(out/'Stress.csproj'),'-c','Release','-r','linux-arm64','--self-contained','-p:IlcSdkPath='+str(sdk)+'/'],cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
if re.search(r'warning IL\d+', (out/'publish.log').read_text()):raise SystemExit('AOT warnings remain')
obj=out/'obj/Release/net10.0/linux-arm64/native/Stress.o'
asm=subprocess.check_output(['aarch64-none-elf-objdump','-dr',str(obj)],text=True)
if re.search(r'\btpidr_el0\b|R_AARCH64_TLS',asm,re.I):raise SystemExit('Linux TLS code remains')
subprocess.run(['python3',str(here.parent.parent/'create-linker-script.py'),str(out/'switch.ld')],check=True)
flags=['-g','-O2','-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE','-ffunction-sections','-fdata-sections','-fno-rtti','-fno-exceptions','-D__SWITCH__','-I'+str(dkp/'libnx/include')]
# Replace only the script reference; switch.specs always injects its own -T.
specs=(dkp/'libnx/switch.specs').read_text()
anchor='-T %:getenv(DEVKITPRO /libnx/switch.ld)'
if specs.count(anchor)!=1:raise SystemExit('Unsupported switch.specs layout')
(out/'switch.specs').write_text(specs.replace(anchor,'-T '+str(out/'switch.ld')))
cmd=[str(dkp/'devkitA64/bin/aarch64-none-elf-g++'),*flags,str(here/'main.cpp'),str(obj),str(sdk/'libbootstrapperdll.o'),'-specs='+str(out/'switch.specs'),'-Wl,--eh-frame-hdr,-Map,'+str(out/'stress.map'),'-Wl,--start-group',str(sdk/'libRuntime.WorkstationGC.a'),str(sdk/'libaotminipal.a'),str(sdk/'libeventpipe-disabled.a'),str(sdk/'libstandalonegc-disabled.a'),str(libs/'libSystem.Native.a'),str(libs/'libSystem.Globalization.Native.a'),str(libs/'libSystem.IO.Compression.Native.a'),str(icu/'lib/libicui18n.a'),str(icu/'lib/libicuuc.a'),str(icu/'lib/libicudata.a'),'-L'+str(dkp/'portlibs/switch/lib'),'-L'+str(dkp/'libnx/lib'),'-lz','-lnx','-Wl,--end-group','-o',str(out/'nativeaot10-managed-stress.elf')]
(out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
with (out/'link.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
subprocess.run([str(dkp/'tools/bin/elf2nro'),str(out/'nativeaot10-managed-stress.elf'),str(out/'nativeaot10-managed-stress.nro')],check=True)
inputs=[*sorted(sdk.glob('*.dll')),obj,sdk/'libRuntime.WorkstationGC.a',sdk/'libbootstrapperdll.o',
        sdk/'libeventpipe-disabled.a',sdk/'libstandalonegc-disabled.a',sdk/'libaotminipal.a',
        *sorted(libs.glob('*.a')),out/'nativeaot10-managed-stress.nro',out/'switch.ld',out/'switch.specs']
manifest={
    'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
    'ilc_version':'10.0.12', 'managed_sdk':'10.0.111',
    'compiler':subprocess.check_output([str(dkp/'devkitA64/bin/aarch64-none-elf-g++'),'--version'],text=True).splitlines()[0],
    'inputs':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
    'source_sha256':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest()
        for p in [here/'build.py',here/'main.cpp',here/'Stress.cs',here.parent.parent/'create-linker-script.py']},
    'managed_linux_tls_instructions_or_relocations':0,
    'managed_il_warnings':0,
}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out/'nativeaot10-managed-stress.nro')
