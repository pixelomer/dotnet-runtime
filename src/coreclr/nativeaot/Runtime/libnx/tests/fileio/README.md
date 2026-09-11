# Horizon BCL positional I/O

The System.Native adapter serves both CoreCLR and NativeAOT. Positional I/O
uses seek/read-or-write/restore under a shared position lock. Ordinary
regular-file Read/Write, LSeek and Close use the same lock, including file
descriptions shared through duplicates. Non-regular blocking reads/writes do
not hold it. Foreign users of the same description must obey this serialization;
it is not a global libc replacement or cross-process atomicity guarantee.

Positional reads beyond file size operate at exact EOF, preserving descriptor
and access checks. Duplication validates the handle through __get_handle and
retains it with libsysbase dup. It clears stale errno and reports EMFILE when
duplication fails without setting an error. Close-on-exec is unsupported.

## Build for .NET 10

Follow the [CoreCLR thread probe](../../../../../pal/tests/libnx/threads/README.md)
for devkitPro/ICU prerequisites, pinned libnx acquisition, complete SDK staging
and CoreCLR cross-configuration. From the runtime root:

```sh
cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
  --target System.Native-Static -- -j6
python3 src/coreclr/nativeaot/Runtime/libnx/tests/fileio/build.py \
  --archive artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe/libs-native/System.Native/libSystem.Native.a \
  --libnx-root "$LIBNX_ROOT"
```

The helper accepts an explicit archive and SDK; its no-argument archive default
remains the .NET 9 output path. Use the command above for this branch.
Outputs under `artifacts/libnx-fileio-test` include NRO, ELF and map.
The log `sdmc:/switch/nativeaot-fileio-test.txt` is overwritten; preserve it
before running. The probe creates `sdmc:/switch/nativeaot-fileio-test.bin`
exclusively and deletes only its own file. It requests application exit to HOME.

## Workload

Concurrent disjoint positional reads/writes check ordinary cursor preservation,
EOF and range handling, read-only rejection and duplicate/shared-position
ownership. Exhaustion rounds retain actual duplicates until the table is full,
check EMFILE, close the duplicates and verify recovery with the original input.
This native probe does not replace managed FileStream/RandomAccess, path
normalization, asynchronous I/O or file-locking checks.
