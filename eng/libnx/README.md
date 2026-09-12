# .NET runtimes for Horizon

This fork supplies Mono, NativeAOT and CoreCLR/RyuJIT for Nintendo Switch
homebrew. It uses devkitA64, public libnx APIs and the upstream .NET runtime.
The SDK entry point is [pixelomer/dotnet-switch](https://github.com/pixelomer/dotnet-switch).
[mono-nx](https://github.com/pixelomer/mono-nx) supplies the Mono launcher and
AOT examples.

| Branch | Runtime | Intended consumers |
| --- | --- | --- |
| `main` | .NET 10.0.12 CoreCLR/RyuJIT and NativeAOT | Dynamic IL/mod loading and new homebrew |
| `horizon-net9-nativeaot` | .NET 9.0.3 NativeAOT | Existing .NET 9 NativeAOT applications |
| `horizon-net9-mono` | .NET 9 Mono interpreter/AOT/LLVM | mono-nx |

Keep a consumer's compiler, CoreLib, managed framework and native archives from
one matching build. Switching branches is not an application migration recipe.
Use separate clones/build directories for the profiles.

## Build from a fresh checkout

The supported build host is Linux x86-64 with Python 3.12+, Git, Bash, GCC/G++,
Clang/LLVM, CMake, Ninja, Make, patch, pkg-config and the normal .NET source-build
prerequisites. Install devkitPro's devkitA64, switch-tools, libnx and Switch
portlibs. Set `DEVKITPRO` to that installation (normally `/opt/devkitpro`).
The runtime bootstraps the Microsoft SDK pinned in `global.json`; downloading
NuGet packages requires internet access. The compiler baseline is
devkitA64 GCC 15.2.0. The source-pinned libnx fork is based on 4.12.0.

```sh
# In a checkout of this repository, select the .NET 10 profile:
git switch main
python3 eng/libnx/build.py --flavor coreclr --jobs 8
```

On `horizon-net9-nativeaot`, use `--flavor nativeaot`; on
`horizon-net9-mono`, use `--flavor mono --llvm`. The scripts fetch the exact
[pixelomer/libnx](https://github.com/pixelomer/libnx) commit in
`dependencies.json`, build it in a private SDK overlay, download and verify ICU
77.1, build host and Horizon ICU, then build the selected runtime and libraries.
They do not install over system packages. Re-running reuses matching downloads
and incremental runtime outputs. A dependency checkout with local changes or a
different commit is rejected rather than reset.

Use a dedicated checkout and keep user inputs outside artifacts/horizon.
When dependency identity changes, the helper removes and recreates its ICU
source and host/target build directories and applies the committed compatibility
patch. The SDK overlay and generated outputs are updated in place. Back up
outputs before rebuilding if they are needed. Generated environment/build
manifests describe that build and are not external prerequisites.

Outputs stay under `artifacts/`. `artifacts/horizon/environment.json` records
the SDK overlay and ICU paths for integration tools. `--dependencies-only`
builds just the native prerequisites. `--source-mirrors FILE` optionally maps
canonical Git URLs to local Git mirrors; it supplies source objects, never
prebuilt runtime folders. Mirror mappings are optional local configuration and
are not part of the repository.

For NativeAOT, build outputs include
`artifacts/bin/coreclr/libnx.arm64.Release/aotsdk/` and
`artifacts/bin/native/netVERSION-libnx-Release-arm64/`. The .NET 9 recipe also
builds the source socket assembly. CoreCLR supplies `coreclr_static` and its
native dependencies under
`artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe/`, CoreLib under
`artifacts/bin/coreclr/libnx.arm64.Release/IL/`, and the Horizon framework under
`artifacts/bin/runtime/net10.0-libnx-Release-arm64/`.

## Embedding and supported scope

Use full application memory. Initialize the native services required by your
application, pair managed and native runtime inputs, retain runtime-owned memory
until process exit, and use the supported libnx TLS convention. NativeAOT needs
`--noinlinetls`; it does not load arbitrary new managed code after compilation.
CoreCLR supports IL loading, JIT compilation and runtime hooks. ReadyToRun images, arbitrary external native-library loading,
subprocesses and debugger/EventPipe transport are not supported. OS-specific
APIs must be checked against the supported profile rather than assumed.

The [supported profile](../../docs/workflow/libnx-supported-profile.md)
documents additional .NET 10 limits; it does not describe the .NET 9 branches.

## Sources and licenses

The upstream .NET source retains `LICENSE.TXT` and
`THIRD-PARTY-NOTICES.TXT`. libnx is fetched from the public homebrew fork and
retains its license. ICU is fetched from the Unicode project's official release;
its single header compatibility patch originates in exelix11/mono-nx (`LICENSE`,
MIT). No Nintendo SDK, commercial runtime, game files, console keys or FMOD
libraries are needed to build these runtimes.
