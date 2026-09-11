#include "pal.h"
namespace CorUnix { class CPalThread; }
#include "pal/virtual.h"
#include "pal/mapnative.h"
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/kernel/mutex.h>
#include <switch/result.h>
#include <libs/Common/nxvm.h>
}

namespace
{
constexpr size_t Page = 4096;
struct Region
{
    Region* next;
    uintptr_t base;
    size_t size;
    DWORD allocationProtect;
};
Mutex regionLock;
Region* regions;

struct Lock
{
    Lock() { mutexLock(&regionLock); }
    ~Lock() { mutexUnlock(&regionLock); }
};

bool PageRange(const void* address, size_t size, uintptr_t& start, size_t& length)
{
    uintptr_t value = reinterpret_cast<uintptr_t>(address);
    if (size == 0 || size > UINTPTR_MAX - value || value + size > UINTPTR_MAX - (Page - 1))
        return false;
    start = value & ~(uintptr_t)(Page - 1);
    length = ((value + size + Page - 1) & ~(uintptr_t)(Page - 1)) - start;
    return length != 0;
}
Region* Find(uintptr_t address, size_t size)
{
    for (Region* r = regions; r != nullptr; r = r->next)
        if (address >= r->base && address - r->base < r->size && size <= r->size - (address - r->base))
            return r;
    return nullptr;
}
DWORD Protection(u32 permission)
{
    switch (permission)
    {
        case Perm_None: return PAGE_NOACCESS;
        case Perm_R: return PAGE_READONLY;
        case Perm_Rw: return PAGE_READWRITE;
        case Perm_Rx: return PAGE_EXECUTE_READ;
        default: return 0;
    }
}
bool Query(uintptr_t address, MemoryInfo& info)
{
    u32 pageInfo;
    return R_SUCCEEDED(svcQueryMemory(&info, &pageInfo, address)) &&
        info.size != 0 && info.addr <= address && address - info.addr < info.size;
}
bool Release(Region* region)
{
    if (!nxvm_release(reinterpret_cast<void*>(region->base), region->size)) return false;
    Region** link = &regions;
    while (*link != region) link = &(*link)->next;
    *link = region->next;
    free(region);
    return true;
}
}

extern "C" BOOL VIRTUALInitialize(bool initializeExecutableMemoryAllocator)
{
    // The executable-address preference is optional. CoreCLR's dual-mapping
    // allocator is a separate backend; data reservations never become RWX.
    (void)initializeExecutableMemoryAllocator;
    // Share the same bounded pool as System.Native. Do not initialize a second
    // pool or claim that decommit returns this backing to Horizon.
    return nxvm_ensure_initialized(size_t(512) << 20);
}
extern "C" void VIRTUALCleanup()
{
    Lock lock;
    // Runtime/BCL allocations can outlive shutdown. Preserve their ownership
    // records and backing; nxvm_destroy refuses any remaining shared users.
    if (regions == nullptr) nxvm_destroy();
}
extern "C" size_t GetVirtualPageSize() { return Page; }

LPVOID PALAPI VirtualAlloc(LPVOID address, SIZE_T size, DWORD allocationType, DWORD protect)
{
    uintptr_t start; size_t bytes;
    if (!PageRange(address, size, start, bytes) ||
        (allocationType & ~(MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN)) != 0 ||
        (allocationType & (MEM_RESERVE | MEM_COMMIT)) == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER); return nullptr;
    }
    if (protect != PAGE_READWRITE && protect != PAGE_READONLY && protect != PAGE_NOACCESS)
    {
        SetLastError(ERROR_NOT_SUPPORTED); return nullptr;
    }
    // Stack-state aliases cannot change protection. Reject unsupported commits
    // before touching an existing reservation or its contents.
    if ((allocationType & MEM_COMMIT) != 0 && protect != PAGE_READWRITE)
    {
        SetLastError(ERROR_NOT_SUPPORTED); return nullptr;
    }
    if ((allocationType & MEM_RESERVE) != 0 && address != nullptr)
    {
        SetLastError(ERROR_NOT_SUPPORTED); return nullptr;
    }

    Lock lock;
    bool isNew = address == nullptr;
    Region* region;
    if (isNew)
    {
        region = static_cast<Region*>(calloc(1, sizeof(Region)));
        if (region == nullptr) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
        void* base = nxvm_reserve(bytes, Page);
        if (base == nullptr)
        {
            free(region); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr;
        }
        region->base = start = reinterpret_cast<uintptr_t>(base);
        region->size = bytes;
        region->allocationProtect = protect;
        region->next = regions; regions = region;
    }
    else
    {
        region = Find(start, bytes);
        if (region == nullptr) { SetLastError(ERROR_INVALID_ADDRESS); return nullptr; }
    }
    if ((allocationType & MEM_COMMIT) != 0 && !nxvm_commit(reinterpret_cast<void*>(start), bytes))
    {
        // nxvm rolls back just the pages introduced by this commit. A failed
        // rollback poisons it; never destroy backing whose mapping is uncertain.
        if (isNew && !Release(region)) abort();
        SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr;
    }
    return reinterpret_cast<void*>(start);
}

BOOL PALAPI VirtualFree(LPVOID address, SIZE_T size, DWORD freeType)
{
    if (address == nullptr || (freeType != MEM_RELEASE && freeType != MEM_DECOMMIT))
    {
        SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    Lock lock;
    uintptr_t start = reinterpret_cast<uintptr_t>(address);
    size_t bytes = 0;
    Region* region;
    if (freeType == MEM_RELEASE)
    {
        region = Find(start, 0);
        if (region == nullptr || region->base != start || size != 0)
        {
            SetLastError(ERROR_INVALID_ADDRESS); return FALSE;
        }
        if (!Release(region)) { SetLastError(ERROR_INTERNAL_ERROR); return FALSE; }
        return TRUE;
    }
    if (!PageRange(address, size, start, bytes) || (region = Find(start, bytes)) == nullptr)
    {
        SetLastError(ERROR_INVALID_ADDRESS); return FALSE;
    }
    if (!nxvm_decommit(reinterpret_cast<void*>(start), bytes))
    {
        SetLastError(ERROR_INTERNAL_ERROR); return FALSE;
    }
    return TRUE;
}

BOOL PALAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD protect, PDWORD oldProtect)
{
    uintptr_t start; size_t bytes;
    if (oldProtect == nullptr || !PageRange(address, size, start, bytes))
    {
        SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    u32 permission;
    switch (protect)
    {
        case PAGE_NOACCESS: permission = Perm_None; break;
        case PAGE_READONLY: permission = Perm_R; break;
        case PAGE_READWRITE: permission = Perm_Rw; break;
        default: SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
    }
    Lock lock;
    Region* region = Find(start, 1);
    if (region != nullptr && Find(start, bytes) != region)
    {
        SetLastError(ERROR_INVALID_ADDRESS); return FALSE;
    }
    DWORD old = 0;
    bool same = true;
    uintptr_t end = start + bytes;
    for (uintptr_t current = start; current < end;)
    {
        MemoryInfo info;
        if (!Query(current, info) || info.type == MemType_Unmapped)
        {
            SetLastError(ERROR_INVALID_ADDRESS); return FALSE;
        }
        DWORD previous = Protection(info.perm);
        if (previous == 0) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
        if (current == start) old = previous;
        same &= info.perm == permission;
        size_t span = info.size - (current - info.addr);
        current += span < end - current ? span : end - current;
    }
    if (region != nullptr && !same)
    {
        SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
    }
    // Existing permissions are real success. For other mapped memory ask the kernel; do not
    // emulate NOACCESS by decommit (which must discard the old contents).
    int nativeProtection = protect == PAGE_READWRITE ? MapRead | MapWrite : protect == PAGE_READONLY ? MapRead : MapNone;
    if (!same && NativeProtect(reinterpret_cast<void*>(start), bytes, nativeProtection) != 0 &&
        R_FAILED(svcSetMemoryPermission(reinterpret_cast<void*>(start), bytes, permission)))
    {
        SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
    }
    *oldProtect = old;
    return TRUE;
}

SIZE_T PALAPI VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION output, SIZE_T length)
{
    if (output == nullptr || length < sizeof(*output))
    {
        SetLastError(ERROR_BAD_LENGTH); return 0;
    }
    uintptr_t start = reinterpret_cast<uintptr_t>(address) & ~(uintptr_t)(Page - 1);
    Lock lock;
    MemoryInfo info;
    if (!Query(start, info)) { SetLastError(ERROR_INVALID_ADDRESS); return 0; }
    MEMORY_BASIC_INFORMATION value{};
    value.BaseAddress = reinterpret_cast<void*>(start);
    value.RegionSize = info.size - (start - info.addr);
    Region* region = Find(start, 1);
    if (region != nullptr)
    {
        value.AllocationProtect = region->allocationProtect;
        size_t remaining = region->size - (start - region->base);
        if (value.RegionSize > remaining) value.RegionSize = remaining;
        value.State = info.type == MemType_Unmapped ? MEM_RESERVE : MEM_COMMIT;
        value.Type = MEM_PRIVATE;
    }
    else
    {
        value.State = info.type == MemType_Unmapped ? MEM_FREE : MEM_COMMIT;
        value.Type = info.type == MemType_Unmapped ? 0 : MEM_MAPPED;
        // An unmapped kernel span can contain PAL reservations. Stop before the
        // first such reservation so a caller cannot treat it as free space.
        for (Region* r = regions; r != nullptr; r = r->next)
            if (r->base > start && r->base - start < value.RegionSize)
                value.RegionSize = r->base - start;
    }
    value.Protect = value.State == MEM_COMMIT ? Protection(info.perm) : 0;
    *output = value;
    return sizeof(value);
}

LPVOID PALAPI PAL_VirtualReserveFromExecutableMemoryAllocatorWithinRange(LPCVOID, LPCVOID, SIZE_T, BOOL)
{
    // This is only the optional near-runtime reservation hint, not the JIT's
    // executable mapping backend. There is no preallocated executable arena.
    return nullptr;
}
void PALAPI PAL_GetExecutableMemoryAllocatorPreferredRange(LPVOID* start, LPVOID* end)
{
    *start = nullptr; *end = nullptr;
}
void* ReserveMemoryFromExecutableAllocator(CorUnix::CPalThread*, SIZE_T) { return nullptr; }
