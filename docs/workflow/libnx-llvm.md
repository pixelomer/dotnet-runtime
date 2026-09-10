# libnx LLVM AOT support

Baseline: exelix11/dotnet_runtime
`d75fa786b88e10f45ca9263773d84e47033cfb01` (.NET 9.0.3).

This branch adds target support needed for LLVM-produced static AOT modules:

1. `MonoEnableLLVMRuntime=true` enables LLVM runtime support independently of
   the LLVM compiler. Use this for the ARM64 Horizon runtime.
2. Compile the libnx target's `llvm-runtime.cpp` with exceptions enabled. The
   toolchain's default `-fno-exceptions` conflicts with its C++ throw/catch helpers.
3. LLVM emits PIC for TARGET_LIBNX in static mode. The final NRO is PIE;
   static relocation mode can produce forbidden read-only dynamic relocations.

Target build, with devkitPro/libnx 4.10.0 or later:

```sh
ROOTFS_DIR="$DEVKITPRO" ./build.sh -s mono.runtime -c Release \
  --cross -a arm64 --os libnx /p:MonoEnableLLVMRuntime=true
```

The cross compiler runs on Linux x64. It requires target offset generation with
the devkitPro sysroot and the pinned host LLVM package's libclang, followed by
`-s mono.aotcross` with ROOTFS_DIR empty, `MonoCrossAOTTargetOS=libnx`,
`BuildMonoAOTCrossCompilerOnly=true`, `BuildMonoAOTCrossCompiler=true`,
`SkipMonoCrossJitConfigure=true`, `AotHostArchitecture=x64`, `AotHostOS=linux`,
and `MonoAOTEnableLLVM=true`. The [mono-nx build script](https://github.com/pixelomer/mono-nx/blob/main/build_llvm.sh)
provides the complete sequence.

Use `--llvm` and explicitly set `llvm-outfile` for each assembly in static mode.
Both the ordinary AOT object and the LLVM object must be linked. The compiler
version must report LLVM enabled.
