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

The socket probe additionally requires the source-built Horizon
System.Net.Sockets assembly in ILC's reference list, selected exclusively.
The polling engine depends on the native event-buffer and close-on-exec
handling in this fork; replacing the NativeAOT SDK assemblies alone does
not select every platform BCL implementation.

Applications using full globalization must load `icudt77l.dat` with
`udata_setCommonData` before managed entry and retain that storage until
process exit. Linking ICU archives alone does not provide the data.
Even formatting an endpoint in a socket exception can initialize CultureInfo.
See the [networking probe](tests/networking/README.md) for initialization and
source socket assembly instructions.

## Isolated socket-library reference recipe

After building the native runtime/BCL and managed SDK assemblies above,
run from the runtime root:

```sh
python3 src/coreclr/nativeaot/Runtime/libnx/build-sockets.py \
  --output "$PWD/artifacts/sockets-reference-build"
```

Choose a new output directory. The script rebuilds the reference projects
named by the Release socket project and their source dependencies into an
isolated pack, then rebuilds the Horizon socket library. It checks that
compilation resolves exactly the expected references from that pack.

The recipe uses the normal repository SDK bootstrap and NuGet dependencies;
it does not require the broad `libs.ref` build or build all framework libraries.
ILC must still select the resulting socket assembly explicitly, as shown in
the networking probe's build script.
