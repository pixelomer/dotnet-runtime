# Horizon CoreCLR native unwind test

From the runtime root after the [CoreCLR cross configure](../context/README.md):

```sh
python3 src/coreclr/pal/tests/libnx/unwind/build.py
```

The test links the production PAL DWARF adapter with the official .NET-vendored
LLVM decoder and original AArch64 assembly. Outputs ELF, NRO and map under
`artifacts/libnx-coreclr-unwind/`. The linker fragment adds EH-frame boundaries
to the installed libnx NRO layout; `--eh-frame-hdr` supplies the search index.
Logs go to `sdmc:/switch/coreclr-unwind-probe.txt`; normal exit goes to HOME.
Preserve an existing log before running the probe.

The inner assembly function spills X19-X28, FP/LR and D8-D15 with explicit CFI,
changes its live registers, and gives the callback a snapshot of that frame.
The callback checks recovered values and exact stack homes. It updates the X19
and D8 homes; the actual epilogue restores them and the outer function verifies
the new values. This tests the writable-home contract needed by moving GC,
without claiming an actual managed collector test.

Both indexed lookup and EH-frame scanning are checked, plus leaf/first-instruction
contexts, undefined registers, failed lookup preserving outputs, and 4,096 frame
returns over 64 pthread lifetimes. The undefined-register case also exercises a
fix to the shared decoder's previously uninitialized saved-location output.
No GNU libunwind, Linux ucontext ABI, external game shim or proprietary Nintendo
SDK/tool code is used. Retain the vendored LLVM Apache-2.0-with-LLVM-exception
license and .NET MIT notices.

Native-image data must be loaded, trusted and remain mapped through the unwind;
this is an in-process unwinder, not a hostile unwind-metadata parser. The first
native embedding is a static NRO. Unknown module/code addresses fail cleanly;
future native module loading must pass the corresponding image sections.
JIT managed-frame unwind metadata is handled by CoreCLR's managed code manager.
PAL fault trampolines and full runtime/GC/EH integration remain separate tests.
