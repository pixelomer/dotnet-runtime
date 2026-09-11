# Embedded Horizon CoreCLR host

This integration probe calls the CoreCLR embedding APIs and statically links
the production VM, RyuJIT, GC and PAL. Follow the
[thread probe](../threads/README.md) for devkitPro/ICU prerequisites, the pinned
libnx source, SDK staging and CoreCLR cross-configuration.
Export `ICU_NX_INSTALL_DIR` to the source-built ICU installation. From the
runtime root:

```sh
./build.sh clr.corelib -os libnx -arch arm64 -c Release /p:PublicSign=true
cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
  --target coreclr_static -- -j6
python3 src/coreclr/pal/tests/libnx/host/build.py
```

The source build bootstraps the SDK pinned in global.json. The helper uses
native Release flags, the staged LIBNX_ROOT from CMakeCache, and the explicitly
selected ICU archives. It compiles the selected probe against this checkout's target
CoreLib with that SDK's C# compiler. Outputs under
`artifacts/libnx-coreclr-host` include NRO, ELF, map and the managed directory.

Copy the generated managed directory to `/switch/coreclr-probe` on the SD card,
preserving any existing files there. Runtime paths use `/switch/...` through
libnx's default SD device: Unix TPA lists use colon separators, so `sdmc:`
prefixes are not valid TPA list elements. The loader-provided NRO path is passed
unchanged to CoreCLR.

The host disables the unsupported FIFO debugger transport with
DOTNET_EnableDiagnostics_Debugger. It overwrites `coreclr-host-probe.txt`,
`coreclr-host-stderr.txt` and `coreclr-host-stdout.txt` under `/switch`; preserve
existing logs before running it. Tracing records native operations without
replacing their results.

Managed Main checks arithmetic, an allocation surviving collection and a caught
exception, with expected managed exit code 100. A native link or successful
coreclr_initialize alone does not demonstrate completion of Main. This is a
focused embedding probe, not an exhaustive runtime or unload/restart check.

Use `--probe stress` to compile Stress.cs instead of the basic Probe.cs:

```sh
python3 src/coreclr/pal/tests/libnx/host/build.py --probe stress
```

Both selections produce Probe.dll in the same managed output directory. Copy
the newly generated managed files to the documented SD destination whenever
the selection or managed inputs change; rebuilding does not deploy them.
Compilation is deterministic.

The stress workload checks worker/GC/TLS/finalizer and unmanaged-call behavior.
The host passes its reporting function address as an explicit argument; this
does not rely on native-library name lookup. The basic probe remains available
with `--probe basic`, which is the default.

Add `--jit-trace` to direct upstream disassembly to the buffered
`sdmc:/switch/coreclr-jit-disasm.txt`. The host removes any existing file at
that path before initialization; preserve it before running this option.
