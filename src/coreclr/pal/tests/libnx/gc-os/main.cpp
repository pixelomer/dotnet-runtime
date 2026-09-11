#include "common.h"
#include "gcenv.structs.h"
#include "gcenv.base.h"
#include "gcenv.os.h"
#include <malloc.h>
extern "C" {
#include <switch/kernel/svc.h>
#include "nxvm.h"
unsigned __nx_applet_exit_mode = 1;
}
static FILE* output;
static unsigned checks;
static void check(bool value, const char* name) {
    ++checks;
    if (!value) { fprintf(output, "FAIL %s check=%u\n", name, checks); abort(); }
}
int main() {
    output = fopen("sdmc:/switch/coreclr-gc-os-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN shared NativeAOT/CoreCLR GC OS boundary; no managed execution\n");
    check(GCToOSInterface::Initialize(), "initialize shared GC OS interface");
    size_t arenaBytes = nxvm_virtual_capacity();
    check(arenaBytes >= (size_t(64) << 20) && GCToOSInterface::GetVirtualMemoryLimit() == arenaBytes, "GC limit is actual dedicated arena");
    size_t largeBytes = arenaBytes / 2;
    auto large = static_cast<uint8_t*>(GCToOSInterface::VirtualReserve(largeBytes, 8192, VirtualReserveFlags::None, NUMA_NODE_UNDEFINED));
    check(large != nullptr && uintptr_t(large) + largeBytes - 1 <= nxvm_virtual_max_address(), "region-sized sparse reservation fits actual arena");
    check(GCToOSInterface::VirtualCommit(large, 4096, NUMA_NODE_UNDEFINED) &&
          GCToOSInterface::VirtualCommit(large + largeBytes - 4096, 4096, NUMA_NODE_UNDEFINED), "commit distant ends without materializing sparse range");
    large[0] = 1; large[largeBytes - 1] = 2;
    check(nxvm_stats().committed == 8192 && large[0] + large[largeBytes - 1] == 3, "physical commitment is independent of reserved capacity");
    check(GCToOSInterface::VirtualRelease(large, largeBytes), "retire region-sized reservation");
    void* holes[3];
    for (auto& hole : holes) { hole = nxvm_reserve(8192, 65536); check(hole != nullptr, "reserve aligned arena holes"); }
    check(nxvm_release(holes[1], 8192), "retire middle arena reservation");
    void* reused = nxvm_reserve(8192, 65536);
    check(reused == holes[1], "sorted allocator reuses a retired aligned hole");
    check(nxvm_release(holes[2], 8192) && nxvm_release(holes[0], 8192) && nxvm_release(reused, 8192), "retire reservations out of allocation order");
    fprintf(output, "ARENA bytes=%zu sparse_reservation=%zu\n", arenaBytes, largeBytes);
    uint64_t mask, pid;
    check(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0) == 0, "query actual allowed CPU mask");
    check(svcGetProcessId(&pid, CUR_PROCESS_HANDLE) == 0 && GCToOSInterface::GetCurrentProcessId() == pid, "GC process identity");
    check(GCToOSInterface::GetCurrentProcessorNumber() < GCToOSInterface::GetMaxProcessorCount(), "GC CPU bound contains executing processor");
    AffinitySet empty, subset;
    check(empty.Initialize(64) && subset.Initialize(64), "allocate real GC affinity sets");
    unsigned first = 0; while (!(mask & (uint64_t(1) << first))) ++first;
    subset.Add(first);
    auto selected = GCToOSInterface::SetGCThreadsAffinitySet(0, &subset);
    check(selected->Count() == 1 && selected->Contains(first), "GC affinity subset");
    selected = GCToOSInterface::SetGCThreadsAffinitySet(0, &empty);
    for (unsigned cpu = 0; cpu < 64; ++cpu)
        check(selected->Contains(cpu) == bool(mask & (uint64_t(1) << cpu)), "reapplying default restores kernel allowed affinity");
    selected = GCToOSInterface::SetGCThreadsAffinitySet(uintptr_t(1) << first, &empty);
    check(selected->Count() == 1, "GC mask selects one allowed CPU");
    GCToOSInterface::SetGCThreadsAffinitySet(0, &empty);
    for (unsigned round = 0; round < 16; ++round) {
        for (unsigned life = 0; life < 64; ++life) {
            auto memory = static_cast<uint8_t*>(GCToOSInterface::VirtualReserve(16384, 65536, VirtualReserveFlags::None, NUMA_NODE_UNDEFINED));
            check(memory != nullptr && (reinterpret_cast<uintptr_t>(memory) & 65535) == 0, "GC aligned native reservation");
            check(!GCToOSInterface::VirtualReset(memory, 4096, false), "reset rejects uncommitted reservation");
            check(GCToOSInterface::VirtualCommit(memory, 8192, NUMA_NODE_UNDEFINED), "GC commit native pages");
            memset(memory, 0x6b, 8192);
            check(GCToOSInterface::VirtualReset(memory, 8192, false), "valid advisory reset");
            check(memory[0] == 0x6b && memory[8191] == 0x6b, "advisory reset preserves accessible contents");
            check(!GCToOSInterface::VirtualReset(memory, 12288, false), "reset rejects partially committed range");
            check(!GCToOSInterface::VirtualReset(memory + 1, 4096, false), "reset rejects unaligned address");
            check(!GCToOSInterface::VirtualReset(memory, SIZE_MAX, false), "reset rejects overflowed size");
            check(GCToOSInterface::VirtualDecommit(memory, 8192), "GC decommit");
            check(!GCToOSInterface::VirtualReset(memory, 4096, false), "reset rejects decommitted pages");
            check(GCToOSInterface::VirtualCommit(memory, 4096, NUMA_NODE_UNDEFINED) && memory[0] == 0, "recommit is zero-filled");
            check(GCToOSInterface::VirtualRelease(memory, 16384), "GC releases complete reservation");
            check(!GCToOSInterface::VirtualReset(memory, 4096, false), "reset rejects retired reservation");
        }
        auto stats = nxvm_stats();
        check(stats.reservations == 0 && stats.committed == 0 && !stats.poisoned, "GC OS round retires all ownership");
        fprintf(output, "ROUND %u checks=%u native_used=%zu\n", round, checks, mallinfo().uordblks);
    }
    GCToOSInterface::Shutdown();
    check(nxvm_stats().capacity == 0, "shared pool retires when no live reservations remain");
    fprintf(output, "PASS checks=%u lifetimes=1024\n", checks);
    fclose(output); return 0;
}
