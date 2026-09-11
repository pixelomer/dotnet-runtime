# Horizon native-context conversion

Install devkitA64, switch-tools and libnx 4.12 through devkitPro. Set
`DEVKITPRO` to the installation, `DEVKITA64=$DEVKITPRO/devkitA64`, and include
`$DEVKITA64/bin` in `PATH`. Obtain Horizon ICU using the source prerequisites
in the [NativeAOT PAL guide](../../../../nativeaot/Runtime/libnx/tests/pal/README.md)
and export `ICU_NX_INSTALL_DIR` as its installation prefix.

From the runtime root, cross-configure CoreCLR and build the probe:

```sh
ROOTFS_DIR="$DEVKITPRO" src/coreclr/build-runtime.sh \
  -arm64 -release -cross -os libnx \
  -subdir coreclr-probe -component runtime -configureonly \
  -cmakeargs '-DFEATURE_EVENT_TRACE=OFF' \
  -cmakeargs '-DFEATURE_PERFTRACING=OFF'
python3 src/coreclr/pal/tests/libnx/context/build.py
```

The script reads the generated PAL compiler flags and compiles the actual
`pal/src/thread/context.cpp`. Section garbage collection excludes unused PAL
APIs; the probe does not initialize the runtime or install a fault handler.
Outputs are under `artifacts/libnx-coreclr-context/`.

The NRO writes `sdmc:/switch/coreclr-context-probe.txt` and requests application
exit to HOME. Preserve an existing log before running it.

Synthetic snapshots exercise all subsets of control, integer and SIMD register
requests, full GPR/SIMD values, FP control/status, untouched non-requested
storage, unsupported debug flags, context metadata, TLS preservation, PC/SP
accessors and conversion back to libnx's format. Conversion neither pauses a
thread nor relocates roots.

The adapter retains the Microsoft PAL definitions and public libnx
`ThreadContext` layout from source revision
`7644c9b26099aa2d2145bc72a21ee24190e92085`.
