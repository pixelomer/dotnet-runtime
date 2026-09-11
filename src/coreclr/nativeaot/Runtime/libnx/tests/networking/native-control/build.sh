#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(git -C "$here" rev-parse --show-toplevel)
out="$repo/artifacts/libnx-native-networking-control"
: "${DEVKITPRO:?Set DEVKITPRO}"
mkdir -p "$out"
"$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc" \
 -g -O2 -Wall -Wextra -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE \
 -D__SWITCH__ -DSOCKET_EFFICIENCY="${SOCKET_EFFICIENCY:-4}" -I"$DEVKITPRO/libnx/include" "$here/main.c" \
 -specs="$DEVKITPRO/libnx/switch.specs" -L"$DEVKITPRO/libnx/lib" -lnx \
 -Wl,-Map,"$out/native-networking-control.map" -o "$out/native-networking-control.elf"
"$DEVKITPRO/tools/bin/nacptool" --create 'Native Networking Control' 'Runtime Probe' '0.1.0' "$out/control.nacp"
"$DEVKITPRO/tools/bin/elf2nro" "$out/native-networking-control.elf" "$out/native-networking-control.nro" --nacp="$out/control.nacp"
sha256sum "$here/main.c" "$here/build.sh" "$out/native-networking-control.nro" "$DEVKITPRO/libnx/lib/libnx.a" > "$out/SHA256SUMS"
printf '%s\n' "$out/native-networking-control.nro"
