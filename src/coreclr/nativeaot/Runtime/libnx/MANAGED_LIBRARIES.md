> Build this branch's .NET 10 SDK with `python3 eng/libnx/build.py --flavor nativeaot`
> from the repository root, then source `eng/libnx/env.sh`. Follow the
> [source-build prerequisites](../../../../../eng/libnx/README.md).
> Keep its .NET 10.0.12 runtime, SDK assemblies and ILC together.

# Building the Horizon managed NativeAOT libraries

This branch uses .NET 10.0.12, public baseline
`4271d88e0aebf3d04f188f1334c2220d80555ef6`, with the Horizon adapters.
Build the native runtime/BCL and matching managed SDK using the
[managed stress guide](tests/managed/README.md), including its devkitPro and
Horizon ICU source prerequisites.

The `clr.nativeaotlibs` subset produces CoreLib, TypeLoader,
Reflection.Execution and StackTraceMetadata under
`artifacts/bin/coreclr/libnx.arm64.Release/aotsdk/`.
Replace these four assemblies together. Pass that absolute directory with a
trailing slash as `-p:IlcSdkPath=.../aotsdk/` during application compilation.
Use ILC and managed framework packages from .NET 10.0.12.

Keep `--noinlinetls` and link the Horizon native runtime/BCL archives, including
`libaotminipal.a`. Replacing managed assemblies does not convert Linux native
archives. The compiler response file records the selected assembly paths.
`PublicSign=true` retains assembly identity and public keys without private-key
signing or host cryptographic-policy changes.

The shared CoreLib implements Horizon roots such as `sdmc:/` and `romfs:/`.
Platform-specific BCL assemblies must be selected explicitly; replacing
CoreLib alone cannot substitute every platform implementation.

Applications using full globalization must load `icudt77l.dat` with
`udata_setCommonData` before managed entry and retain it until process exit.
Linking ICU archives alone does not supply this data.

The inherited build-sockets.py and synchronous networking recipes target
.NET 9.0.3; use horizon-net9-nativeaot for those recipes. The SDK packager and
validator select the source runtime version. The managed stress probe above
uses .NET 10; a .NET 9 package must not be used as its runtime input.
