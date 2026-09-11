#!/usr/bin/env python3
"""Link real PAL sections from the source-built archive; no runtime stubs."""
from pathlib import Path
import os, subprocess
here = Path(__file__).resolve().parent
repo = here.parents[6]
out = repo / 'artifacts/libnx-pal-test'
out.mkdir(parents=True, exist_ok=True)
dkp = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
archive = repo / 'artifacts/obj/coreclr/libnx.arm64.Release/nativeaot/Runtime/Full/libRuntime.WorkstationGC.a'
flags = ['-g', '-O2', '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIE', '-ffunction-sections', '-fdata-sections', '-fno-exceptions', '-fno-rtti', '-D__SWITCH__', '-I'+str(dkp/'libnx/include')]
subprocess.run([str(dkp/'devkitA64/bin/aarch64-none-elf-g++'), *flags, str(here/'main.cpp'), str(archive), '-specs='+str(dkp/'libnx/switch.specs'), '-L'+str(dkp/'libnx/lib'), '-lnx', '-Wl,--gc-sections,--eh-frame-hdr,-Map,'+str(out/'pal.map'), '-Wl,-T,'+str(here.parent.parent/'unwind-sections.ld'), '-o', str(out/'dotnet-pal-probe.elf')], check=True)
subprocess.run([str(dkp/'tools/bin/elf2nro'), str(out/'dotnet-pal-probe.elf'), str(out/'dotnet-pal-probe.nro')], check=True)
print(out/'dotnet-pal-probe.nro')
