#pragma once
#include <stddef.h>

// Experimental embedding extensions for the statically linked Horizon runtime.
// Use only between successful coreclr_initialize and coreclr_shutdown. The host
// owns import registration. Callers retain allocations/methods across all uses;
// patches require quiescent callers (or unpublished code). No atomic multi-
// instruction replacement or arbitrary native text patching is promised.
#ifdef __cplusplus
extern "C" {
#endif
void* coreclr_libnx_get_jit(void);
// Exact ICorJitCompiler::compileMethod ABI, with an explicit compiler first
// argument. Validate the JIT GUID before using these private-interface pointers.
// Compare/exchange preserves an existing hook installed by another owner. The
// caller must keep every published callback alive through runtime shutdown (or
// externally establish quiescence before releasing a replaced callback).
void* coreclr_libnx_jit_get_compile_callback(void);
int coreclr_libnx_jit_set_compile_callback(void* expected, void* callback);
size_t coreclr_libnx_memory_granularity(void);
// Bounds are [low, high), or both null for an unconstrained allocation. Size
// must be a nonzero multiple of granularity. Returns an opaque handle and base.
// All int status results are 1 on success, 0 on failure.
int coreclr_libnx_memory_allocate(size_t size, int executable, void* low, void* high,
                                 void** handle, void** address);
// Exactly once per successful allocation; requires its original opaque handle.
void coreclr_libnx_memory_free(void* handle);
size_t coreclr_libnx_memory_readable(const void* address, size_t size);
// Optional backup has room for size bytes. For RX, only runtime-owned code is
// accepted. Read-only NRO data uses the PAL protection contract and is restored.
int coreclr_libnx_memory_patch(void* address, const void* data, void* backup, size_t size);
#ifdef __cplusplus
}
#endif
