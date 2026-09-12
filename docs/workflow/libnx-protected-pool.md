# Horizon protected data pool

The shared nxvm allocator provides an opt-in protected data-alias backend
through nxvm_init_protected(bytes). The default nxvm_init and implicit
initialization retain the stack-alias backend and its sparse reservations.

Select the protected pool before PAL or GC initialization. Later
nxvm_ensure_initialized calls reuse an existing healthy pool rather than
replacing it. Initialization requires the own-process handle and process-code
mapping/protection SVCs and never silently falls back to the default backend.

## Capacity and ownership

The protected backend maps the backing allocation once, converts its alias
to data and initially makes it inaccessible. Commitment enables writable
pages and zeroes fresh pages; decommit makes them inaccessible again.
Adjacent permissions can coalesce without a stack-map boundary for every
commit operation. No data page is made executable.

Virtual capacity equals physical backing size, including guard pages.
Reservations larger than that backing are not supported by this backend.
The full native backing remains allocated until pool destruction; decommit
recycles capacity within the pool rather than returning it to Horizon.

Successful rollback retains old committed pages and removes only the fresh
transaction's work. A failed decommit or cleanup retains ownership and poisons
the pool, causing later operations to fail closed. Pool destruction requires
no live reservations or committed pages and a healthy state.

PAL VirtualQuery distinguishes logical commitment from the existence of a
kernel alias for owned reservations. VirtualProtect rejects uncommitted
owned pages. Releasing a PAL reservation does not release the protected
pool's underlying no-access alias; the pool owns it until destruction.

## Probes and diagnostics

The [allocator probe](../../src/coreclr/pal/tests/libnx/protected-pool/README.md)
documents source SDK inputs, both backends, rollback and lifetime checks.
The [PAL probe](../../src/coreclr/pal/tests/libnx/virtual-memory/README.md)
has a separate protected-pool variant. The
[mapping probe](../../src/coreclr/pal/tests/libnx/data-mapping/README.md)
exercises the underlying alias and permission operations independently.

Mapping diagnostics distinguish native allocator use, process-used memory
and kernel mapping records by type and permission. Process-used bytes include
the native heap reservation and are not a count of live malloc allocations.
Both data and executable mappings can contribute to resource pressure.
Probe coverage does not establish an adequate heap budget for every application
or identify the cause of an unrelated native allocation failure.
