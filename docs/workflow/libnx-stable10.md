# Horizon interfaces on .NET 10

The upstream baseline is `v10.0.12`, commit
`4271d88e0aebf3d04f188f1334c2220d80555ef6`.
The source tree retains the public upstream ancestry and the Horizon adapters.

## NativeAOT interface changes

The context adapter uses .NET 10's `NativeContext` and `Pal` interfaces with
libnx's declared ARM64 register layout. Runtime threads retain a separate
Horizon registration owner alongside their pthread identity; registration
remains alive through suspension and is released during thread teardown.
Recursive critical sections use upstream's minipal mutex implementation.

The SDK contains four `System.Private.*` assemblies: CoreLib, TypeLoader,
Reflection.Execution and StackTraceMetadata. .NET 10 has no separate
DisabledReflection SDK assembly. Link `libaotminipal.a` together with the
version-matched workstation runtime and native BCL archives. Use ILC and
managed framework packages from .NET 10.0.12; do not mix .NET 9 native archives
with .NET 10 managed objects.

See the [managed stress guide](../../src/coreclr/nativeaot/Runtime/libnx/tests/managed/README.md)
for the .NET 10 source-build commands and runtime lifetime contract.
The other inherited probe and packaging recipes retain their .NET 9 contracts;
use `horizon-net9-nativeaot` for those recipes unless their own documentation
specifies .NET 10 support.

## CoreCLR boundary

NativeAOT's data allocator rejects executable mappings. A CoreCLR embedding
also requires a PAL, executable allocation, loading, exceptions and suspension
appropriate to Horizon. libnx JIT buffers expose separate writable and executable
addresses, requiring explicit relocation, cache synchronization and lifetime
handling. Building NativeAOT does not supply those CoreCLR interfaces.

## Affinity storage and thread identifiers

.NET 10's AffinitySet owns dynamic storage. Initialize it for all 64 possible
kernel core-mask bits before adding CPU indices. Minipal retrieves the kernel
thread ID with `svcGetThreadId`, matching the runtime thread registry rather
than using a borrowed handle as an identity.
