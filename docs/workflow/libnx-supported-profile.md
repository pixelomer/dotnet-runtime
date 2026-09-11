# Horizon .NET 10 hosting profile

This experimental platform port embeds CoreCLR/RyuJIT in an ARM64 NRO.
The following contract describes its source-built inputs and platform limits;
it is not a general .NET compatibility guarantee.

## Source inputs

Use the .NET 10.0.12 lineage based on public upstream commit
`4271d88e0aebf3d04f188f1334c2220d80555ef6`, with this checkout's Horizon
changes. Build native CoreCLR, matching CoreLib and the platform framework
together. Do not substitute a foreign runtime pack or mismatched CoreLib.

The [host recipe](../../src/coreclr/pal/tests/libnx/host/README.md) documents
the source-build commands, pinned source SDK, Python tooling and ICU
prerequisites. Follow its linked [thread guide](../../src/coreclr/pal/tests/libnx/threads/README.md)
to build and stage the required libnx revision. The SDK includes the descriptor,
native-thread-handle and nonempty-directory errno behavior used by the adapters.
No private SDK directory or prebuilt validation payload is required.

The NativeAOT managed SDK and runtime have their own
[source-build recipe](../../src/coreclr/nativeaot/Runtime/libnx/tests/managed/README.md).
Use version-matched native archives and managed assemblies; .NET 9 branch
recipes do not establish a .NET 10 SDK contract.

## Embedding and deployment

Embed the static runtime/JIT with its native unwind descriptors and explicit
native imports. Keep the runtime's thread registration, soft-TLS ABI and
exception-entry/unwind integration. Executable mappings use owned backing
and separate writable/executable views with cache publication through Horizon
APIs; ordinary heap pages cannot be made arbitrarily executable.

Deploy source-built Horizon IL CoreLib/framework and IL application assemblies.
Set `PublishReadyToRun=false`.
The [IL-format checker](../../src/coreclr/pal/tests/libnx/host/validate-il.py)
requires pefile and rejects native headers, mixed-code inputs and incompatible
PE/architecture flags. It checks deployment format, not API compatibility or
trustworthiness. The separate QCall checker verifies managed/native entry-table
consistency without executing the input.

The host must initialize services used by its application, including BSD for
sockets, and link matching native BCL libraries. Compression requires zlib and
the Brotli encoder, decoder and common archives. Full globalization additionally
requires source-built ICU and application-owned ICU data with the lifetime
specified by the managed NativeAOT guide; the broad BCL probe uses invariant
globalization. Loopback probes do not establish external network or TLS support.

Use the existing embedded-host lifecycle and stop application work before
shutdown. CoreCLR shutdown is not an unload/reinitialization interface.
NativeAOT and its runtime threads remain resident until process exit.
BSD must remain available while the managed socket engine is alive.

## Runtime boundaries

CoreCLR's Horizon JIT uses cooperative polls on backward edges and potentially
cyclic method entries through the existing GC helper and GC-info machinery.
NativeAOT has a separate rendezvous implementation. Neither supplies arbitrary
asynchronous native-thread activation. Native code that blocks indefinitely
without a supported GC transition is outside this suspension contract.

The source probes exercise workstation-GC scenarios, worker/TLS/finalizer
lifetimes, moving roots, emitted IL and collectible contexts. They are reusable
workloads, not an exhaustive guarantee for server/background GC or every
application. See the [ownership and platform guide](libnx-stability.md) for
descriptor, mapping and socket contracts and the linked probe recipes.

External native module loading is unsupported. The adapter resolves resident
NRO exports and explicitly linked import tables, not arbitrary dlopen/NSO/ELF
modules. Managed IL assembly loading is a separate interface.

Native ReadyToRun execution is disabled, even when requested by an environment
option. Retained IL does not remove a foreign image's identity, layout and
protection requirements; use IL-only deployment inputs. The host's source-owned
ReadyToRun fixture is an explicit unsupported-format control, not an application
dependency or a promise of fallback support.

EventPipe is disabled consistently in managed CoreLib and native CoreCLR.
No diagnostic IPC/profiler transport is provided. Native symbols, link maps,
runtime output and optional JIT disassembly remain available to developers.

Shared writable file maps, arbitrary data-pool protection changes, Unix process
creation/signals/named IPC and desktop-specific facilities are outside this
platform contract. Unsupported operations must report their defined failures.
Cryptography/TLS, broader networking and globalization requirements need
application-specific compatibility review.

## Sources and licenses

Retain the .NET source licenses and the vendored LLVM unwinder's notices.
The adapter uses public libnx/devkitPro interfaces; the
[CoreCLR guide](libnx-coreclr.md) links the public API and code-memory references.
Those references do not change the licenses of the implementation sources.
