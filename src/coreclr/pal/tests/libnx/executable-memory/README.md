# Horizon CoreCLR executable-memory probe

First follow the prerequisites and CoreCLR cross-configuration commands in the
[context probe](../context/README.md), then build from the repository root:

```sh
python3 src/coreclr/pal/tests/libnx/executable-memory/build.py
```

The test links production VMToOSInterface and PAL context.cpp using their
respective generated compiler flags. Unused PAL context functions are discarded
at link time; no fake PAL types or runtime success stubs are linked. Linker
wrappers inject one create/map failure at the SVC boundary while all successful
operations use the real Horizon kernel. Generated NRO, ELF and map are under
`artifacts/libnx-coreclr-exec`. The SD log is `/switch/coreclr-exec-probe.txt`.
The probe overwrites `sdmc:/switch/coreclr-exec-probe.txt`; preserve any existing
log before running it.

Eight rounds exercise 2,048 worker allocation lifetimes across 32 threads:
partial commitment, overlapping idempotent commits, views crossing backing
chunks, disjoint writer addresses for overlapping RX requests, independent
view retirement, real unmapping before single-thread reuse, unchanged RX
execution after writer retirement, zero initialization after commitment,
invalid offset/range/protection requests and adjacent RW-data/RX-code layout.
An emitted PC-relative function reads its adjacent writable data, as required by
the upstream dynamic interleaved-stub path. A function published by the main
thread is executed by all workers while its writer is cached. Failure injection
checks rollback of fresh chunks and writable views while retaining old code.
A mapper closed with live allocations is retired after their final release.

## Implementation boundaries

The existing CoreCLR allocator, loader heaps and JIT remain unchanged. The new
VMToOSInterface implementation uses public libnx process-memory SVCs with the
actual own-process handle supplied by the loader. Each mapper owns two 512 MiB
virtual arenas; native backing is allocated per reservation on its first commitment. The full primary reservation is initially mapped inaccessible; only
requested pages become accessible. Logical offsets
cannot overlap live allocations. Mandatory ranges and exact placement are
honored **within the owned primary arena**; other addresses fail. This is a
bounded allocator, not general fixed mmap support.

Each reservation owns contiguous aligned backing and one initial
MapProcessCodeMemory call. Fresh commit runs receive RX or RW permissions
through SetProcessMemoryPermission. Writable subviews
use MapProcessMemory against the actual RX mapping. Each request owns a
disjoint writer address range and retains its source chunks, including requests
for overlapping RX bytes. Releasing one view flushes its aliases and unmaps
only its own segments; other views remain independently writable. Partial map
failures roll back acquired segments. The original source heap remains
kernel-locked until writable aliases and primary mappings
are removed. The process handle is borrowed and never closed by the allocator.
An unrecoverable cleanup error terminates rather than freeing mapped/locked
backing. Ordinary map/protect failures roll back new work.

Process mappings avoid allocating a separate CodeMemory object per fresh
commit run.

Commitment cannot change the mode of existing chunks. RW views require fully
committed RX ranges; mixed data/code or uncommitted spans fail. The normal
CoreCLR interleaved-loader path already commits data and code separately.
CreateTemplate returns null, selecting that existing dynamic path (as Windows
does), instead of reimplementing stub generation. No managed runtime execution
is claimed by this native boundary test. PAL-wide VirtualQuery still needs a
common view of software reservations owned by other platform components.

Original adapter/test code uses official .NET and libnx 4.12 APIs. The public
libnx implementation and Atmosphere 1.11.2 KCodeMemory/slab/process-memory SVC code informed behavior
and ownership review; Atmosphere GPL code is not copied into this source.

## Reservation backing and dense commitments

Contiguous backing keeps allocator metadata out of independently mapped source
pages. A reservation allocates all its native backing on first commitment,
including bytes not yet committed in the primary arena. Budget native memory
for the reservation size, not only the accessible chunks. Each mapper's
reservation offsets remain bounded by its 512 MiB capacity.

The dense workload commits independent pages in one reservation, writes emitted
functions through views crossing chunk boundaries, executes them after writer
retirement and checks inaccessible addresses after release. Workload dimensions
are specified below; they are not captured performance results.

An optional LibnxRuntimeDiagnostic callback receives RW-map failures with
region/chunk/view counts and native process/resource counters. Those counters
describe different memory scopes; process-used bytes alone do not identify
native allocator exhaustion.

Use --output to select a dedicated generated directory; rebuilding replaces
its objects and NRO/ELF/map/NACP files. The helper takes LIBNX_ROOT from the
selected CMake cache for both compile configuration and link inputs.
Follow the [thread guide](../threads/README.md) to stage the pinned SDK and
configure coreclr-probe consistently.

## Reservation-wide primary mapping

On first commitment, the allocator maps the whole reservation as an inaccessible
primary alias of its contiguous backing. Fresh commits set RX or RW permissions
on the requested runs. This avoids a separate MapProcessCodeMemory boundary for
each small JIT commitment. Uncommitted primary pages remain inaccessible,
although the reservation's native backing is allocated.

Whole-region teardown unmaps the primary alias before freeing backing.
Rollback of fresh commit runs unmaps and recreates inaccessible aliases rather
than only changing permissions: a code alias converted to RW data cannot
regain executable capability by a permission change alone. Existing committed
chunks remain intact. Failed cleanup terminates instead of freeing locked
backing.

The dense workload uses 16,384 independent 4 KiB commitments and queries
whether their RX range coalesces into a single kernel memory block. It also
covers initial mapping failure/retry, partial RW rollback, preserved old data
and a later RX commitment to a rolled-back page.
GC commit failures report shared-pool capacity, committed/reserved bytes,
reservation count, the last SVC error and the poisoned flag through the
optional diagnostic callback. These probes and diagnostics are not managed
application compatibility guarantees.
