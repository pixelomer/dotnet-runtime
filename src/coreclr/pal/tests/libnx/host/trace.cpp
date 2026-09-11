// Link-time observation only: preserve the real PAL operation and last error.
#include <pal.h>
extern "C" void HostProtectionTrace(void*, size_t, unsigned, int, unsigned);
extern "C" BOOL PALAPI __real_VirtualProtect(LPVOID, SIZE_T, DWORD, PDWORD);
extern "C" BOOL PALAPI __wrap_VirtualProtect(LPVOID address, SIZE_T size, DWORD protection, PDWORD old)
{
    BOOL result = __real_VirtualProtect(address, size, protection, old);
    DWORD error = GetLastError();
    HostProtectionTrace(address, size, protection, result, error);
    SetLastError(error);
    return result;
}

extern "C" {
#include <switch/kernel/svc.h>
void HostStackReservationTrace(size_t, void*);
void LibnxRuntimeDiagnostic(const char*);
void* __real_virtmemFindStack(size_t, size_t);
void* __wrap_virtmemFindStack(size_t size, size_t guard)
{
    void* result = __real_virtmemFindStack(size, guard);
    HostStackReservationTrace(size, result);
    return result;
}
void HostDumpStackMap()
{
    uint64_t start, size;
    if (svcGetInfo(&start, InfoType_StackRegionAddress, CUR_PROCESS_HANDLE, 0) ||
        svcGetInfo(&size, InfoType_StackRegionSize, CUR_PROCESS_HANDLE, 0)) return;
    char text[180];
    snprintf(text, sizeof(text), "StackRegion start=%llx size=%llu", (unsigned long long)start, (unsigned long long)size);
    LibnxRuntimeDiagnostic(text);
    for (uint64_t current = start; current < start + size; ) {
        MemoryInfo info; uint32_t page;
        if (svcQueryMemory(&info, &page, current) || !info.size || info.addr + info.size <= current) return;
        snprintf(text, sizeof(text), "StackSpan addr=%llx size=%llu type=%x perm=%x", (unsigned long long)info.addr, (unsigned long long)info.size, info.type, info.perm);
        LibnxRuntimeDiagnostic(text);
        current = info.addr + info.size;
    }
}
}
