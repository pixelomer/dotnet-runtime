# NativeAOT delegate callback probe

These build commands target the preserved .NET 9.0.3 runtime. Use the
`horizon-net9-nativeaot` branch for this recipe; the
[managed stress guide](../managed/README.md) describes the .NET 10 probe.

Build the Horizon native runtime and BCL using the prerequisites and commands
in the [managed stress guide](../managed/README.md). With `ICU_NX_INSTALL_DIR`
exported, run from the repository root:

```sh
python3 src/coreclr/nativeaot/Runtime/libnx/tests/callbacks/build.py
```

The build uses pinned ILC 9.0.3, SDK 10.0.111, `--noinlinetls` and source-built
Horizon native archives. Its managed BCL comes from the official 9.0.3 NativeAOT
package; no Linux native runtime is linked.

The NRO is generated under `artifacts/libnx-callback-test/`. It writes
`sdmc:/switch/nativeaot-callback-test.txt`; preserve an existing file before use.
It exits the application to HOME and must not unload back into hbmenu with live
runtime threads. Use full application memory.

## Pool and test scope

The pool has 64 pages of 255 slots (16,320 physical slots), with 256 KiB each of
static code and BSS data. Managed bookkeeping reserves a sentinel and other live
delegate types consume slots. This is a bounded process-lifetime resource, not
an arbitrary executable allocator. Dead delegates return slots through the
NativeAOT finalizer/free list. No GC heap page is made executable.

The probe exercises captured delegates invoked from C++ after collection,
function-pointer/delegate identity, callbacks from joined native pthreads,
integer and floating-point register/stack arguments, code/data permissions,
pool exhaustion and subsequent reuse.

It does not cover parallel first-time pool creation, arbitrary signature
marshalling, exceptions crossing native boundaries or callbacks racing native
deregistration. Callers must keep a delegate alive until native code has finished
using its pointer.
