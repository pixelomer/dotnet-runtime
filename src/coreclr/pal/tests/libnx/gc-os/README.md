# Shared Horizon GC OS boundary

This build helper requires devkitPro at `/opt/devkitpro`. Follow the
[thread probe](../threads/README.md) for toolchain/ICU prerequisites, the pinned
libnx source, staged SDK and CoreCLR cross-configuration using that installation.
From the runtime root:

```sh
cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
  --target gc_pal coreclrpal_objects -- -j6
python3 src/coreclr/pal/tests/libnx/gc-os/build.py
```

The script links the production shared GC OS object and nxvm allocator using
the actual GC environment headers and compiler flags. It reads LIBNX_ROOT from
CMakeCache. No managed runtime or replacement GC algorithms are linked.

Outputs are in `artifacts/libnx-gc-os`. The NRO overwrites
`sdmc:/switch/coreclr-gc-os-probe.txt` and requests application exit to HOME;
preserve any existing log before running it.

The workload checks process/CPU identities, restoration of allowed affinity
after restricted configurations, aligned reserve/commit/decommit/release,
advisory-reset range validation, data preservation and zero-filled recommit.
Each round retires its reservations and committed pages; final shutdown retires
the empty shared pool. These are native ownership checks, not managed execution.
