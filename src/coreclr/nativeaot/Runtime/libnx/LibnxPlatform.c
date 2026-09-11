#include "LibnxPlatform.h"
#include <switch.h>

extern char _start[], __end__[];

uint32_t LibnxGetCurrentThreadHandle(void)
{
    Thread* thread = threadGetSelf();
    return thread ? thread->handle : INVALID_HANDLE;
}

bool LibnxGetCurrentStackBounds(void** low, void** high)
{
    *low = *high = NULL;
    Thread* thread = threadGetSelf();
    if (!thread || !thread->stack_sz) return false;
    uintptr_t base = (uintptr_t)(thread->stack_mirror ? thread->stack_mirror : thread->stack_mem);
    if (!base || base > UINTPTR_MAX - thread->stack_sz) return false;
    // Check that these are the executing stack's bounds, including main-thread
    // initialization differences between loaders. Do not fabricate a stack size.
    uintptr_t sp;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    if (sp < base || sp >= base + thread->stack_sz) return false;
    *low = (void*)base;
    *high = (void*)(base + thread->stack_sz);
    return true;
}

void* LibnxGetModuleBase(void* address)
{
    uintptr_t p = (uintptr_t)address;
    return p >= (uintptr_t)_start && p < (uintptr_t)__end__ ? _start : NULL;
}

bool LibnxSetDataPermission(void* address, size_t size, int permission)
{
    if (!address || !size || ((uintptr_t)address & 4095) || (size & 4095)) return false;
    if (permission != Perm_None && permission != Perm_R && permission != Perm_Rw) return false;
    uintptr_t start = (uintptr_t)address;
    if (start > UINTPTR_MAX - size) return false;
    uintptr_t end = start + size;
    bool alreadyCorrect = true, staticCode = true;
    while (start < end)
    {
        MemoryInfo info;
        u32 pageInfo;
        if (R_FAILED(svcQueryMemory(&info, &pageInfo, start)) || !info.size || info.type == MemType_Unmapped ||
            info.addr > start || info.addr > UINTPTR_MAX - info.size ||
            info.addr + info.size <= start) return false;
        alreadyCorrect &= info.perm == (u32)permission;
        staticCode &= info.type == MemType_CodeStatic || info.type == MemType_ModuleCodeStatic;
        start = info.addr + info.size;
    }
    // svcMapMemory aliases have Stack state and cannot be reprotected. Their
    // existing RW mapping is nevertheless valid; verify every span above.
    if (alreadyCorrect) return true;
    if (R_SUCCEEDED(svcSetMemoryPermission(address, size, permission))) return true;
    if (staticCode)
    {
        // Process-memory SVCs require the loader's real process handle; the
        // CUR_PROCESS_HANDLE pseudo-handle is not accepted by this operation.
        Handle process = envGetOwnProcessHandle();
        return process != INVALID_HANDLE &&
            R_SUCCEEDED(svcSetProcessMemoryPermission(process, (u64)address, size, permission));
    }
    return false;
}

void LibnxFlushInstructionCache(void* address, size_t size)
{
    armDCacheFlush(address, size);
    armICacheInvalidate(address, size);
}
