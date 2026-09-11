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
