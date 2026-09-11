# System.Native anonymous mapping probe

These build commands target the preserved .NET 9.0.3 runtime. Use the
`horizon-net9-nativeaot` branch for this recipe; the
[managed stress guide](../managed/README.md) describes the .NET 10 probe.

Build `clr.nativeaotruntime+libs.native` with the prerequisites and commands in
the [managed stress guide](../managed/README.md), then run from the runtime root:

```sh
python3 src/coreclr/nativeaot/Runtime/libnx/tests/mapping/build.py
```

This native-only probe links the System.Native archive and initializes a bounded
shared pool. Its NRO is generated under `artifacts/libnx-mapping-test/`. It writes
`sdmc:/switch/nativeaot-mapping-test.txt`, destroys its fully released pool and
returns to hbmenu. No managed GC threads are started. Preserve an existing log
before use.

The probe checks:

- Reuse of an existing healthy pool and zero-backing PROT_NONE reservations.
- Zero initialization on commit, unchanged contents on repeated commit, and
  preservation of earlier pages when committing an adjacent range.
- Rejection of unsupported protection changes without losing data.
- Rejection of partial, repeated and foreign unmaps.
- Complete release and zero initialization after backing reuse.
- Rejection of shared/file/executable mappings and zero lengths.

See [the mapping contract](../../MAPPINGS.md). NONE-to-RW commits zero pages;
RW-to-NONE is rejected because decommit would lose data and violate protection
semantics.
