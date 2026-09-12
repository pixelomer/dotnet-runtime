#!/usr/bin/env python3
"""Validate a packaged Horizon SDK and print its source revision."""
import argparse
import hashlib
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('sdk', type=Path)
args = parser.parse_args()
root = args.sdk.resolve()
data = json.loads((root/'sdk-manifest.json').read_text())
if (data.get('format') != 1 or data.get('target') != 'libnx-arm64' or
    data.get('runtime_version') not in ('9.0.3', '10.0.12') or
    data.get('ilc_version') != data.get('runtime_version') or
    data.get('gc') != 'workstation' or '--noinlinetls' not in data.get('required_ilc_arguments', [])):
    raise SystemExit('Unsupported SDK contract')
revision = data.get('source_revision', '')
if not re.fullmatch('[0-9a-f]{40}', revision):
    raise SystemExit('Invalid source revision')
files = data.get('files')
if not isinstance(files, dict) or not files:
    raise SystemExit('SDK has no file inventory')
sdk = 'artifacts/bin/coreclr/libnx.arm64.Release/aotsdk/'
major = data['runtime_version'].split('.')[0]
required = [sdk + 'System.Private.' + n + '.dll' for n in
            ('CoreLib', 'DisabledReflection', 'Reflection.Execution', 'StackTraceMetadata', 'TypeLoader')]
required += [sdk + n for n in ('libRuntime.WorkstationGC.a', 'libbootstrapperdll.o', 'libeventpipe-disabled.a', 'libstandalonegc-disabled.a')]
required += [f'artifacts/bin/native/net{major}.0-libnx-Release-arm64/' + n for n in
             ('libSystem.Native.a', 'libSystem.Globalization.Native.a', 'libSystem.IO.Compression.Native.a')]
required += ['LICENSE.TXT', 'THIRD-PARTY-NOTICES.TXT']
if major == '10': required.append(sdk + 'libaotminipal.a')
if not set(required) <= files.keys():
    raise SystemExit('SDK inventory omits required runtime inputs')
for relative, digest in files.items():
    path = root/relative
    if (Path(relative).is_absolute() or '..' in Path(relative).parts or
        not path.resolve().is_relative_to(root) or not path.is_file()):
        raise SystemExit('Invalid SDK file path: '+relative)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit('SDK hash mismatch: '+relative)
print(revision)
