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

A complete embedding also requires a native unwinder with writable saved-register
locations for moving GC, executable allocation with explicit backing and alias
ownership, thread suspension and exception dispatch, and static native import
resolution. Dynamic IL loading and native module loading are separate contracts.

## Reference APIs and licenses

The adapter uses public .NET PAL and libnx definitions. Preserve notices
for copied upstream source. The vendored LLVM unwind implementation has its own notices.
Atmosphere's code-memory service implementation is a behavioral reference,
not code incorporated into this runtime.

- [.NET 10.0.12](https://github.com/dotnet/runtime/tree/v10.0.12)
- [libnx 4.12.0](https://github.com/switchbrew/libnx/tree/v4.12.0)
- [Atmosphere code-memory service](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/libraries/libmesosphere/source/svc/kern_svc_code_memory.cpp)
