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

## Thread ownership

`mono_thread_platform_create_thread`
in `src/mono/mono/utils/mono-threads-posix.c` creates joinable pthreads.
`ves_icall_System_Threading_Thread_Join_internal` calls `mono_thread_join` after
the managed wait; the runtime's joinable-thread registry also reaps completed
runtime threads. Do not add a second independent reaper to that path. In
addition, `mono_threads_platform_exit` can call pthread_exit; a wrapper which
queues reclamation only after its callback returns would miss this path.

The separate native BCL API `SystemNative_CreateThread` in
`src/native/libs/System.Native/pal_threading.c` still sets DETACHED and discards
the pthread handle. This contract differs from Mono's managed-thread path and
requires separate handling because libnx ignores the pthread detach state.
The NativeAOT reaper handles that API; Mono's managed Thread.Join path has its
own ownership and reclamation contract.

## TLS container lifetime

The Horizon implementation in `src/mono/mono/utils/mono-tls.c` keeps its
multiplexed TLS container visible while destructors run. It clears each value
before invoking its destructor and permits up to four passes for values restored
by callbacks. After those passes it clears the native slot and frees the container.
Reading an unset value or assigning null does not allocate a container. This
ownership contract is independent of LLVM code generation.
