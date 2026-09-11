#!/usr/bin/env python3
"""Build the Horizon socket library using a fresh, targeted reference pack.

Requires the source-built NativeAOT CoreLib/SDK and normal repository bootstrap.
Does not require or invoke the broad libs.ref build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

here = Path(__file__).resolve().parent
repo = here.parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True,
                    help='New directory for the isolated reference pack and build logs')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
pack = out / 'refpack'
refs = pack / 'ref/net9.0'
refs.mkdir(parents=True)
project = repo / 'src/libraries/System.Net.Sockets/src/System.Net.Sockets.csproj'
common = ['-c', 'Release', '-p:TargetOS=libnx', '-p:TargetArchitecture=arm64',
          '-p:RuntimeFlavor=CoreCLR', '-p:UseNativeAotCoreLib=true',
          '-p:PublicSign=true', '-p:NativeAotSupported=true',
          '-p:EnableTrimAnalyzer=false', '-p:MicrosoftNetCoreAppRefPackDir='+str(pack)+'/',
          '-p:MicrosoftNetCoreAppRefPackRefDir='+str(refs)+'/']
names = sorted({node.attrib['Include'] for node in ET.parse(project).iter('Reference')
                if node.attrib['Include'] != 'System.Console'})
commands = []
for name in names:
    ref_project = repo / 'src/libraries' / name / 'ref' / (name+'.csproj')
    if not ref_project.is_file():
        raise SystemExit('Missing reference project: '+str(ref_project))
    # Rebuild forces binplacing into the new pack even if normal artifacts are warm.
    cmd = [str(repo/'dotnet.sh'), 'build', str(ref_project), *common, '-t:Rebuild']
    commands.append(cmd)
    with (out/(name+'.log')).open('w') as log:
        subprocess.run(cmd, cwd=repo, stdout=log, stderr=subprocess.STDOUT, check=True)
    print('Built reference '+name, flush=True)
missing = [name for name in names if not (refs/(name+'.dll')).is_file()]
if missing:
    raise SystemExit('Incomplete isolated references: '+', '.join(missing))
cmd = [str(repo/'dotnet.sh'), 'build', str(project), *common,
       '-p:TargetFramework=net9.0-libnx', '-t:Rebuild']
commands.append(cmd)
with (out/'sockets.log').open('w') as log:
    subprocess.run(cmd, cwd=repo, stdout=log, stderr=subprocess.STDOUT, check=True)
library = repo/'artifacts/bin/System.Net.Sockets/Release/net9.0-libnx/System.Net.Sockets.dll'
cmd = [str(repo/'dotnet.sh'), 'msbuild', str(project), *common[2:],
       '-p:Configuration=Release', '-p:TargetFramework=net9.0-libnx',
       '-t:ResolveReferences', '-getItem:ReferencePath']
commands.append(cmd)
resolved = subprocess.check_output(cmd, cwd=repo, text=True)
(out/'resolved-references.json').write_text(resolved)
paths = [Path(item['Identity']).resolve()
         for item in json.loads(resolved)['Items']['ReferencePath']]
if set(paths) != {refs/(name+'.dll') for name in names}:
    raise SystemExit('Compilation did not resolve exactly the isolated reference set')
manifest = {
    'commands': commands,
    'references': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(refs.glob('*.dll'))},
    'library': str(library.relative_to(repo)),
    'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest(),
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
    'recipe_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
}
(out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(library)
