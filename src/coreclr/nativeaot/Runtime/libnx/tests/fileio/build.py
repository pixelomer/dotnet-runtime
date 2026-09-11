#!/usr/bin/env python3
"""Build a native test of the actual System.Native positional file adapter."""
from pathlib import Path
import hashlib,json,os,subprocess
here=Path(__file__).resolve().parent
repo=here.parents[6]
out=repo/'artifacts/libnx-fileio-test'
out.mkdir(parents=True,exist_ok=True)
dkp=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))
archive=repo/'artifacts/bin/native/net9.0-libnx-Release-arm64/libSystem.Native.a'
elf=out/'nativeaot-fileio-test.elf';nro=out/'nativeaot-fileio-test.nro'
cmd=[str(dkp/'devkitA64/bin/aarch64-none-elf-gcc'),'-g','-O2','-D__SWITCH__','-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE','-ffunction-sections','-fdata-sections','-I'+str(dkp/'libnx/include'),str(here/'main.c'),str(archive),'-specs='+str(dkp/'libnx/switch.specs'),'-L'+str(dkp/'libnx/lib'),'-lnx','-lm','-Wl,-Map,'+str(out/'fileio.map'),'-o',str(elf)]
(out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
subprocess.run(cmd,check=True)
subprocess.run([str(dkp/'tools/bin/elf2nro'),str(elf),str(nro)],check=True)
(out/'manifest.json').write_text(json.dumps({'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),'sha256':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (archive,nro,here/'main.c',repo/'src/native/libs/System.Native/pal_io.c',repo/'src/native/libs/System.Native/pal_io_libnx.h')}},indent=2)+'\n')
print(nro)
