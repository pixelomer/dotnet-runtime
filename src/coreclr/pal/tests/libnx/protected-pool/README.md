# Shared allocator: protected data pool

This native workload links this checkout's production
src/native/libs/Common/nxvm.c and uses Horizon SVCs. It does not start CoreCLR.
Follow the [data-mapping recipe](../data-mapping/README.md) for devkitPro tools
on PATH and the linked pinned libnx source staging procedure. With LIBNX_ROOT
set to that staged SDK, run from the runtime root:

```sh
python3 src/coreclr/pal/tests/libnx/protected-pool/build.py \
  --libnx "$LIBNX_ROOT" --output artifacts/libnx-protected-pool
```

Use Python 3 and a new output directory; the helper rejects an existing one.
It produces NRO, ELF, map, NACP and a generated build-input manifest.
No managed runtime build, ICU or previously generated result is required.
The NRO needs an application-memory homebrew context and overwrites
sdmc:/switch/coreclr-protected-pool.txt before requesting return to HOME.
Preserve any existing log before running it.

See the [pool contract](../../../../../../docs/workflow/libnx-protected-pool.md)
for initialization, physical/virtual capacity, commitment and failure semantics.
The protected backend must be selected before PAL/GC initialization and
requires process-code mapping SVCs; it never silently falls back.

Each backend's workload uses a 64 MiB pool, exercises a 32 MiB reservation
with 8,192 page commitments and compares mapping coalescence. Checks cover
capacity limits, guards, invalid ranges, overlapping/idempotent commitments,
zeroed recommits, hole reuse, exact ownership accounting and eight workers
with 128 allocation lifetimes each. These are workload parameters, not
recorded results.

The permission-SVC wrapper injects initialization failure, a failed second
commit operation and a failed decommit. Successful SVC calls are forwarded.
Rollback must preserve preexisting data; failed initialization cleanup must
permit reuse. The final injected decommit failure deliberately leaves a
poisoned pool with retained ownership; later operations must fail closed.
Process teardown reclaims that final instance. The probe is not an allocator
unload/restart guarantee for a running managed application.
