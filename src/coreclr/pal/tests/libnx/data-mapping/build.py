#!/usr/bin/env python3
"""Cross-build the original native mapping-budget probe; no device action."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--libnx', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
sdk, out = a.libnx.resolve(), a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
source = Path(__file__).with_name('main.c')
target = out / 'data-mapping'
cmd = ['aarch64-none-elf-gcc', '-O2', '-g', '-march=armv8-a+crc+crypto',
       '-mtune=cortex-a57', '-mtp=soft', '-fPIE', '-D__SWITCH__',
       '-I' + str(sdk / 'include'), '-specs=' + str(sdk / 'switch.specs'),
       '-Wl,-Map,' + str(target.with_suffix('.map')), str(source),
       '-L' + str(sdk / 'lib'), '-lnx', '-o', str(target.with_suffix('.elf'))]
subprocess.run(cmd, check=True)
subprocess.run(['nacptool', '--create', 'Horizon data mapping probe', 'Homebrew runtime research',
                '1.0.0', str(target.with_suffix('.nacp'))], check=True)
subprocess.run(['elf2nro', str(target.with_suffix('.elf')), str(target.with_suffix('.nro')),
                '--nacp=' + str(target.with_suffix('.nacp'))], check=True)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
(out / 'manifest.json').write_text(json.dumps(dict(command=cmd, source_sha256=sha(source),
    libnx_sha256=sha(sdk / 'lib/libnx.a'), nro_sha256=sha(target.with_suffix('.nro'))), indent=2) + '\n')
print(target.with_suffix('.nro'))
