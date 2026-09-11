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

The inherited `build-sockets.py`, SDK packager/validator and other probe build
recipes target .NET 9.0.3 at this point in the branch. Use
`horizon-net9-nativeaot` for those recipes. The managed stress probe above is
versioned for .NET 10; the .NET 9 package must not be used as its runtime input.
