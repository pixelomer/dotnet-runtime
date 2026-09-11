# Managed Horizon stress test

The workload exercises implicit null-reference and explicit exceptions with
finally blocks, managed threads in call-free loops during compacting collections,
weak-reference reclamation, ThreadStatic isolation, concurrent exception handlers
and finalizers. The GC pins roots reported in copied register contexts while
scanning real stack slots normally. This probe does not cover every safe-point,
unwind, thread-abort, server-GC or background-worker shutdown path.

## Build

Use the devkitPro and Horizon ICU source prerequisites described in the
[PAL probe guide](../pal/README.md). Export `ICU_NX_INSTALL_DIR` as the ICU
installation prefix, set `DEVKITA64=$DEVKITPRO/devkitA64`, and add
`$DEVKITA64/bin` to `PATH`. Install the Microsoft
.NET SDK version 10.0.111 for the standalone managed compilation; the runtime
source build uses its own pinned SDK through `build.sh`. Restore downloads the
pinned ILC and managed runtime 10.0.12 packages from NuGet.

Build the native subsets and matching managed SDK libraries from the runtime root:

```sh
ROOTFS_DIR="$DEVKITPRO" ./build.sh \
  -s clr.nativeaotruntime+libs.native -c Release --cross -a arm64 --os libnx \
  /p:NativeAotSupported=true /p:EnableTrimAnalyzer=false \
  -cmakeargs '-DFEATURE_EVENT_TRACE=OFF'
ROOTFS_DIR="$DEVKITPRO" ./build.sh -s clr.nativeaotlibs -c Release \
  --cross -a arm64 --os libnx /p:NativeAotSupported=true \
  /p:EnableTrimAnalyzer=false /p:PublicSign=true
python3 src/coreclr/nativeaot/Runtime/libnx/tests/managed/build.py
```

Local empty Directory.Build files isolate the standalone managed project from
Arcade. Its linux-arm64 target supplies only the managed object; all linked
native runtime/BCL archives target Horizon. The build rejects IL warnings and
Linux inline TLS in the managed object.

The generated linker script keeps managed/unbox/module/EH boundaries and gives
the read-only security cookie an isolated 4 KiB data page. The generated specs
replace libnx's default `-T` argument, preserving the other NRO link options.
ICU needs libstdc++; do not also link libstdc++compat.a.

Outputs are under `artifacts/libnx-managed-stress/`, including
`nativeaot10-managed-stress.nro`, its ELF and map. The program writes
`sdmc:/switch/nativeaot10-managed-stress.txt`; preserve any existing file before
running it. Use full application memory. No game data is required.

## Runtime lifetime

The allocator can alias heap backing only into the kernel stack region, so the
GC must use that region's capacity rather than the full ASLR range.

NativeAOT remains resident until process exit. The launcher sets
`__nx_applet_exit_mode=1` to request application exit to HOME. Do not return to
hbmenu or free backing still used by live GC threads as a substitute for runtime
unloading.

The shared native reaper joins completed runtime/BCL pthreads after TLS
destruction and kernel exit, releasing stacks and handles. Callbacks using this
helper must return normally; arbitrary `pthread_exit` callbacks are outside its
contract. One reaper remains for process lifetime.

Primary platform references:
[virtual-region selection](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/kernel/virtmem.c),
[pthread join/unsupported detach](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/runtime/newlib.c),
[application exit mode](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/services/applet.c).

The probe requires the four source-built Horizon aotsdk DLLs and passes their
directory to ILC with `IlcSdkPath`; see the [managed library guide](../../MANAGED_LIBRARIES.md).
The .NET 10 link includes `libaotminipal.a`; there is no separate
DisabledReflection SDK DLL. The shared allocator accounts for both BCL frozen
objects and GC mappings.
