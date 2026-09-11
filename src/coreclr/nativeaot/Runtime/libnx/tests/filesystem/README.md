# Horizon managed filesystem probe

Build the native runtime/BCL and matching managed NativeAOT libraries using
the [managed library guide](../../MANAGED_LIBRARIES.md), with the standalone
SDK and toolchain prerequisites from the [managed stress guide](../managed/README.md).
Export `ICU_NX_INSTALL_DIR`, then run from the repository root:

```sh
python3 src/coreclr/nativeaot/Runtime/libnx/tests/filesystem/build.py
```

The build supplies source-built Horizon aotsdk DLLs through `IlcSdkPath`, using
pinned ILC 9.0.3 and SDK 10.0.111. Outputs are under
`artifacts/libnx-filesystem-test/`.

The probe uses its own directory, `sdmc:/switch/nativeaot-filesystem-test`,
refusing to start if it already exists. It deletes that directory on success,
writes `sdmc:/switch/nativeaot-filesystem-test.txt`, and exits to HOME. Preserve
any existing log before use. Do not unload NativeAOT back into hbmenu.

It exercises:

1. sdmc/romfs root recognition and dot/parent segment normalization.
2. Directory creation, current-directory changes, relative paths and byte-exact
   File.WriteAllBytes/ReadAllBytes.
3. FileStream seek/read/flush and RandomAccess I/O preserving stream position.
4. File.Copy, File.Move, wildcard enumeration, FileInfo length and destination data.
5. File/directory deletion and current-directory restoration.

The native CopyFile wrapper tolerates ENOSYS/ENOTSUP only for optional mode
copying on Horizon, alongside its existing EPERM handling. Direct chmod/fchmod
APIs still fail; no POSIX permissions are synthesized. The platform's fchmod
implementation is in [fs_dev.c](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/runtime/devices/fs_dev.c).

This probe does not cover cross-process file locking, asynchronous file APIs,
crash-safe replacement, timestamp preservation, symlinks, sockets or arbitrary
mounted device names. [Native positional I/O](../fileio/README.md) has a separate
concurrency probe.
