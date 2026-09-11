#!/usr/bin/env python3
"""Link the actual shared Horizon GC OS adapter with native ownership tests.

Requires devkitPro at /opt/devkitpro. Follow the toolchain/ICU, pinned libnx
and cross-configuration procedure in src/coreclr/pal/tests/libnx/threads/README.md.
From the runtime root, build native inputs before invoking this script:

    cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \\
      --target gc_pal coreclrpal_objects -- -j6
    python3 src/coreclr/pal/tests/libnx/gc-os/build.py

The staged libnx SDK is selected through LIBNX_ROOT in CMakeCache.
"""
from pathlib import Path
import shlex, subprocess
here = Path(__file__).resolve().parent
repo = here.parents[5]
build = repo / 'artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe'
flags = {}
for line in (build / 'gc/unix/CMakeFiles/gc_pal.dir/flags.make').read_text().splitlines():
    if ' = ' in line:
        key, value = line.split(' = ', 1); flags[key] = shlex.split(value)
dkp = Path('/opt/devkitpro')
cc = dkp / 'devkitA64/bin/aarch64-none-elf-g++'
out = repo / 'artifacts/libnx-gc-os'; out.mkdir(exist_ok=True)
common = repo / 'src/native/libs/Common'
obj = out / 'main.o'
subprocess.run([str(cc), *flags['CXX_DEFINES'], *flags['CXX_INCLUDES'], *flags['CXX_FLAGS'], '-I'+str(common), '-c',str(here/'main.cpp'),'-o',str(obj)],check=True)
libnx = dkp / 'libnx'
for line in (build/'CMakeCache.txt').read_text().splitlines():
    if line.startswith('LIBNX_ROOT:PATH='): libnx = Path(line.split('=',1)[1])
target = out/'coreclr-gc-os-probe'
objects = [obj, build/'gc/unix/CMakeFiles/gc_pal.dir/__/libnx/gcenv.libnx.cpp.obj']
objects += list((build/'pal/src/CMakeFiles/coreclrpal_objects.dir').rglob('nxvm.c.obj'))
assert len(objects) == 3 and all(p.is_file() for p in objects)
subprocess.run([str(cc),'-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE','-g','-specs='+str(libnx/'switch.specs'),'-Wl,--gc-sections,-Map,'+str(target.with_suffix('.map')),*map(str,objects),'-L'+str(libnx/'lib'),'-lnx','-o',str(target.with_suffix('.elf'))],check=True)
subprocess.run([str(dkp/'tools/bin/nacptool'),'--create','Shared GC OS boundary','Runtime research','1.0.0',str(target.with_suffix('.nacp'))],check=True)
subprocess.run([str(dkp/'tools/bin/elf2nro'),str(target.with_suffix('.elf')),str(target.with_suffix('.nro')),'--nacp='+str(target.with_suffix('.nacp'))],check=True)
print(target.with_suffix('.nro'))
