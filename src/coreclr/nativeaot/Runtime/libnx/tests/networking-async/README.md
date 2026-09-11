# NativeAOT async socket probe

This probe compiles the same SocketProbe.cs as the
[CoreCLR host](../../../../../pal/tests/libnx/host/README.md) with .NET 10.0.12
NativeAOT. It exercises loopback backpressure, async accept/connect/send/receive,
cancellation and close/descriptor reuse.

## Source-built prerequisites

Use the devkitPro toolchain and source-built ICU prerequisites in the
[managed probe guide](../managed/README.md). Install its specified .NET SDK
10.0.111 and run its two runtime/library build commands from this checkout
to produce the Horizon NativeAOT SDK:
`artifacts/bin/coreclr/libnx.arm64.Release/aotsdk`.
The clr.nativeaotruntime subset supplies bootstrapper and native support
archives; clr.nativeaotlibs supplies the four matching managed SDK assemblies.
Do not run the managed probe helper merely to obtain these inputs.

Follow the [CoreCLR host recipe](../../../../../pal/tests/libnx/host/README.md)
and its linked thread guide for the pinned staged libnx SDK, coreclr-probe
CMake configuration, native BCL and libs.sfx framework build. Export
`LIBNX_ROOT` as that staged SDK directory and `ICU_NX_INSTALL_DIR` as the
source-built ICU installation prefix. Build the current NativeAOT workstation
archive in the same coreclr-probe configuration. From the runtime root:

```sh
cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
  --target Runtime.WorkstationGC System.Native-Static \
  System.Globalization.Native-Static System.IO.Compression.Native-Static -- -j6
python3 src/coreclr/nativeaot/Runtime/libnx/tests/networking-async/build.py \
  --sdk artifacts/bin/coreclr/libnx.arm64.Release/aotsdk \
  --sockets artifacts/bin/runtime/net10.0-libnx-Release-arm64/System.Net.Sockets.dll \
  --libnx-root "$LIBNX_ROOT" --output artifacts/libnx-nativeaot-async-sockets
```

The native link uses the current checkout's coreclr-probe runtime/BCL archives,
its source-built zlib/Brotli dependencies, the selected ICU installation and
the explicit libnx SDK. Keep the native and managed inputs version-matched.
The helper requests stable ILC/runtime 10.0.12 packages from NuGet, selects only
the supplied Horizon socket assembly in the ILC response file, and rejects IL
warnings or Linux inline TLS. It does not modify the supplied SDK.

## Outputs and lifetime

Use a dedicated output directory: the helper replaces generated project,
source-copy, object, linker, NRO and report files there. Keep user inputs
elsewhere. The generated manifest and link map describe the selected inputs;
they are outputs, not prebuilt prerequisites.

The native host initializes BSD with sb_efficiency=8 and retains it through
process exit for the background socket engine. It uses a 120-second watchdog.
Managed result 100 indicates completed checks. Use full application memory.

The host overwrites `/switch/nativeaot-async-sockets.txt`,
`/switch/nativeaot-async-sockets-stderr.txt` and
`/switch/nativeaot-async-sockets-stdout.txt` on the SD card; preserve existing
files before running it. NativeAOT remains resident until process exit.
