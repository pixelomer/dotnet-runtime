# Managed JSON concurrency probe

These build commands target the preserved .NET 9.0.3 runtime. Use the
`horizon-net9-nativeaot` branch for this recipe; the
[managed stress guide](../managed/README.md) describes the .NET 10 probe.

Use the [filesystem probe prerequisites](../filesystem/README.md): source-built
Horizon NativeAOT SDK DLLs, pinned ILC 9.0.3, SDK 10.0.111 and source-built
native libraries. Export `ICU_NX_INSTALL_DIR` and run from the repository root:

```sh
python3 src/coreclr/nativeaot/Runtime/libnx/tests/json/build.py
```

The build retains `--noinlinetls` and produces its NRO under
`artifacts/libnx-json-test/`. The launcher writes
`sdmc:/switch/nativeaot-json-test.txt` and exits the application to HOME.
Preserve an existing log before use and use full application memory.

The workload exercises HashCode, Type.ToString, source-generated JSON
serialization/deserialization and concurrent metadata access while requesting
compacting collections. Worker tasks are awaited. This is a shared-context
probe, not coverage of arbitrary type metadata, multi-context initialization or
native interop. No renderer, game data or FMOD dependency is required.

Cold initialization uses gated worker tasks racing the main thread to the
first generated metadata request before the sequential checks. Workers perform
empty-list deserialization and request compacting collections. This shared-context
race does not reproduce arbitrary multi-context startup.
