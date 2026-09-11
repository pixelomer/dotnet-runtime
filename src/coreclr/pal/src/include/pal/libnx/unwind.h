#ifndef _PAL_LIBNX_UNWIND_H_
#define _PAL_LIBNX_UNWIND_H_

namespace CorUnix
{
// A caller-owned, loaded native image. Memory must stay mapped throughout the
// unwind. JIT-managed frames are decoded by CoreCLR's managed code manager.
struct LibnxUnwindSections
{
    uintptr_t codeStart;
    uintptr_t codeEnd;
    uintptr_t ehFrame;
    size_t ehFrameSize;
    uintptr_t ehFrameHeader;
    size_t ehFrameHeaderSize;
};

// On failure, context and contextPointers are unchanged. Non-null output homes
// point into the original stack, never into an unwinder temporary. A null home
// denotes a register that was not recovered from memory in this step.
BOOL LibnxUnwindFrame(CONTEXT* context, KNONVOLATILE_CONTEXT_POINTERS* contextPointers,
                     const LibnxUnwindSections& sections);
BOOL LibnxUnwindNativeFrame(CONTEXT* context, KNONVOLATILE_CONTEXT_POINTERS* contextPointers);
}
#endif
