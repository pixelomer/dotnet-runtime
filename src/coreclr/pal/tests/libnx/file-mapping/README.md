# Horizon PAL file-mapping boundary

Follow the toolchain and ICU prerequisites in the
[context probe](../context/README.md). This probe also needs the libnx
`fsdevPread` extension. From the runtime root, obtain and stage the matching
[libnx source](https://github.com/pixelomer/libnx) without replacing the installed
SDK:

```sh
git clone https://github.com/pixelomer/libnx.git artifacts/libnx-source
git -C artifacts/libnx-source checkout --detach 99ad201ee1bf465ebbd9f18f826f19969f8deb4a
make -C artifacts/libnx-source/nx install DESTDIR="$PWD/artifacts/libnx-sdk" -j6
LIBNX_ROOT="$PWD/artifacts/libnx-sdk$DEVKITPRO/libnx"
ROOTFS_DIR="$DEVKITPRO" src/coreclr/build-runtime.sh \
  -arm64 -release -cross -os libnx \
  -subdir coreclr-probe -component runtime -configureonly \
  -cmakeargs '-DFEATURE_EVENT_TRACE=OFF' \
  -cmakeargs '-DFEATURE_PERFTRACING=OFF' \
  -cmakeargs "-DLIBNX_ROOT=$LIBNX_ROOT"
python3 src/coreclr/pal/tests/libnx/file-mapping/build.py
```

`DEVKITPRO` must be the absolute installation prefix. The install target stages
the complete SDK, including BSD headers; the raw source include directory is
not a substitute.

The test uses production NativeMap/Unmap/Protect, PAL VirtualProtect and the
newlib pread bridge to libnx fsdevPread. It creates only its own pattern file at
`/switch/coreclr-filemap-probe.bin` and logs to `/switch/coreclr-filemap-probe.txt`.
It performs 1,024 cycles over 32 threads: section placement into a reserved
image, guard holes, partial backing retirement, private edits/protection,
no-access restoration, private section RW-to-RX execution, final-page loading/zero padding, invalid/unsupported
requests, and cleanup of images which failed before recording a section. It
checks that positional reads preserve the descriptor's initial position and
that private writes never change the input file. NRO/ELF/map are generated in
`artifacts/libnx-coreclr-filemap`. The probe overwrites its pattern file and log;
preserve existing files at those paths before running it.

`host-adapter.cpp` checks the Unix inline adapter using real mmap/mprotect/munmap;
it can be compiled with a host C++ compiler and the PAL source include path.

## Scope and compromises

CoreCLR's map.cpp retains its PE section parsing and mapping-object/reference
handling. Only OS mapping operations are routed through mapnative.h. Horizon
uses buffered file snapshots and actual process-memory mapping/protection APIs.
There is no kernel file pager or promise that subsequent file writes become
visible in a read-only view. Shared writable file views fail; unsupported
live fixed-map replacement fails without destroying old contents. File length
rounding loads the whole final page and zero-fills only beyond EOF. Whole pages
past EOF fail instead of pretending to provide a SIGBUS pager fault.

Each source backing stays owned until its last mapped page is unmapped. Image
cleanup retires unrecorded gaps/padding and reservations left by early failure.
POSIX discard advice keeps pinned private contents intact. PAL-wide reservation
queries and general shared-file coherence remain separate integration work.
The positional-read bridge supports fsdev descriptors; other drivers, including
romfs, currently fail explicitly. No seek/restore workaround is used.

The adapter/test code and libnx driver extension use official public Horizon
filesystem operations. No proprietary SDK code.
This is a native platform-boundary test, not managed assembly loading/startup.
