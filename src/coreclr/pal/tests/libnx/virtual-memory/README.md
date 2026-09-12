# Horizon PAL virtual-memory probe

Build from the runtime root after the
[CoreCLR cross configure](../context/README.md):

```sh
python3 src/coreclr/pal/tests/libnx/virtual-memory/build.py
```

This links production PAL virtual.cpp, error.cpp and sysinfo.cpp, minipal CPU
count and the existing shared nxvm allocator. No runtime-success stubs are
linked. Generated NRO, ELF, symbols and map are under `artifacts/libnx-coreclr-vm`.

The native test checks kernel-derived CPU/address limits, shared pool ownership,
reservation larger than backing, quota failure retaining previously committed
contents, alignment/overflow/boundary rejection, verified protection, decommit
and zero-filled reuse. Four concurrent workers perform 4,096 allocation lifetimes
across 64 thread lifetimes; round boundaries require all backing and reservations
returned. Each fresh/recommitted two-page span is inspected completely. The
released-address query runs before workers start, avoiding address-reuse races.
The log is `sdmc:/switch/coreclr-vm-probe.txt`; completion returns to HOME.
Preserve an existing log before running the probe.

## Explicit scope

This is the data allocator, not the required CodeMemory JIT backend. The bounded
pool is shared with System.Native and decommit recycles its backing rather than
returning the backing pool to Horizon. Only writable commitment is supported;
fixed placement and protection changes on Stack aliases fail explicitly.
Kernel queries are intersected with PAL reservation ownership. Uncommitted
reservations created directly by other nxvm consumers currently lack a shared
query API and may appear free to VirtualQuery. General file/shared mapping,
executable mapping and full CoreCLR startup remain separate work.

Original adapter/test code uses official .NET PAL and public libnx APIs.
No Nintendo
SDK material or third-party commercial runtime is used.

## Optional protected pool control

After the same source SDK staging and PAL cross-configuration, run:

```sh
python3 src/coreclr/pal/tests/libnx/virtual-memory/build.py --protected-pool
```

This selects a 32 MiB protected data pool and writes generated files under
artifacts/libnx-coreclr-vm-protected. The default variant retains its 8 MiB
backing pool and larger sparse reservations. Rebuilding replaces generated
files in the corresponding directory. Both variants overwrite the same
SD log; preserve it between runs.

Queries distinguish logical reservations from kernel aliases, and protecting
uncommitted owned pages must fail. After PAL reservation release, the protected
pool still owns its no-access kernel alias until pool destruction; the default
backend returns an unmapped span. The variant does not broaden supported
protection operations for PAL-owned data.

The [allocator probe](../protected-pool/README.md) separately covers the shared
pool's initialization, rollback and fail-closed ownership contract.
