# Embedded Horizon CoreCLR host

This integration probe calls the CoreCLR embedding APIs and statically links
the production VM, RyuJIT, GC and PAL. Follow the
[source-build guide](../../../../../../eng/libnx/README.md) for Linux host and
devkitPro prerequisites, the pinned libnx SDK overlay, ICU and matching runtime
inputs. Source eng/libnx/env.sh after the build to select its SDK and ICU. The host's QCall checker
requires dnfile and pyelftools; the IL-format checker requires pefile. Use Python 3.12 or newer and install them into
a local Python environment
before invoking the helper. From the runtime root:

```sh
python3 -m venv artifacts/qcall-python
. artifacts/qcall-python/bin/activate
python3 -m pip install dnfile pyelftools pefile
python3 eng/libnx/build.py --flavor coreclr
source eng/libnx/env.sh
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

Built-in probe selections produce Probe.dll in the selected managed output directory. Copy
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
the host builds its TPA list from deployed DLLs other than the selected entry assembly (Probe.dll by default). Keep
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

## Async socket probe

After building the platform framework with the libs.sfx command above, use:

```sh
python3 src/coreclr/pal/tests/libnx/host/build.py \
  --probe sockets --output artifacts/libnx-coreclr-sockets
```

The loopback workload exercises send backpressure, async operation cancellation
and close/descriptor reuse with the same finite native watchdog. The host
initializes BSD with sb_efficiency=8 and retains it until process exit.
It does not alter network settings.

`--framework PATH` optionally selects a compatible source-built framework
directory instead of `artifacts/bin/runtime/net10.0-libnx-Release-arm64`.
Keep its assemblies together with matching CoreLib; the helper copies framework
DLLs except CoreLib and Probe.dll. Deploy the complete generated managed
directory as described above.

The [NativeAOT counterpart](../../../../nativeaot/Runtime/libnx/tests/networking-async/README.md)
uses the same SocketProbe.cs and explicitly selects the Horizon socket assembly.

The socket suite also leaves an unread byte on an idle registered socket for
750 ms, then checks that it can still receive that byte. SocketPollTrace.cpp
counts native poll calls while forwarding every call unchanged. More than
100 calls in that interval produces native host result 112 independently of
managed exit. Read that host result as well as the runtime's latched shutdown
code.

## Generated suspension control flow

After the libs.sfx framework build above, select the generated-loop workload:

```sh
python3 src/coreclr/pal/tests/libnx/host/build.py \
  --probe suspension-flows --output artifacts/libnx-coreclr-suspension-flows
```

It uses Reflection.Emit to generate mutual tail-call cycles and an irreducible
loop. Four workers retain live object/interior references during up to 32
compacting collections. Exit 100 requires preserved root contents, at least
one observed relocation and no suspension watchdog timeout; timeout returns
101. Workers must also terminate within their bounded joins.

Use `--minopts` for minimum-optimization code generation and `--jit-trace` to
inspect actual loop and tail-call lowering. This variant disables tiered
compilation and uses both the suspension and overall native watchdogs.
Deploy its complete managed directory after each build. Compiler warning
CS0649 for Root.Value reflects field writes emitted dynamically by the probe.

## Tiered compilation and lifetime workload

After building the source framework with libs.sfx, select the soak variant:

```sh
python3 src/coreclr/pal/tests/libnx/host/build.py \
  --probe soak --jit-trace --output artifacts/libnx-coreclr-soak
```

Deploy its complete managed directory to the documented SD destination.
Soak.cs loads its own `/switch/coreclr-probe/Probe.dll` in collectible
contexts, invokes Compute and checks unload through weak references.
Keep the deployed Probe.dll matched to the selected workload.

Four persistent workers retain object/interior roots in call-free HotLoop
methods. Each round allocates arrays, creates and joins transient workers,
checks thread-static isolation, runs finalizers and unloads a collectible
context. The workload stops after three minutes or 180 rounds, or on timeout.
These are workload limits, not measured throughput.

The native host enables tiered compilation, quick JIT for loops and aggressive
tiering. With `--jit-trace`, disassembly is restricted to Soak:HotLoop; inspect
the emitted tiers and any on-stack replacement rather than inferring them from
a source option. The three-second watchdog covers the entire round, not only
explicit collections. The outer process watchdog allows 240 seconds.
Exit 100 requires intact roots, observed relocation and no round timeout;
a round timeout returns 101 after releasing workers.

The host removes `/switch/coreclr-soak-progress.txt` before startup and appends
progress/memory samples by opening and closing it for each update. Preserve any
existing file before running, along with the ordinary host and disassembly
logs described above. Managed live-memory, native allocation and Horizon
process-used counters have different scopes; none alone proves an absence of
leaks or represents an isolated GC pause.

## IL deployment format and ReadyToRun controls

The host builder runs [validate-il.py](validate-il.py) on the generated managed
deployment. Install pefile in the same Python environment as the QCall tools.
The checker reads PE/CLI headers without loading assemblies. It requires a
managed IL-only input with no native header, no 32-bit-only requirement or
native entry point, and an AnyCPU or ARM64 PE format. It rejects ReadyToRun and
foreign CPU formats. This format check is not a security validator and does
not establish framework or API compatibility.

Normal application assemblies must be built with `PublishReadyToRun=false`.
The Horizon VM forces native ReadyToRun execution off even if
`DOTNET_ReadyToRun=1`; that does not make a foreign ReadyToRun PE image a
supported IL-only deployment. Loader identity, mapping and protection checks
still apply.

For an explicit unsupported-format control, the source fixture in
[r2r-control](r2r-control/OwnedReadyToRun.cs) supplies its own checked arithmetic
method. Install Microsoft .NET SDK 10.0.111 for this standalone helper, which
downloads Linux ARM64 ReadyToRun tooling/framework 10.0.12 through restore.
From the runtime root:

```sh
python3 src/coreclr/pal/tests/libnx/host/r2r-control/build.py
python3 src/coreclr/pal/tests/libnx/host/build.py \
  --probe r2r \
  --r2r-input artifacts/libnx-r2r-control/publish/OwnedReadyToRun.dll \
  --output artifacts/libnx-coreclr-r2r --jit-trace
```

The control builder requires dnfile and writes the default AnyCPU-source
variant under `artifacts/libnx-r2r-control`. Add `--platform-specific` to
retain RID-inferred identity; its output is under
`artifacts/libnx-r2r-control-architecture`. Pass that variant's publish DLL
as `--r2r-input` when selecting it. Each helper invocation replaces generated
project/source-copy/publish/report files in its selected output; keep user
inputs elsewhere.

The host still needs the source-built CoreLib and libs.sfx framework described
above. Only `OwnedReadyToRun.dll` is excluded from its IL-format check for the
explicit r2r selection. It is copied into the generated managed directory;
deploy that whole directory and preserve existing probe files as documented.
Keep the control input outside the host's replaced managed/source-snapshot
subdirectories.

The workload attempts path-based loading, arithmetic/overflow and default-context
identity checks. Managed result 100 means those checks completed; loader
rejection or another exception returns 110 with diagnostic output. This control
does not declare foreign ReadyToRun loading supported. Do not bypass the normal
deployment format checker to use it for application inputs.

## Existing application entry points

After the source CoreLib/framework builds described above, --probe bcl with
--application-entry Application.dll and --managed-reference pointing to the
caller's IL-only Application.dll deploys and executes that application directly.
Supply its other managed dependencies with repeated --managed-reference inputs;
keep them compatible with this checkout's framework. The entry must be a plain,
non-reserved DLL basename, and --managed-source cannot be combined with it.

No reflection launcher is inserted. The entry DLL is excluded from TPA,
and the selected --managed-directory is passed as APP_CONTEXT_BASE_DIRECTORY.
The host calls coreclr_execute_assembly with no application arguments.
An optional native HostConfigureApplication function can set the application's
working directory and environment before CLR initialization; a nonzero result
aborts initialization.

--watchdog-seconds accepts 0 through 3600. Zero disables the BCL host's timeout
for interactive applications while retaining completion monitoring. The option
does not change the separate suspension watchdog. Default probe behavior is
retained when these options are absent.

See the [embedding guide](../../../../../../docs/workflow/libnx-embedding-hooks.md)
for optional native objects, static libraries, explicit exports and import
registration. Use source-built integration inputs for the same target SDK/ABI.
Choose dedicated generated output and SD deployment directories, keep inputs
outside the helper's replaced subdirectories, deploy all generated managed
files and preserve existing files/logs as described above.


For native failure diagnostics, `--wrap-symbol NAME` links a reviewed
`__wrap_NAME` implementation supplied through `--native-object`. Names use the
same restricted identifier validation as exported symbols. The build manifest
records the selected wrappers; default integration behavior is unchanged.
Supply optional wrapper objects from integration source built with the same
target SDK and ABI. Diagnostic wrappers should forward successful and failing
calls unchanged when they are intended only to observe behavior; --wrap-symbol
itself does not enforce that property.
