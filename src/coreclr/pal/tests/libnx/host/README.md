# Embedded Horizon CoreCLR host

This integration probe calls the CoreCLR embedding APIs and statically links
the production VM, RyuJIT, GC and PAL. Follow the
[thread probe](../threads/README.md) for devkitPro/ICU prerequisites, the pinned
libnx source, SDK staging and CoreCLR cross-configuration.
Export `ICU_NX_INSTALL_DIR` to the source-built ICU installation. The host's QCall checker
requires dnfile and pyelftools. Use Python 3.11 or newer and install them into
a local Python environment
before invoking the helper. From the runtime root:

```sh
python3 -m venv artifacts/qcall-python
. artifacts/qcall-python/bin/activate
python3 -m pip install dnfile pyelftools
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

All probe selections produce Probe.dll in the selected managed output directory. Copy
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

## Suspension and framework probes

Use `--probe suspension` for four tight-loop workers in ordinary and
try/catch/finally regions, compacting collections, and live object/interior
root checks. The native watchdog releases loops when a collection stalls for
three seconds. Managed exit 101 indicates timeout; exit 100 requires intact
roots and at least one observed relocation. `--minopts` selects
DOTNET_JITMinOpts; combine it with `--jit-trace` to inspect the emitted worker
code. The suspension variant disables tiered compilation.

For the BCL probe, first build the platform framework from this checkout:

```sh
./build.sh libs.sfx -os libnx -arch arm64 -c Release \
  /p:RuntimeFlavor=CoreCLR /p:PublicSign=true
python3 src/coreclr/pal/tests/libnx/host/build.py \
  --probe bcl --output artifacts/libnx-coreclr-bcl
```

The native link also requires the three source-built Brotli archives, included
by the compression target's existing dependencies. The helper copies framework
DLLs from `artifacts/bin/runtime/net10.0-libnx-Release-arm64` beside the selected
CoreLib. Deploy that entire managed directory to the documented SD destination;
the host builds its TPA list from deployed DLLs other than Probe.dll. Keep
unrelated DLLs out of this probe-specific directory.

The BCL workload covers Console, files/async I/O, timers/cancellation, reflection,
generated IL, collectible assembly loading, JSON and compression. It uses
invariant globalization. A 120-second native watchdog exits the process if the
managed workload does not complete. The workload creates a tick-named
`/switch/coreclr-bcl-owned-*` directory and recursively removes it after
creation; do not run concurrent instances or place other files in that directory.
An interrupted run can leave its temporary input behind.

`--corelib PATH` optionally selects a source-built compatible CoreLib instead
of this checkout's default output. It must still pass the QCall metadata check.

## Generated outputs

Use `--output` to select a dedicated generated-output directory for each
variant. Every build removes and recreates its `managed` and `source-snapshot`
subdirectories and replaces generated NRO/ELF/map, reports and source diff.
Keep user inputs outside those subdirectories and preserve outputs before
rebuilding when needed. The default output directory is
`artifacts/libnx-coreclr-host`.

Generated build-manifest.json records selected options, source and managed
inputs and the actual linked inputs. It describes the current build, not a
prerequisite. The helper requires Python 3.11 or newer for hashlib.file_digest.

## Managed/native QCall consistency

The host build runs `validate-qcalls.py CORELIB HOST_ELF` using the same Python
interpreter. The checker reads CoreLib ImplMap metadata and the linked ELF's
QCall table without executing either input. Keep the unstripped ELF and its
symbol table; inputs must be an ARM64 little-endian 64-bit host and the matching
CoreLib. Missing managed imports or invalid native table entries fail the build.

The generated report is `qcall-validation.json` in the selected output directory.
It contains input fingerprints, import counts and missing names for the current
build. This is a metadata contract check, not a behavioral test of every entry.
