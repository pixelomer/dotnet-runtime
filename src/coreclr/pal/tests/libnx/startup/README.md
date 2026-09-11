# Horizon full PAL startup and synchronization

Follow the [thread probe](../threads/README.md) for devkitPro/ICU prerequisites,
the pinned libnx source with fsdevPread/pthreadGetNativeHandle, SDK staging and
CoreCLR cross-configuration. From the runtime root:

```sh
cmake --build artifacts/obj/coreclr/libnx.arm64.Release/coreclr-probe \
  --target coreclrminipal minipal coreclrpal_objects -- -j6
python3 src/coreclr/pal/tests/libnx/startup/build.py
```

All objects in the current PAL target must exist. The script archives those
objects and links the two minipal libraries using the staged SDK in CMakeCache.
Outputs are in `artifacts/libnx-coreclr-startup`, including ELF, map and NRO.
The NRO overwrites `sdmc:/switch/coreclr-startup-probe.txt` and requests
application exit to HOME. Preserve an existing log before running it. The file checks also overwrite and
then remove `sdmc:/switch/coreclr-pal-file-input.bin`; preserve an existing file
at that path before running the probe.

The probe calls PAL_InitializeCoreCLR(argv[0], TRUE), including the object
manager, synchronization worker, initial PAL thread, memory allocators, module
bookkeeping and exceptions. It creates PAL thread/event objects with
CREATE_SUSPENDED, checks the startup gate, resumes once, rejects repeated resume,
waits for event/thread completion, reads the retained internal PAL exit record
and closes handles. The exit-code check uses the real internal object interface.

Horizon reuses PAL's resume semaphore and replaces the worker-command pipe with
a byte queue protected by native mutex/condition-variable synchronization.
The workload checks FIFO order, bounded backpressure, nonblocking/timed reads,
concurrent wakeups, drain-before-EOF, rejected writes after closure and shutdown
of blocked readers. Shutdown parking uses a condition variable, not polling
an unavailable descriptor backend.

Process identity comes from svcGetProcessId. Unix session IDs are unavailable;
foreign-process handles, monitoring and subprocess creation are rejected.
minipal_getexepath resolves the homebrew loader's executable path and is shared
with the resident-module adapter.

The shared nxvm backing pool, synchronization worker, native reaper and PAL
caches have process lifetime. The probe covers native PAL startup and shutdown,
not managed CoreCLR initialization, managed GC/JIT/EH, runtime unload or restart.

The probe also checks PAL_ProbeMemory across committed, uncommitted, retired and
overflowed ranges without changing caller bytes. Named mutexes fail explicitly;
unnamed mutexes retain their normal ownership and synchronization.

Resident read-only data initialization/restoration uses the actual kernel
code-to-data transition within that NRO segment. Attempts to make executable
text writable are rejected. The separate GC/data-pool protection limitation
remains explicit.
