# Horizon mapping coalescence probe

This standalone native workload uses public libnx SVCs without starting
CoreCLR. Follow the [thread guide](../threads/README.md) for the devkitPro
toolchain and the pinned, source-built libnx SDK staging procedure.
It does not require a managed runtime build or ICU.

With DEVKITPRO and LIBNX_ROOT set as in that guide, run from the runtime root:

```sh
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
python3 src/coreclr/pal/tests/libnx/data-mapping/build.py \
  --libnx "$LIBNX_ROOT" --output artifacts/libnx-data-mapping
```

Use Python 3 and a new output directory: the helper rejects an existing one.
It invokes aarch64-none-elf-gcc, nacptool and elf2nro from PATH, linking the
staged SDK's libnx.a with its switch.specs. Outputs include NRO, ELF, map,
NACP and a generated input manifest. The manifest is produced by the build,
not a prerequisite or a checked-in result.

Run the NRO in an application-memory homebrew context. It overwrites
sdmc:/switch/coreclr-data-mapping.txt and requests return to HOME; preserve
any existing log first.

The workload allocates 32 MiB of page-aligned backing. First it maps separate
4 KiB stack aliases, records the mapping boundary count, then unmaps them
and checks preserved bytes. Resource exhaustion in that phase is recorded
as a limit, not treated as an expected-success check.

Next it maps one process-code alias for the whole backing, converts it to
RW data and makes it inaccessible. It changes each page to RW separately
and checks whether adjacent pages coalesce into one kernel memory block.
It checks read-only, no-access and writable-again transitions, fresh clearing,
re-coalescence and whole-alias release. A nonzero failure count produces a
nonzero result. Inspect diagnostics as well as the final count because early
allocation or SVC failures can return before the final summary.

This probes native mapping and permission behavior. It does not establish
protected-pool allocator integration or managed application compatibility.
