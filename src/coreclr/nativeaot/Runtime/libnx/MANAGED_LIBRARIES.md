# Building the Horizon managed NativeAOT libraries

The upstream Linux ARM64 NativeAOT SDK DLLs do not provide a complete Horizon
BCL: they treat `sdmc:/` and `romfs:/` as relative paths. This fork's shared
CoreLib sources include TARGET_LIBNX root handling; compile it for NativeAOT
as well.

Build `clr.nativeaotruntime+libs.native` using the prerequisites and commands in
the [managed stress guide](tests/managed/README.md), then run from the runtime
root with `ICU_NX_INSTALL_DIR` exported:

```sh
ROOTFS_DIR="$DEVKITPRO" ./build.sh \
  -s clr.nativeaotlibs -c Release --cross -a arm64 --os libnx \
  /p:NativeAotSupported=true /p:EnableTrimAnalyzer=false /p:PublicSign=true
```

Outputs are the five `System.Private.*.dll` files under
`artifacts/bin/coreclr/libnx.arm64.Release/aotsdk/`. The source baseline is
.NET 9.0.3, commit `d75fa786b88e10f45ca9263773d84e47033cfb01`, plus this fork.
Pass the absolute output directory with a trailing slash to application publish
using `-p:IlcSdkPath=.../aotsdk/`. Keep `--noinlinetls` and link the Horizon
native archives; replacing managed assemblies does not convert Linux native
archives. The generated `.ilc.rsp` records which managed SDK paths ILC uses.

`PublicSign=true` retains assembly identity and public keys without private-key
signing or changes to the host's cryptographic policy.

Replace source-built CoreLib, TypeLoader, reflection execution, stack trace and
reflection-disabled assemblies together. Other framework DLLs still come from
the pinned official ILC package. The supported root list here is `sdmc:/`,
`romfs:/` and `/`; the [filesystem probe](tests/filesystem/README.md) covers a
subset of filesystem operations, not networking or arbitrary device roots.
