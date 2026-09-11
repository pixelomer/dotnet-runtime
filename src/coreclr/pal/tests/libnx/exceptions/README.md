# Horizon PAL exception dispatch

Follow the [thread probe](../threads/README.md) for devkitPro/ICU prerequisites,
the pinned libnx source with pthreadGetNativeHandle/fsdevPread, complete SDK
staging and CoreCLR cross-configuration. From the runtime root, build the native
inputs and then this probe:

```sh
cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
  --target coreclrminipal minipal -- -j6
cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
  --target coreclrpal_objects -- -k -j6
python3 src/coreclr/pal/tests/libnx/exceptions/build.py
```

The probe archives available objects belonging to the current PAL target and
links only the exception/context/unwind dependencies plus the two minipal
archives. The keep-going object build can report failures in unrelated PAL
units; every dependency actually linked by the probe must resolve. Native
module loading is outside this probe. It uses the vendored LLVM DWARF decoder
and the actual PAL types, heap records and dispatch implementation.

Outputs are in `artifacts/libnx-coreclr-exceptions`. The NRO writes
`sdmc:/switch/coreclr-exception-probe.txt` and requests application exit to HOME;
preserve any existing log. It initializes the PAL TLS key with no CPalThread
value, not the complete PAL thread-object or managed runtime lifecycle.

The bridge captures the native user-exception frame and unbanked ARM64
registers. Initial C work uses a per-thread emergency stack after kernel
exception mode ends. Ordinary dispatch runs below the original SP, retaining
SEHProcessException, record promotion and the virtual-unwind transition.
Nested faults use independent interrupted-stack frames. Recoverable
continuations use RestoreCompleteContext and Horizon ReturnFromException to
preserve X16/X17 as well as the rest of the saved context.

The workload performs outer and nested read faults across workers. It checks
ESR/FAR translation, heap record promotion, original-stack dispatch, stack walks
to the interrupted context, handler edits to PC/X0, and integer/SIMD restoration.
It does not exercise write/instruction/alignment faults, stack overflow,
nonreturning managed throws, full PAL startup or managed GC activation.

Activation returns ERROR_NOT_SUPPORTED. SEH-enabled threads join the shared
Horizon pause/context registry; process write-buffer flushing uses that registry,
not a local fence alone. Cross-process advisory file locks and writable shared
file mappings fail explicitly.
