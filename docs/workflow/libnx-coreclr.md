# Horizon CoreCLR PAL

The runtime baseline is .NET 10.0.12, public commit
`4271d88e0aebf3d04f188f1334c2220d80555ef6`.

## Native contexts

The PAL's `native_context_t` uses libnx's `ThreadContext`.
Register conversion respects requested groups, preserves non-register metadata
flags and clears validity for unavailable debug/SVE state. It never changes
the libnx TLS register. The caller owns suspension and the snapshot's lifetime.
Horizon faults carry `ThreadExceptionDump` and ESR information, not POSIX
`siginfo`; the Unix signal-code decoder is excluded.

The [context probe](../../src/coreclr/pal/tests/libnx/context/README.md)
compiles the actual PAL conversion/accessor functions and exercises synthetic
snapshots. It does not initialize CoreCLR, dispatch faults or relocate GC roots.

Runtime integration requires writable saved-register locations for moving GC,
executable allocation with explicit backing and alias ownership, thread suspension
and exception dispatch, and static native import resolution. Dynamic IL loading
and native module loading are separate contracts.

## Reference APIs and licenses

The adapter uses public .NET PAL and libnx definitions. Preserve notices
for copied upstream source. The vendored LLVM unwind implementation has its own notices.
Atmosphere's code-memory service implementation is a behavioral reference,
not code incorporated into this runtime.

- [.NET 10.0.12](https://github.com/dotnet/runtime/tree/v10.0.12)
- [libnx 4.12.0](https://github.com/switchbrew/libnx/tree/v4.12.0)
- [Atmosphere code-memory service](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/libraries/libmesosphere/source/svc/kern_svc_code_memory.cpp)

## Native DWARF unwinding

The PAL selects the .NET-vendored LLVM DWARF decoder through
`PAL_VirtualUnwind`. The adapter supplies saved-register homes for X19–X28,
FP/LR and D8–D15. A caller-owned image descriptor provides code and EH sections,
which must remain mapped throughout the unwind. Unknown code addresses fail
without changing the caller's context or output pointers.

The static-NRO entry point uses link-time EH boundaries. Other native images
need their own section descriptors; JIT-managed frames use CoreCLR's managed
code manager. Undefined/value-only register rules produce no saved home.
See the [native unwind probe](../../src/coreclr/pal/tests/libnx/unwind/README.md)
for spill updates, epilogue restoration and lookup-failure cases. Keep the
vendored LLVM license and notices.

## System information

Logical CPU count comes from the process core mask; CPU-set storage uses one
past the highest possible index so sparse masks fit. Address-space limits come
from Horizon's ASLR-region query. Invalid bounds terminate the void system-info
API rather than supplying fabricated limits.

Default-stack configuration uses the existing 64 KiB thread-creation fallback
when newlib omits `PTHREAD_STACK_MIN`. File-descriptor limit adjustment is
disabled because Horizon/newlib does not provide the POSIX resource-limit API.

## Data virtual memory

The PAL data allocator uses `map/libnx/virtual.cpp` with the shared
`src/native/libs/Common/nxvm.c` pool. It owns reservation metadata and implements
reserve, commit, decommit and release. Queries intersect kernel regions with PAL
allocation boundaries. Fixed placement and unsupported Stack-alias protection
changes fail explicitly; executable allocation is a separate backend.

Decommit recycles backing into the bounded shared pool rather than returning
the entire pool to Horizon. Uncommitted reservations made directly by other nxvm
consumers are outside this PAL's reservation table. The
[virtual-memory probe](../../src/coreclr/pal/tests/libnx/virtual-memory/README.md)
exercises shared ownership, quota rollback, zero-filled reuse and query bounds.
System information uses Horizon's 4096-byte pages and C linkage for libnx APIs.

## Diagnostics and CPU quotas

Static Horizon PAL diagnostics identify their module using the NRO linker base,
without dynamic-loader APIs. Message formatting and process code do not depend
on unused POSIX mapping headers; the utility mapping include is Apple-only.

Horizon has no cgroup hierarchy or CPU-bandwidth quota. Cgroup initialization
and cleanup own no resources, and `PAL_GetCpuLimit` returns FALSE without writing
the output. Permitted CPU count comes from the separate kernel-backed query.

## CoreCLR executable allocator integration

The Horizon minipal target selects `minipal/libnx/doublemapping.cpp`.
Its VMToOSInterface backend uses process mappings and paired libnx-reserved
virtual arenas. Partial commitments and referenced writable subviews retain
explicit backing and mapping ownership. Cache publication resolves the writable
alias even while CoreCLR caches a writer view.

The existing executable allocator and loader heaps are unchanged. A null
CreateTemplate selects CoreCLR's dynamic interleaved code/data path.
See the [executable-memory probe](../../src/coreclr/pal/tests/libnx/executable-memory/README.md)
for build instructions, bounded placement, mode restrictions and ownership
checks. This native boundary probe does not initialize the managed runtime.

The backend uses MapProcessCodeMemory, SetProcessMemoryPermission and
MapProcessMemory with the loader-supplied own-process handle. That handle is
borrowed, not closed by the allocator. This avoids allocating a separate
CodeMemory object for each fresh commit run.

## PAL file mapping and positional I/O

The `mapnative.h` adapter routes OS mapping operations while retaining CoreCLR's
PE layout and mapping-object handling. Horizon file views are buffered snapshots
mapped and protected through process-memory APIs. Shared writable files and
replacement of live fixed mappings fail explicitly. Read-only views do not
promise coherence with later file writes. Partial unmapping retains source
backing until its final page retires; image cleanup also releases unrecorded
reservations and alignment gaps.

The `LIBNX_ROOT` CMake option selects a staged libnx SDK. Use the
[file-mapping probe instructions](../../src/coreclr/pal/tests/libnx/file-mapping/README.md)
to obtain the pinned libnx source and stage all headers, including BSD headers.
The file-mapping and virtual-memory probes read this SDK path from CMakeCache.
The PAL pread bridge calls the driver's `fsdevPread` without changing a shared
descriptor's position or depending on private driver layouts. This bridge
supports fsdev descriptors; it does not implement other devoptab drivers.

Executable capability is fixed at mapping time. Scoped PAL writer aliases let
the PE relocation decoder update executable images while their primary mapping
remains RX. Writer acquisitions pin backing, split heterogeneous sections into
separate runs, roll back failed mappings and publish caches on release.

CoreCLR uses C++ exception cleanup for these views. The toolchain does not
disable C++ EH for the entire platform; NativeAOT controls it in its own scope.
The native file-mapping probe exercises these boundaries, not a complete
managed image load.

## Thread integration and exit-safe shared reaping

The PAL uses libnx's current Thread to obtain usable stack bounds and validates
them against the executing SP. Priority changes use a borrowed
`pthreadGetNativeHandle` and Horizon's process priority mask. The handle is
valid only while the pthread is live; no private pthread layout is imported.
The PAL retains its thread objects, startup handshake and synchronization.

The shared CoreCLR/NativeAOT/System.Native reaper queues completion from a TLS
destructor, covering both normal return and `pthread_exit`. Its
attribute-preserving interface publishes the native identity before completion
can be queued. The reaper joins kernel termination before freeing native
resources, even when later TLS destructors are still active. One reaper and one
completion TLS key remain for process lifetime.

See the [thread probe](../../src/coreclr/pal/tests/libnx/threads/README.md)
for the required libnx source revision, staging instructions and native boundary
checks. Usable stack size excludes libnx bootstrap storage; the probe compares
relative requested-size increments without depending on that private layout.

The Horizon cross-configuration records procfs and file-backed mmap-pager
features as unavailable, including their cached exit codes on reconfiguration.
The native thread probe does not initialize managed CoreCLR or exercise a full
PAL thread/GC-suspension lifecycle.

## PAL exception integration

Horizon user exceptions enter the existing CoreCLR SEHProcessException path,
including heap record promotion and PAL virtual-unwind transitions. Ordinary
dispatch runs below the interrupted SP so nested faults have independent frames.
CoreCLR's ARM64 RestoreCompleteContext deliberate-fault mechanism uses Horizon
ReturnFromException to restore the complete register set, including X16/X17.

SEH-enabled PAL threads register in the common Horizon thread registry shared
with NativeAOT. Process write-buffer flushing uses its synchronized kernel
pause/resume protocol. Shared-memory objects retain PAL's object handling and
use the native mapping adapter. Unsupported cross-process advisory locks,
writable shared file mappings, subprocess crash dumps and activation requests
fail explicitly.

The [exception probe](../../src/coreclr/pal/tests/libnx/exceptions/README.md)
exercises native read faults, nested dispatch and context restoration. It does
not initialize managed CoreCLR or implement managed GC activation.

## Resident native modules

The native module adapter retains PAL module management and describes the
resident NRO using its segment/BSS header, loader-supplied executable path and
System V dynamic symbol/hash metadata. Hosts must retain intended exports at
link time. Hidden, TLS and undefined symbols are not resolved. Lookup errors
are thread-local and consumable; releasing a lookup reference does not unload
the resident NRO. Loading external native modules is unsupported.

PAL_CopyModuleData validates the whole destination extent before copying native
segments and BSS. The adapter uses section-bound __code_start and hidden
PC-relative declarations for linker metadata, not an ordinary GOT reference to
the absolute __start__ symbol. The copied executable path lives until process
exit. libnx service declarations in the shared minipal thread header use C linkage.

See the [module probe](../../src/coreclr/pal/tests/libnx/modules/README.md)
for source prerequisites, exported-symbol lookup and bounded snapshot checks.
Dynamic managed IL loading is a separate runtime contract.

## PAL startup and synchronization

PAL initialization retains the object manager, synchronization worker, thread
startup handshake and resume semaphore. Horizon worker commands use an embedded
mutex/condition-variable byte channel with bounded backpressure, monotonic
timeouts, FIFO order and drain-before-EOF closure. Shutdown parking blocks on a
native condition variable.

svcGetProcessId supplies the checked process identity. Unix session IDs remain
unavailable; foreign-process handles, monitoring and subprocess creation fail
explicitly. minipal_getexepath resolves the homebrew loader's executable path
and supplies it to the resident-module adapter.

The [startup probe](../../src/coreclr/pal/tests/libnx/startup/README.md) exercises
PAL_InitializeCoreCLR/PAL_Shutdown, suspended-thread startup, single-resume
semantics, PAL events, retained exit records, kernel identities and the command
channel. PAL shutdown retains its process-lifetime contract; this is not managed
runtime initialization or an unload/restart interface.

## Shared GC OS support

CoreCLR and NativeAOT use `src/coreclr/gc/libnx/gcenv.libnx.cpp` for their Horizon
OS interface, retaining the GC algorithms and event implementation. Advisory
reset validates that the complete range is owned and committed without
discarding accessible bytes. Affinity reconfiguration starts from the kernel's
original allowed mask; the CPU index bound is one past its highest set bit.

The [GC OS probe](../../src/coreclr/pal/tests/libnx/gc-os/README.md) exercises
identity, affinity restoration, aligned reservation, commitment, reset and
release through the shared nxvm allocator. It links native adapter objects,
not a managed runtime.

EventPipe selects its existing TCP transport with the default listener disabled
on Horizon. GNU sincos declarations are enabled only for the arithmetic target,
without changing feature visibility globally.

## Embedded CoreCLR host

The embedded host links the production VM, RyuJIT, GC and PAL. The static link
interface includes the GC map encoder and compression library. Build matching
CoreLib from the same checkout; the [host probe](../../src/coreclr/pal/tests/libnx/host/README.md)
documents the pinned SDK, ICU and staged-libnx prerequisites.

PAL_ProbeMemory checks mapped spans and permissions without mutating caller
bytes. Named shared objects fail before creating files; unnamed mutexes keep
their process-local implementation. The filesystem FIFO debugger transport
rejects unsupported create/connect requests. The host uses the existing
DOTNET_EnableDiagnostics_Debugger opt-out.

VirtualProtect can initialize data in the resident NRO's declared read-only
segment through SetProcessMemoryPermission. After the first code-to-data
transition, ordinary memory-permission operations apply. Executable text is
excluded from that irreversible transition and retains its execute capability.
Host-only tracing preserves the original PAL operations and last-error value.

## Dedicated shared virtual arena

nxvm reserves an arena before native worker stacks fragment Horizon's stack
region. A libnx reservation excludes competing stack allocations; sorted
first-fit placement reuses aligned holes with guard gaps. Physical commitment
remains bounded separately by the backing pool. Arena sizing starts at twice
the backing budget, with a 64 MiB minimum and a cap of half the stack region,
then tries smaller sizes by halving. Initialization fails if none can be
reserved. GC limits come from the actual arena capacity and maximum address.

The GC OS probe covers sparse commitments and aligned hole reuse. The
[managed stress probe](../../src/coreclr/nativeaot/Runtime/libnx/tests/managed/README.md)
provides a source-build recipe for exercising the NativeAOT runtime and BCL.

## Device-qualified PAL paths

The shared root test recognizes device-qualified absolute paths. Existing
lexical dot/parent normalization operates on their rooted tail, preserving the
device prefix even at the root. Directory creation uses the same test.
The startup probe checks mounted/default-device paths, missing-file errors and
PAL open/read/close against a separate input whose native writer is closed first.
