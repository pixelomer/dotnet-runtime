# Horizon runtime ownership and platform boundaries

See the [Horizon hosting profile](libnx-supported-profile.md) for required inputs,
IL deployment checks and runtime lifetime limits.

## PAL mapping failure ownership

Mapping-object storage is zero-initialized without invoking a constructor.
An explicit ownership flag prevents cleanup from closing an unrelated
descriptor before creation has assigned one. A successful mapping can own
descriptor zero; its final view retirement closes that descriptor normally.

Creation retains local descriptor ownership until all fallible metadata
queries finish. Ownership then transfers to the mapping object before
registration consumes its reference. Temporary-file names belong to the
object even if a later size check fails, so cleanup does not access released
object data or close a descriptor twice.

The [startup probe](../../src/coreclr/pal/tests/libnx/startup/README.md) exercises
invalid-handle, incompatible-access, empty-file and prohibited-growth failures
using a saved/restored descriptor-zero sentinel. It checks independent
descriptors, duplication leaks and successful zero-descriptor ownership.
These changes affect CoreCLR PAL mapping, not NativeAOT's separate mapper.

## Platform boundaries

External native modules are not loaded; the adapter resolves resident NRO
symbols. CoreCLR native activation requests return ERROR_NOT_SUPPORTED.
EventPipe is disabled consistently in managed CoreLib and native CoreCLR.
Data-pool protection changes and shared writable file mappings have explicit
limits. See the [CoreCLR PAL guide](libnx-coreclr.md) for the supported adapters
and source-built probe recipes.

## Shared native descriptor exhaustion

System.Native validates the input descriptor under its shared position lock
and uses libsysbase dup to retain real ownership. It clears stale errno before
dup and reports EMFILE if duplication fails without setting an error.
The native probe exhausts the descriptor table, closes its duplicates, then
checks duplication recovery and reads the original file. CoreCLR and NativeAOT
share this implementation; the native workload does not exercise managed
asynchronous file I/O.

## Cooperative CoreCLR GC polls

Horizon uses the existing JIT GC-poll phase, helper and GC-info machinery.
Backward edges receive polls, including irreducible loops. Methods with managed
calls or tail/jump transfers receive entry polls to cover interprocedural cycles;
leaf methods avoid this entry cost. Special EH transfers use a call poll without
altering the transfer itself.

The runtime marks its own CoreLib PollGC helper as an intrinsic before compiling
it, preventing recursive instrumentation. NativeAOT code generation retains its
separate suspension policy. ReadyToRun execution is disabled on Horizon so IL
passes through the target JIT and its ABI/polling rules. This does not implement
asynchronous native activation or suspension of arbitrary external native code.

The [suspension probe](../../src/coreclr/pal/tests/libnx/host/README.md) checks
ordinary and try/catch/finally loops with live object and interior roots.
Independent non-inlined address samples check relocation; a native watchdog
releases stalled workers. It supports FullOpts and MinOpts selections.

Platform references:
[libnx thread APIs](https://github.com/switchbrew/libnx/blob/7644c9b26099aa2d2145bc72a21ee24190e92085/nx/include/switch/kernel/svc.h),
[Atmosphere debug API](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/libraries/libmesosphere/source/svc/kern_svc_debug.cpp).
These are interface/behavior references, not incorporated GPL implementation.

## Framework and directory contracts

The host's BCL workload uses the source-built Horizon framework with matching
CoreLib. It exercises files and async I/O, timers and cancellation, reflection,
generated IL, collectible assemblies, JSON, Unicode and compression. Globalization
is invariant. The native compression import resolver uses the existing static
export table and links the source-built Brotli encoder, decoder and common
archives along with zlib.

The required libnx SDK translates nonempty-directory result 0x1002 to ENOTEMPTY,
which permits the existing managed recursive deletion algorithm to operate.
The [native file-I/O probe](../../src/coreclr/nativeaot/Runtime/libnx/tests/fileio/README.md)
creates owned directories and children, verifies failure preserves a child,
then retires them and checks missing-directory errors. This SDK behavior is
shared by CoreCLR and NativeAOT.

## Async socket interests and registration identity

The shared socket engine refreshes interests after every bounded poll, including
timeouts, so a queued send cannot remain behind an old read-only snapshot.
Interrupted, transient and closed-descriptor polls retry with a fresh snapshot.

Each snapshot retains its exact SocketAsyncContext registrations. Dispatch
rejects replaced registrations and uses captured contexts rather than a later
owner of the same descriptor. All triggered batches are processed even when
the snapshot exceeds the event buffer. Snapshot references are confined to one
non-inlined poll/dispatch call so idle polling cannot retain removed contexts
indefinitely.

The [CoreCLR host](../../src/coreclr/pal/tests/libnx/host/README.md) and
[NativeAOT probe](../../src/coreclr/nativeaot/Runtime/libnx/tests/networking-async/README.md)
compile the same loopback workload: queued writes under backpressure, async
accept/connect/send/receive, cancellation and close with descriptor reuse.
These bounded scenarios do not establish all socket lifetime or idle behavior.

## Queued-operation polling and generated GC control flow

The shared socket adapter selects read/write interests only when the respective
operation queue is nonempty. Registered sockets with no queued operation are
omitted from the poll call; when none remain, the engine sleeps before taking
a fresh snapshot. Level-triggered unread data therefore does not make an idle
registration spin. New operations are picked up on the next bounded iteration.
The pending-operation snapshot uses Volatile.Read on each queue tail to acquire
publication/retirement. This is a speculative interest observation; the existing
queue lock and normal operation machinery still own completion.

Both socket hosts link SocketPollTrace.cpp, which counts calls while forwarding
every poll unchanged. The shared workload cancels a read, leaves a byte unread
for a bounded interval, then verifies that the byte remains available.
The native host reports excessive polling separately from the managed result.

The CoreCLR host's suspension-flows workload emits mutually tail-calling
DynamicMethods and an irreducible loop. Four workers retain object and interior
roots across compacting collections. The workload checks root contents,
relocation and bounded worker shutdown under the native suspension watchdog.
It uses the source-built framework and supports FullOpts and MinOpts.
See the [host recipe](../../src/coreclr/pal/tests/libnx/host/README.md).

## Tiered compilation and collectible lifetimes

The host's soak workload keeps object and interior roots live in call-free hot
loops while allocating arrays, creating short-lived workers, running finalizers
and loading/unloading collectible assembly contexts. Its own deployed Probe.dll
is the collectible assembly input; no external application data is required.

The host enables tiered and quick loop compilation. The optional trace selects
Soak:HotLoop so emitted tiers and on-stack replacement can be inspected.
Native deadlines cover complete rounds, including allocation-triggered
collections, worker retirement and unloading; round timings are not isolated
GC pauses. Managed and native memory counters describe different allocations
and are not interchangeable with process-wide memory usage.

See the [host recipe](../../src/coreclr/pal/tests/libnx/host/README.md) for the
source-built framework, selected output/deployment and overwritten log paths.
The bounded workload does not establish unlimited runtime lifetime.
