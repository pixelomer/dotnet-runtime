#include "pal.h"
namespace CorUnix { class CPalThread; }
#include "pal/virtual.h"
#include <minipal/cpucount.h>
extern "C" {
#include <libs/Common/nxvm.h>
#include <switch/kernel/svc.h>
#include <switch/result.h>
}
#include <atomic>
#include <pthread.h>
#include <malloc.h>

extern "C" { unsigned __nx_applet_exit_mode = 1; }
static FILE* output;
static std::atomic<unsigned> checks{0};
static void check(bool value, const char* name)
{
    unsigned count = ++checks;
    if (!value) { fprintf(output, "FAIL %s check=%u error=%u\n", name, count, GetLastError()); abort(); }
}
static MEMORY_BASIC_INFORMATION query(void* p)
{
    MEMORY_BASIC_INFORMATION info{};
    check(VirtualQuery(p, &info, sizeof(info)) == sizeof(info), "VirtualQuery");
    return info;
}
static bool allZero(const unsigned char* p, size_t size)
{
    for (size_t i = 0; i < size; ++i) if (p[i] != 0) return false;
    return true;
}
static void* worker(void*)
{
    for (unsigned iteration = 0; iteration < 64; ++iteration)
    {
        auto p = static_cast<unsigned char*>(VirtualAlloc(nullptr, 2 * 1024 * 1024, MEM_RESERVE, PAGE_NOACCESS));
        check(p != nullptr, "reserve without proportional backing");
        auto info = query(p);
        check(info.State == MEM_RESERVE && info.RegionSize == 2 * 1024 * 1024, "reserved range");
        check(VirtualAlloc(p + 1, 4096, MEM_COMMIT, PAGE_READWRITE) == p, "unaligned two-page commit");
        check(query(p).State == MEM_COMMIT && query(p).Protect == PAGE_READWRITE, "committed query");
        check(allZero(p, 8192), "fresh committed pages zero");
        memset(p, 0x5a, 8192);
        check(VirtualAlloc(p, 8192, MEM_COMMIT, PAGE_READWRITE) == p && p[7] == 0x5a, "idempotent commit preserves bytes");
        DWORD old = 0;
        check(VirtualProtect(p, 8192, PAGE_READWRITE, &old) && old == PAGE_READWRITE, "existing protection verified");
        check(!VirtualProtect(p, 8192, PAGE_NOACCESS, &old) && p[7] == 0x5a, "unsupported protection retains data");
        check(!VirtualFree(p + 4096, 0, MEM_RELEASE), "interior release rejected");
        check(!VirtualFree(p, 4096, MEM_RELEASE), "partial release rejected");
        check(VirtualFree(p + 4095, 2, MEM_DECOMMIT), "unaligned cross-page decommit");
        check(query(p).State == MEM_RESERVE, "decommitted query");
        check(VirtualAlloc(p, 8192, MEM_COMMIT, PAGE_READWRITE) == p, "recommit");
        check(allZero(p, 8192), "recommit zeroes both pages");
        check(VirtualFree(p, 0, MEM_RELEASE), "release complete allocation");
    }
    return nullptr;
}
int main()
{
    output = fopen("sdmc:/switch/coreclr-vm-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN CoreCLR PAL virtual memory; no managed runtime\n");
    check(nxvm_init(8 * 1024 * 1024), "initialize bounded host pool");
    check(VIRTUALInitialize(true), "PAL shares existing pool");
    check(nxvm_stats().capacity == 8 * 1024 * 1024, "PAL does not replace shared pool");
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    u64 mask, base, length;
    check(R_SUCCEEDED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)), "kernel core mask");
    check(R_SUCCEEDED(svcGetInfo(&base, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0)), "kernel address base");
    check(R_SUCCEEDED(svcGetInfo(&length, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0)), "kernel address length");
    unsigned count = 0, maximum = 0;
    for (unsigned i = 0; i < 64; ++i) if (mask & (uint64_t(1) << i)) { ++count; maximum = i + 1; }
    check(system.dwNumberOfProcessors == count, "PAL permitted CPU count");
    check(minipal_get_cpu_max_possible_count() == int(maximum), "CPU index bound");
    check(reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress) == base + length - 1, "PAL address-space bounds");
    check(system.dwPageSize == 4096 && GetVirtualPageSize() == 4096, "page size");
    fprintf(output, "SYSTEM cpus=%u index_bound=%u max_address=%p page=%u\n", count, maximum,
        system.lpMaximumApplicationAddress, system.dwPageSize);
    auto p = static_cast<unsigned char*>(VirtualAlloc(nullptr, 16 * 1024 * 1024, MEM_RESERVE, PAGE_NOACCESS));
    check(p != nullptr, "reservation exceeds physical pool");
    check(VirtualAlloc(p, 4096, MEM_COMMIT, PAGE_READWRITE) == p, "initial commit"); p[0] = 42;
    check(!VirtualAlloc(p, 12 * 1024 * 1024, MEM_COMMIT, PAGE_READWRITE), "quota failure");
    check(p[0] == 42 && nxvm_stats().committed == 4096, "quota failure preserves earlier pages");
    check(!VirtualAlloc(p + 16 * 1024 * 1024 - 4096, 8192, MEM_COMMIT, PAGE_READWRITE), "cross-reservation commit rejected");
    check(!VirtualAlloc(p, 4096, MEM_RESERVE, PAGE_READWRITE), "fixed reservation rejected");
    check(!VirtualAlloc(nullptr, SIZE_MAX, MEM_RESERVE, PAGE_NOACCESS), "overflow rejected");
    check(!VirtualAlloc(nullptr, 4096, MEM_COMMIT, PAGE_EXECUTE_READWRITE), "data pool rejects executable mapping");
    check(VirtualFree(p, 0, MEM_RELEASE), "cleanup quota case");
    check(query(p).State == MEM_FREE, "released range no longer reserved");
    for (unsigned round = 0; round < 16; ++round)
    {
        pthread_t threads[4];
        for (auto& t : threads) check(pthread_create(&t, nullptr, worker, nullptr) == 0, "create worker");
        for (auto& t : threads) check(pthread_join(t, nullptr) == 0, "join worker");
        NxvmStats stats = nxvm_stats();
        check(stats.reserved == 0 && stats.committed == 0 && stats.reservations == 0 && !stats.poisoned, "all backing and reservations returned");
        fprintf(output, "ROUND %u checks=%u native_used=%zu reserved=%zu committed=%zu\n",
            round, checks.load(), mallinfo().uordblks, stats.reserved, stats.committed);
    }
    VIRTUALCleanup();
    check(nxvm_stats().capacity == 0, "empty pool destroyed at cleanup");
    fprintf(output, "PASS checks=%u lifetimes=4096 threads=64\n", checks.load());
    fclose(output);
    return 0;
}
