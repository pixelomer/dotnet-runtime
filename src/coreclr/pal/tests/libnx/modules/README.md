# Horizon PAL resident native modules

Follow the [thread probe](../threads/README.md) for devkitPro/ICU prerequisites,
the pinned libnx source with pthreadGetNativeHandle/fsdevPread, complete SDK
staging and CoreCLR cross-configuration. From the runtime root, build the native
inputs and then this probe:

```sh
cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
  --target coreclrminipal minipal -- -j6
cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
  --target coreclrpal_objects -- -j6
python3 src/coreclr/pal/tests/libnx/modules/build.py
```

All objects in the current PAL target must exist. The build script links actual
PAL Release objects and exports its probe function/data with
`--export-dynamic-symbol`. Production hosts must explicitly retain their
intended native entry points too; lookup cannot discover stripped symbols.

The adapter retains PAL module management and replaces its native OS calls.
It uses the resident NRO's segment header, section-bound address, loader argv
path and System V ELF dynamic symbol/hash table. Hidden, undefined, TLS and
unsupported symbol kinds are rejected. Lookup errors are thread-local and
consumable. External native module loading is unsupported; the homebrew loader
owns the resident NRO's lifetime. Dynamic managed IL loading is separate.

PAL_CopyModuleData retains the native layout, including BSS, and validates the
complete destination extent before copying. Heap and JIT memory are not
reported as native modules. The executable path is copied once for process
lifetime. Linker metadata uses hidden PC-relative declarations and __code_start,
not a GOT reference to the absolute __start__ symbol.

Outputs are in `artifacts/libnx-coreclr-modules`. The NRO writes
`sdmc:/switch/coreclr-module-probe.txt` and requests application exit to HOME;
preserve any existing log. The probe initializes the PAL TLS key without a
CPalThread value and does not perform full PAL or managed runtime initialization.

The workload checks exported function execution and data identity, hidden-symbol
rejection, per-thread error consumption, invalid handles, reopening the actual
executable path and bounded NRO/BSS snapshots. No proprietary module inputs
or private source artifacts are required.
