# Horizon CoreCLR thread boundary

Follow the toolchain and ICU prerequisites in the
[context probe](../context/README.md). This probe also needs the libnx
`pthreadGetNativeHandle` extension. From the runtime root, obtain and stage the matching
[libnx source](https://github.com/pixelomer/libnx) without replacing the installed
SDK:

```sh
git clone https://github.com/pixelomer/libnx.git artifacts/libnx-source
git -C artifacts/libnx-source checkout --detach cb5645686f762feb9e3a8c21cd683b1044df615c
make -C artifacts/libnx-source/nx install DESTDIR="$PWD/artifacts/libnx-sdk" -j6
LIBNX_ROOT="$PWD/artifacts/libnx-sdk$DEVKITPRO/libnx"
ROOTFS_DIR="$DEVKITPRO" src/coreclr/build-runtime.sh \
  -arm64 -release -cross -os libnx \
  -subdir coreclr-probe -component runtime -configureonly \
  -cmakeargs '-DFEATURE_EVENT_TRACE=OFF' \
  -cmakeargs '-DFEATURE_PERFTRACING=OFF' \
  -cmakeargs "-DLIBNX_ROOT=$LIBNX_ROOT"
python3 src/coreclr/pal/tests/libnx/threads/build.py
```

`DEVKITPRO` must be the absolute installation prefix. The install target stages
the complete SDK, including BSD headers; the raw source include directory is
not a substitute.

The probe links production `threadplatform.cpp` and the shared native reaper,
not the complete PAL thread-management layer. Outputs are in
`artifacts/libnx-coreclr-threads`, including NRO, ELF and linker map. It writes
`sdmc:/switch/coreclr-thread-probe.txt` and requests application exit to HOME.
Preserve an existing log before running it.

The workload alternates the legacy and attribute-preserving reaper interfaces,
normal return and `pthread_exit`. Late TLS cleanup blocks after completion is
queued, checking that the reaper retains the executing stack and handle until
kernel termination. Each worker has a bounded wait.

Stack bounds come from the current libnx Thread and are checked against SP.
Bootstrap storage is outside the usable stack. Requests vary from 128 to
320 KiB; checks compare relative increments without assuming a private
bootstrap structure size.

Priority changes use a native handle borrowed from libnx's pthread driver.
PAL relative priorities map around the existing worker baseline 0x3b, with
reversed Horizon numbering and clamping to the process priority mask. Invalid
priorities fail. The probe changes only its own workers' priorities.

The shared reaper and completion TLS key remain until process exit. This native
boundary probe does not initialize managed CoreCLR or unload the runtime.
