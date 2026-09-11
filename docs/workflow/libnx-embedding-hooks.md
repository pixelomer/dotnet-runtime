# Horizon embedded JIT integration

[coreclr-libnx.h](../../src/coreclr/hosts/inc/coreclr-libnx.h) exposes
experimental memory and JIT extensions for the statically linked runtime.
Use them only after successful coreclr_initialize and before shutdown.
The native host owns import registration.

## Owned memory

Executable allocations use ExecutableAllocator and minipal's owned RX/RW
mappings, with PAL memory queries/protection and cache publication.
Non-executable allocations use VirtualAlloc. Request a nonzero multiple of
coreclr_libnx_memory_granularity; optional bounds are [low, high), or both
null for an unconstrained allocation. Integer status results are 1 for success
and 0 for failure.

Keep every allocation and method owner alive across its uses. Free each
successful allocation exactly once with its original opaque handle, after
its users stop and before runtime shutdown. Readability and ownership queries
are snapshots, not lifetime guards.

Patches require quiescent callers or unpublished code. An aligned pointer-sized
store uses release ordering, but multi-instruction replacement is not atomic.
Unknown native RX pages are rejected. Owned executable ranges are written
through their writable alias and published before that alias is released.

For non-executable data, a patch cannot span regions with differing original
permissions. The PAL attempts to change protection and restore it afterward.
NRO data may be protected by the loader; permission changes can fail.
A failure after writing, including restoration failure, does not guarantee
rollback. An optional backup buffer must have room for the entire patch.

## Protected JIT interface

libnx locks resident NRO RELRO after relocation, including the compiler's
C++ vtable. Do not disable that loader protection. The embedding extension
exposes the current compile callback and compare/exchange installation
without modifying the vtable. The original compiler implementation is callable
without recursing through replacement dispatch.

Validate the exact JIT GUID and private compileMethod ABI, including the
explicit compiler first argument. This is not a stable cross-version API.
Callbacks, native exception wrappers and managed roots must remain alive
through shutdown unless their owners independently establish quiescence.
Atomic callback replacement does not make a replaced callback safe to free.

Reverse callbacks must obey the
[GC entry-transition contract](libnx-gc-transition-regression.md).
Ordinary GC checks, backward-edge polls and finalization remain required.

## Source build and host integration

Follow the [host recipe](../../src/coreclr/pal/tests/libnx/host/README.md)
and its linked thread guide to build the pinned libnx source, stage the SDK,
build ICU from source and configure coreclr-probe. Export ICU_NX_INSTALL_DIR
to that ICU installation. Build coreclr_static and matching CoreLib/framework
from this checkout using those recipes; do not substitute foreign runtime
archives or a mismatched CoreLib.

The host build helper accepts these optional inputs:

- --managed-source selects caller-supplied C# probe source instead of the
  built-in workload; --managed-reference adds matching IL-only assemblies.
  Reference names must not collide with CoreLib, Probe.dll or differing
  framework assemblies.
- --native-object adds native objects built from the caller's integration
  source with the same target SDK, ABI and runtime headers.
- --export-symbol retains and exports a named native host entry point.
- --dotnet-root selects an installation of the SDK version pinned in global.json.
- --managed-directory and --log-prefix select plain absolute paths under
  /switch, without device prefixes or parent traversal.

These are optional integration inputs, not dependencies on a separate probe
repository. The ordinary built-in probe requires none of them.
An optional HostResolvePInvoke function supplied by a native integration object
is registered through PINVOKE_OVERRIDE; this is explicit import resolution,
not arbitrary native-module loading.

Use a dedicated generated --output directory and keep source inputs elsewhere.
The helper checks IL format and QCall consistency and records build inputs.
Copy the generated managed directory to the selected SD destination before
running the host; rebuilding does not deploy it. Preserve existing output files
and logs before they are replaced. The path options change host deployment
and ordinary logs, not every path embedded in built-in managed workloads or
specialized tracing/progress output. Use their documented paths, or supply
integration source that matches the selected deployment.
