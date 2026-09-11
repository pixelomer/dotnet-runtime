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
virtual arenas; physical backing is allocated only on commit. Logical offsets
cannot overlap live allocations. Mandatory ranges and exact placement are
honored **within the owned primary arena**; other addresses fail. This is a
bounded allocator, not general fixed mmap support.

Each fresh commit run owns aligned backing mapped through MapProcessCodeMemory
and protected RX or RW through SetProcessMemoryPermission. Writable subviews
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
