# Horizon runtime ownership and platform boundaries

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
