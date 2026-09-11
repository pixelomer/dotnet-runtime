#include <switch.h>
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../../LibnxPlatform.h"
extern "C" {
#include "../../nxvm.h"
void* PalVirtualAlloc(size_t, uint32_t);
void PalVirtualFree(void*, size_t);
uint32_t PalVirtualProtect(void*, size_t, uint32_t);
void* PalCreateEventW(void*, uint32_t, uint32_t, const uint16_t*);
uint32_t SetEvent(void*);
uint32_t ResetEvent(void*);
uint32_t CloseHandle(void*);
uint32_t WaitForSingleObjectEx(void*, uint32_t, uint32_t);
uint32_t DuplicateHandle(void*, void*, void*, void**, uint32_t, uint32_t, uint32_t);
bool PalGetMaximumStackBounds(void**, void**);
void* PalGetModuleHandleFromPointer(void*);
}
static FILE* logFile;
static unsigned checks;
// A dedicated page, so a read-only test cannot protect neighboring runtime data.
alignas(4096) static unsigned char permissionPage[4096];
#define CHECK(x) do { if (!(x)) { fprintf(logFile, "FAIL line=%d expression=%s\n", __LINE__, #x); fclose(logFile); abort(); } ++checks; } while(0)
struct Worker { bool passed; void* event; };
static void* worker(void* arg)
{
    Worker* w = static_cast<Worker*>(arg);
    void* low; void* high;
    bool ok = PalGetMaximumStackBounds(&low, &high);
    uintptr_t here = reinterpret_cast<uintptr_t>(&low);
    ok = ok && here >= reinterpret_cast<uintptr_t>(low) && here < reinterpret_cast<uintptr_t>(high);
    void* copy = nullptr;
    ok = ok && DuplicateHandle((void*)-1, (void*)-2, (void*)-1, &copy, 0, 0, 0);
    if (copy) ok = CloseHandle(copy) && ok;
    u64 id;
    ok = R_SUCCEEDED(svcGetThreadId(&id, LibnxGetCurrentThreadHandle())) && ok;
    w->passed = ok;
    SetEvent(w->event);
    return nullptr;
}
int main()
{
    logFile = fopen("sdmc:/switch/dotnet-pal-probe.txt", "w");
    if (!logFile) return 1;
    fprintf(logFile, "BEGIN source-built PAL component probe; no managed runtime initialization\n");
    fflush(logFile);
    CHECK(nxvm_init(4 * 1024 * 1024));
    void* alias = nxvm_reserve(4096, 4096);
    CHECK(alias && nxvm_commit(alias, 4096));
    MemoryInfo info; u32 pageInfo;
    CHECK(R_SUCCEEDED(svcQueryMemory(&info, &pageInfo, (u64)alias)));
    Result rc = svcSetMemoryPermission(alias, 4096, Perm_Rw);
    fprintf(logFile, "alias type=%u permission=%u set_same_permission_rc=0x%x\n", info.type, info.perm, rc);
    CHECK(nxvm_release(alias, 4096));
    CHECK(PalVirtualAlloc(4096, 0x20) == nullptr); // Executable mappings unsupported.
    CHECK(PalVirtualAlloc(SIZE_MAX, 4) == nullptr);
    for (unsigned i = 0; i < 128; ++i)
    {
        unsigned char* p = static_cast<unsigned char*>(PalVirtualAlloc(8191, 4));
        CHECK(p != nullptr);
        for (unsigned j = 0; j < 8192; ++j) CHECK(p[j] == 0);
        memset(p, 0xa5, 8192);
        CHECK(!PalVirtualProtect(p, 8192, 2)); // Stack aliases cannot be reprotected.
        CHECK(p[0] == 0xa5 && p[8191] == 0xa5);
        CHECK(!PalVirtualProtect(p, 8192, 0x40));
        CHECK(PalVirtualProtect(p, 8192, 4));
        CHECK(!PalVirtualProtect(p, 8192, 1));
        CHECK(PalVirtualProtect(p, 8192, 4));
        PalVirtualFree(p, 8191);
    }
    void* full = PalVirtualAlloc(4 * 1024 * 1024, 4);
    CHECK(full != nullptr);
    CHECK(PalVirtualAlloc(4096, 4) == nullptr);
    PalVirtualFree(full, 4 * 1024 * 1024);
    CHECK(!PalVirtualProtect(full, 4096, 1)); // An unmapped range is not a protected allocation.
    auto stats = nxvm_stats();
    CHECK(stats.committed == 0 && stats.reservations == 0 && !stats.poisoned);
    CHECK(nxvm_destroy());
    permissionPage[0] = 0x42;
    CHECK(R_SUCCEEDED(svcQueryMemory(&info, &pageInfo, (u64)permissionPage)));
    fprintf(logFile, "static_page type=%u permission=%u real_process_handle=%u\n", info.type, info.perm, envGetOwnProcessHandle());
    fflush(logFile);
    CHECK(PalVirtualProtect(permissionPage, sizeof(permissionPage), 2));
    CHECK(R_SUCCEEDED(svcQueryMemory(&info, &pageInfo, (u64)permissionPage)));
    CHECK(info.perm == Perm_R && permissionPage[0] == 0x42);
    CHECK(PalVirtualProtect(permissionPage, sizeof(permissionPage), 4));
    permissionPage[0] = 0x24;
    void* event = PalCreateEventW(nullptr, 0, 0, nullptr);
    CHECK(event && event != (void*)-1);
    CHECK(WaitForSingleObjectEx(event, 2, 0) == 258);
    CHECK(SetEvent(event));
    CHECK(WaitForSingleObjectEx(event, 0, 0) == 0);
    CHECK(WaitForSingleObjectEx(event, 0, 0) == 258);
    CHECK(CloseHandle(event));
    event = PalCreateEventW(nullptr, 1, 1, nullptr);
    CHECK(event && event != (void*)-1);
    CHECK(WaitForSingleObjectEx(event, 0, 0) == 0);
    CHECK(WaitForSingleObjectEx(event, 0, 0) == 0);
    CHECK(ResetEvent(event));
    CHECK(WaitForSingleObjectEx(event, 0, 0) == 258);
    Worker workers[4] = {};
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; ++i)
    {
        workers[i].event = PalCreateEventW(nullptr, 0, 0, nullptr);
        CHECK(workers[i].event && workers[i].event != (void*)-1);
        CHECK(pthread_create(&threads[i], nullptr, worker, &workers[i]) == 0);
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        CHECK(WaitForSingleObjectEx(workers[i].event, 5000, 0) == 0);
        CHECK(pthread_join(threads[i], nullptr) == 0);
        CHECK(workers[i].passed);
        CHECK(CloseHandle(workers[i].event));
    }
    CHECK(CloseHandle(event));
    void* low; void* high;
    CHECK(PalGetMaximumStackBounds(&low, &high));
    CHECK((uintptr_t)&low >= (uintptr_t)low && (uintptr_t)&low < (uintptr_t)high);
    CHECK(PalGetModuleHandleFromPointer((void*)main) != nullptr);
    CHECK(PalGetModuleHandleFromPointer(&low) == nullptr);
    fprintf(logFile, "END PASS checks=%u reuse_cycles=128 pthreads=4 no_live_mappings=1\n", checks);
    fclose(logFile);
    return 0;
}
