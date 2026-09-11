#include "stdafx.h"
#include "executableallocator.h"
#include "../../hosts/inc/coreclr-libnx.h"
#include "../../minipal/libnx/doublemapping.h"
#include "corjit.h"

namespace {
struct Allocation { void* address; bool executable; };
bool Readable(DWORD protection)
{
    return protection == PAGE_READONLY || protection == PAGE_READWRITE ||
           protection == PAGE_EXECUTE_READ;
}
void StorePatch(void* address, const void* data, size_t size)
{
    // Publishing an aligned pointer must store one whole
    // aligned pointer to concurrent readers. Larger instruction sequences still
    // require caller quiescence; this is not a stop-the-world patch primitive.
    if (size == sizeof(uintptr_t) && (reinterpret_cast<uintptr_t>(address) % alignof(uintptr_t)) == 0)
    {
        uintptr_t value;
        memcpy(&value, data, sizeof(value));
        __atomic_store_n(static_cast<uintptr_t*>(address), value, __ATOMIC_RELEASE);
    }
    else memmove(address, data, size);
}
}

extern "C" void* coreclr_libnx_get_jit(void) { return getJit(); }
extern "C" void* LibnxGetJitCompileCallback();
extern "C" int LibnxSetJitCompileCallback(void* expected, void* callback);
extern "C" void* coreclr_libnx_jit_get_compile_callback(void)
{
    return getJit() ? LibnxGetJitCompileCallback() : nullptr;
}
extern "C" int coreclr_libnx_jit_set_compile_callback(void* expected, void* callback)
{
    return getJit() ? LibnxSetJitCompileCallback(expected, callback) : 0;
}
extern "C" size_t coreclr_libnx_memory_granularity(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwAllocationGranularity;
}

extern "C" int coreclr_libnx_memory_allocate(size_t size, int executable, void* low, void* high,
                                             void** handle, void** address)
{
    if (!handle || !address) return 0;
    *handle = nullptr; *address = nullptr;
    size_t granularity = coreclr_libnx_memory_granularity();
    uintptr_t lo = reinterpret_cast<uintptr_t>(low), hi = reinterpret_cast<uintptr_t>(high);
    if (!size || size % granularity || (executable != 0 && executable != 1) ||
        ((lo || hi) && (hi <= lo || size > hi - lo))) return 0;
    Allocation* allocation = static_cast<Allocation*>(malloc(sizeof(Allocation)));
    if (!allocation) return 0;
    void* memory;
    if (executable)
    {
        auto allocator = ExecutableAllocator::Instance();
        memory = hi ? allocator->ReserveWithinRange(size, low, high) : allocator->Reserve(size);
        if (memory && !allocator->Commit(memory, size, true))
        {
            allocator->Release(memory); memory = nullptr;
        }
    }
    else
    {
        memory = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        uintptr_t start = reinterpret_cast<uintptr_t>(memory);
        if (memory && hi && (start < lo || start > hi || size > hi - start))
        {
            VirtualFree(memory, 0, MEM_RELEASE); memory = nullptr;
        }
    }
    if (!memory) { free(allocation); return 0; }
    allocation->address = memory; allocation->executable = executable != 0;
    *handle = allocation; *address = memory;
    return 1;
}

extern "C" void coreclr_libnx_memory_free(void* handle)
{
    if (!handle) return;
    Allocation* allocation = static_cast<Allocation*>(handle);
    if (allocation->executable) ExecutableAllocator::Instance()->Release(allocation->address);
    else VirtualFree(allocation->address, 0, MEM_RELEASE);
    free(allocation);
}

extern "C" size_t coreclr_libnx_memory_readable(const void* address, size_t size)
{
    uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (!size || size > UINTPTR_MAX - start) return 0;
    uintptr_t current = start;
    while (current < start + size)
    {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery(reinterpret_cast<void*>(current), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || !Readable(info.Protect)) break;
        uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
        if (current < base || current - base >= info.RegionSize) break;
        size_t span = info.RegionSize - (current - base);
        size_t left = size - (current - start);
        current += span < left ? span : left;
    }
    return current - start;
}

extern "C" int coreclr_libnx_memory_patch(void* address, const void* data, void* backup, size_t size)
{
    if (!size) return 1;
    if (!data || coreclr_libnx_memory_readable(address, size) != size) return 0;
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(address, &info, sizeof(info))) return 0;
    if (info.Protect == PAGE_EXECUTE_READ)
    {
        if (!LibnxIsOwnedCodeMemory(address, size)) return 0;
        auto allocator = ExecutableAllocator::Instance();
        void* writable = allocator->MapRW(address, size, ExecutableAllocator::DoNotAddToCache);
        if (!writable) return 0;
        if (backup) memcpy(backup, address, size);
        StorePatch(writable, data, size);
        bool published = LibnxFlushCodeMemory(address, size);
        allocator->UnmapRW(writable);
        return published ? 1 : 0;
    }
    // Do not change protection across region boundaries with differing original
    // permissions. The caller can split separate data patches where appropriate.
    uintptr_t start = reinterpret_cast<uintptr_t>(address);
    uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
    if (start < base || start - base > info.RegionSize || size > info.RegionSize - (start - base)) return 0;
    DWORD previous = info.Protect;
    if (previous != PAGE_READWRITE && !VirtualProtect(address, size, PAGE_READWRITE, &previous)) return 0;
    if (backup) memcpy(backup, address, size);
    StorePatch(address, data, size);
    if (previous != PAGE_READWRITE)
    {
        DWORD ignored;
        if (!VirtualProtect(address, size, previous, &ignored)) return 0;
    }
    return 1;
}
