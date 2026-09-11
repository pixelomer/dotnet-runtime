#!/usr/bin/env python3
"""Build a native test of the actual System.Native positional file adapter."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
here=Path(__file__).resolve().parent
repo=here.parents[6]
out=repo/'artifacts/libnx-fileio-test'
out.mkdir(parents=True,exist_ok=True)
dkp=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))
parser=argparse.ArgumentParser()
parser.add_argument('--archive', type=Path, default=repo/'artifacts/bin/native/net9.0-libnx-Release-arm64/libSystem.Native.a')
parser.add_argument('--libnx-root', type=Path, default=dkp/'libnx')
args=parser.parse_args()
archive=args.archive.resolve()
sdk=args.libnx_root.resolve()
elf=out/'nativeaot-fileio-test.elf';nro=out/'nativeaot-fileio-test.nro'
cmd=[str(dkp/'devkitA64/bin/aarch64-none-elf-gcc'),'-g','-O2','-D__SWITCH__','-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE','-ffunction-sections','-fdata-sections','-I'+str(sdk/'include'),str(here/'main.c'),str(archive),'-specs='+str(sdk/'switch.specs'),'-L'+str(sdk/'lib'),'-lnx','-lm','-Wl,-Map,'+str(out/'fileio.map'),'-o',str(elf)]
(out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
subprocess.run(cmd,check=True)
subprocess.run([str(dkp/'tools/bin/elf2nro'),str(elf),str(nro)],check=True)
(out/'manifest.json').write_text(json.dumps({'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),'libnx_root':str(sdk),'sha256':{str(p.relative_to(repo)) if p.is_relative_to(repo) else str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (archive,nro,here/'main.c',here/'build.py',sdk/'lib/libnx.a',repo/'src/native/libs/System.Native/pal_io.c',repo/'src/native/libs/System.Native/pal_io_libnx.h')}},indent=2)+'\n')
print(nro)
