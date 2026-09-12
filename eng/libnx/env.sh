#!/usr/bin/env bash
# Source this file after eng/libnx/build.py completes.
_horizon_here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
_horizon_config="$_horizon_here/../../artifacts/horizon/environment.json"
if [[ ! -f "$_horizon_config" ]]; then
    echo "Run python3 eng/libnx/build.py for this profile first." >&2
    return 1
fi
eval "$(python3 - "$_horizon_config" <<'PYCODE'
import json, shlex, sys
for key, value in json.load(open(sys.argv[1])).items():
    if key not in {'DEVKITPRO', 'DEVKITA64', 'ROOTFS_DIR', 'ICU_NX_INSTALL_DIR'}:
        raise SystemExit('Unexpected build environment key')
    print('export ' + key + '=' + shlex.quote(value))
PYCODE
)"
export PATH="$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH"
unset _horizon_here _horizon_config
