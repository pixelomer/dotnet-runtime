"""Pinned source fetching for the Horizon build scripts (Python standard library)."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request


def run(command, cwd=None, env=None):
    command = list(map(str, command))
    print('+ ' + ' '.join(command), flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def git_source(spec, target, mirrors=None):
    """Never reset an existing checkout. Mirrors supply Git objects, not builds."""
    target = Path(target).resolve()
    revision = spec['revision']
    if len(revision) != 40 or any(c not in '0123456789abcdef' for c in revision):
        raise ValueError('Source revisions must be full commit IDs')
    source = (mirrors or {}).get(spec['url'], spec['url'])
    if not target.exists():
        target.mkdir(parents=True)
        run(['git', 'init', str(target)])
        run(['git', '-C', target, 'remote', 'add', 'origin', spec['url']])
    if not (target / '.git').exists():
        raise RuntimeError('Source destination is not a Git checkout: ' + str(target))
    status = subprocess.check_output(['git', '-C', target, 'status', '--porcelain'], text=True)
    if status:
        raise RuntimeError('Refusing to change dirty dependency: ' + str(target))
    head = subprocess.run(['git', '-C', target, 'rev-parse', '--verify', 'HEAD'], capture_output=True, text=True)
    if head.returncode == 0 and head.stdout.strip() != revision:
        raise RuntimeError('Dependency revision changed; choose a fresh build directory: ' + str(target))
    if head.returncode:
        run(['git', '-C', target, 'fetch', '--depth=1', source, revision])
        run(['git', '-C', target, 'checkout', '--detach', revision])
    return target


def download(spec, target):
    target = Path(target)
    if target.exists() and digest(target) == spec['sha256']:
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_suffix(target.suffix + '.part')
    with urllib.request.urlopen(spec['url'], timeout=120) as response, partial.open('wb') as output:
        while block := response.read(1024 * 1024):
            output.write(block)
    if digest(partial) != spec['sha256']:
        raise RuntimeError('Download hash mismatch: ' + spec['url'])
    partial.replace(target)
    return target


def extract_tar(archive, destination):
    with tarfile.open(archive) as source:
        # Python's data filter rejects traversal and links outside the destination.
        source.extractall(destination, filter='data')


def read_mirrors(path):
    if path is None:
        return {}
    value = json.loads(Path(path).read_text())
    if not isinstance(value, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in value.items()):
        raise ValueError('Mirrors must map canonical Git URLs to Git source URLs/paths')
    return value
