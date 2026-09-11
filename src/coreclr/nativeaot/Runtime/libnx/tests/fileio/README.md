# Horizon BCL positional I/O

The System.Native libnx adapter implements positional I/O using
seek/read-or-write/restore under a shared position lock. Ordinary regular-file
Read/Write, LSeek and Close use the same lock, including file descriptions shared
through duplicate descriptors. Non-regular blocking Read/Write do not hold it.
Foreign native users of the same file description must not bypass this
serialization. This is not a global libc replacement and does not provide
atomicity against unrelated processes modifying or truncating the file.

Positional reads beyond the file size are performed at exact EOF to return zero
while retaining the underlying read's descriptor/access checks.

Descriptor duplication uses libsysbase `dup`, validating the input through
`__get_handle` first and retaining shared handle ownership. The adapter does not
treat libnx's positive unsupported-command result from `fcntl` as a descriptor.
Close-on-exec is not supported.

## Build and scope

Use the source prerequisites in the [PAL probe guide](../pal/README.md).
From the runtime root, build the native library subset and link the probe:

```sh
ROOTFS_DIR="$DEVKITPRO" ./build.sh -s libs.native -c Release \
  --cross -a arm64 --os libnx
python3 src/coreclr/nativeaot/Runtime/libnx/tests/fileio/build.py
```

Outputs are under `artifacts/libnx-fileio-test/`, including
`nativeaot-fileio-test.nro`, its ELF and map. The log is
`sdmc:/switch/nativeaot-fileio-test.txt`; preserve any existing file before use.
The probe creates `nativeaot-fileio-test.bin` exclusively and deletes only its
own file. No game files are used.

The workload exercises concurrent disjoint positional writes/reads, preserved
ordinary file position, exact/beyond/partial EOF, negative offsets,
Read/Write/LSeek, read-only write rejection, descriptor duplication/shared
position, independent closing and invalid descriptors. It does not replace
managed FileStream/RandomAccess, path-normalization or file-locking tests.
