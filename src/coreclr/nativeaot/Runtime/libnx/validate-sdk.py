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
    data.get('runtime_version') != '9.0.3' or data.get('ilc_version') != '9.0.3' or
    data.get('gc') != 'workstation' or '--noinlinetls' not in data.get('required_ilc_arguments', [])):
    raise SystemExit('Unsupported SDK contract')
revision = data.get('source_revision', '')
if not re.fullmatch('[0-9a-f]{40}', revision):
    raise SystemExit('Invalid source revision')
files = data.get('files')
if not isinstance(files, dict) or not files:
    raise SystemExit('SDK has no file inventory')
for relative, digest in files.items():
    path = root/relative
    if (Path(relative).is_absolute() or '..' in Path(relative).parts or
        not path.resolve().is_relative_to(root) or not path.is_file()):
        raise SystemExit('Invalid SDK file path: '+relative)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit('SDK hash mismatch: '+relative)
print(revision)
