# Shared Horizon anonymous memory mappings

NativeAOT GC virtual memory and System.Native anonymous mappings use the same
`src/native/libs/Common/nxvm.c` pool. Each archive contains the shared
implementation; static archive resolution links one implementation into the
NRO. The runtime-local nxvm.c/h paths remain compatibility includes for probes.

System.Native supports private anonymous mappings with null or ignored address
hints, fd=-1, offset=0 and NONE or RW protection. Lengths round up to 4 KiB.
NONE reserves an address range; RW commits zeroed backing pages. MProtect(RW)
commits additional pages or preserves already committed contents. Full MUnmap
releases the owned reservation and backing.

The default shared budget is 512 MiB; an existing healthy pool is reused.
Backing and reservation ownership remain bounded, and failed kernel unmaps
retain ownership rather than freeing live mappings.

Unsupported operations report errors: file/shared/fixed mappings,
executable/read-only protection, RW-to-NONE protection and partial/foreign
unmaps. MProtect cannot be implemented by decommit because protection changes
must preserve data. This adapter's Stack-state aliases do not provide arbitrary
reprotection. This is not the full MemoryMappedFile or POSIX mmap API.

## Frozen object heap

NativeAOT's FrozenObjectHeapManager uses Interop.Sys.MMap(PROT_NONE) and
MProtect(RW) to create RuntimeType objects. Uninitialized malloc storage and a
no-op protection function do not satisfy that contract. The shared allocator
provides zeroed, owned mappings for the BCL as well as the GC.

The [native mapping probe](tests/mapping/README.md) exercises reservation,
commit, reuse, protection rejection and release independently of managed GC.
