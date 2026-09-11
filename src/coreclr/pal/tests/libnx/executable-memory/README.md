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
partial commitment, overlapping idempotent commits, views crossing CodeMemory
objects, overlapping view references, real final-owner unmapping, unchanged RX
execution after writer retirement, zero initialization after kernel creation,
invalid offset/range/protection requests and adjacent RW-data/RX-code layout.
An emitted PC-relative function reads its adjacent writable data, as required by
the upstream dynamic interleaved-stub path. A function published by the main
thread is executed by all workers while its writer is cached. Failure injection
checks rollback of fresh chunks and writable views while retaining old code.
A mapper closed with live allocations is retired after their final release.

## Implementation boundaries

The existing CoreCLR allocator, loader heaps and JIT remain unchanged. The new
VMToOSInterface implementation uses public libnx CodeMemory and reservation APIs.
Each mapper owns two 512 MiB virtual arenas; physical backing is allocated only
on commit. Logical offsets cannot overlap live allocations. Mandatory ranges
and exact placement are honored **within the owned primary arena**; other
addresses fail. This is a bounded allocator, not general fixed mmap support.

Each contiguous fresh commit run owns one CodeMemory object. Writable subviews
map whole objects into the corresponding contiguous writable arena and track
references per object; the last reference unmaps that object's writable alias.
The original source heap remains kernel-locked until both mappings are removed
and the handle is closed. An unrecoverable cleanup error terminates rather than
freeing mapped/locked backing. Ordinary create/map failures roll back new work.

Commitment cannot change the mode of existing chunks. RW views require fully
committed RX ranges; mixed data/code or uncommitted spans fail. The normal
CoreCLR interleaved-loader path already commits data and code separately.
CreateTemplate returns null, selecting that existing dynamic path (as Windows
does), instead of reimplementing stub generation. No managed runtime execution
is claimed by this native boundary test. PAL-wide VirtualQuery still needs a
common view of software reservations owned by other platform components.

Original adapter/test code uses official .NET and libnx 4.12 APIs. The public
libnx implementation and Atmosphere 1.11.2 KCodeMemory/SVC code informed behavior
and ownership review; Atmosphere GPL code is not copied into this source.
