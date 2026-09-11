// Horizon GC OS interface backed by bounded data-only memory reservations.
#include "common.h"
#include "LibnxDiagnostics.h"
#include <cstdio>
#include "gcenv.structs.h"
#include "gcenv.base.h"
#include "gcenv.os.h"
#include "gcenv.unix.inl"
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/result.h>
#include <switch/arm/counter.h>
}
#include <pthread.h>
#include <cstdlib>
extern "C" {
#include "nxvm.h"
}

uint32_t g_pageSizeUnixInl = 4096;
static AffinitySet s_affinity;
static uint32_t s_cpuCount;
// Kernel-backed rendezvous of registered runtime threads, including GC workers.
// See THREAD_ORDERING.md; this is not a local DMB or permission-change TLBI.
extern "C" void LibnxFlushProcessWriteBuffers();

static void CheckResult(Result rc) { if (R_FAILED(rc)) abort(); }
static size_t PageSize(size_t size)
{
    if (size > SIZE_MAX - 4095) return 0;
    return (size + 4095) & ~size_t(4095);
}

bool GCToOSInterface::Initialize()
{
    u64 coreMask;
    if (R_FAILED(svcGetInfo(&coreMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) return false;
    s_cpuCount = 0;
    for (unsigned i = 0; i < 64; ++i)
        if (coreMask & (uint64_t(1) << i)) { s_affinity.Add(i); ++s_cpuCount; }
    if (!s_cpuCount) return false;
    // Explicit bounded backing budget; failure is real OOM, not an unbounded
    // malloc-backed substitute for virtual reservation. Configuration is pending.
    if (!nxvm_ensure_initialized(size_t(512) << 20)) return false;
    return true;
}
void GCToOSInterface::Shutdown()
{
    // Live runtime reservations can survive GC shutdown. Retain ownership if
    // any remain; never free backing still mapped into a live reservation.
    nxvm_destroy();
}
void* GCToOSInterface::VirtualReserve(size_t size, size_t alignment, uint32_t flags, uint16_t node)
{
    if (LibnxRuntimeDiagnostic) {
        char text[128];
        snprintf(text,sizeof(text),"GCReserve size=%zu align=%zu flags=%u node=%u",size,alignment,flags,node);
        LibnxTraceStartup(text);
    }
    if (flags != VirtualReserveFlags::None || node != NUMA_NODE_UNDEFINED) return nullptr;
    size = PageSize(size);
    return size ? nxvm_reserve(size, alignment ? alignment : 4096) : nullptr;
}
bool GCToOSInterface::VirtualRelease(void* address, size_t size) { return nxvm_release(address, PageSize(size)); }
bool GCToOSInterface::VirtualCommit(void* address, size_t size, uint16_t node)
{
    return node == NUMA_NODE_UNDEFINED && nxvm_commit(address, PageSize(size));
}
bool GCToOSInterface::VirtualDecommit(void* address, size_t size) { return nxvm_decommit(address, PageSize(size)); }
void* GCToOSInterface::VirtualReserveAndCommitLargePages(size_t, uint16_t) { return nullptr; }
// Reset is only an advisory discard; preserving the bytes and commitment is
// allowed. Do not release pages that the GC can still access without recommit.
bool GCToOSInterface::VirtualReset(void*, size_t, bool) { return true; }
bool GCToOSInterface::SupportsWriteWatch() { return false; }
void GCToOSInterface::ResetWriteWatch(void*, size_t) { abort(); }
bool GCToOSInterface::GetWriteWatch(bool, void*, size_t, void**, uintptr_t*) { return false; }

void GCToOSInterface::FlushProcessWriteBuffers()
{
    LibnxFlushProcessWriteBuffers();
}
void GCToOSInterface::DebugBreak() { abort(); }
void GCToOSInterface::Sleep(uint32_t milliseconds) { svcSleepThread(uint64_t(milliseconds) * 1000000); }
void GCToOSInterface::YieldThread(uint32_t) { svcSleepThread(-1); }
uint32_t GCToOSInterface::GetCurrentProcessorNumber() { return svcGetCurrentProcessorNumber(); }
bool GCToOSInterface::CanGetCurrentProcessorNumber() { return true; }
bool GCToOSInterface::SetCurrentThreadIdealAffinity(uint16_t, uint16_t) { return false; }
bool GCToOSInterface::GetCurrentThreadIdealProc(uint16_t* proc)
{
    s32 ideal; u64 mask;
    if (R_FAILED(svcGetThreadCoreMask(&ideal, &mask, CUR_THREAD_HANDLE)) || ideal < 0) return false;
    *proc = uint16_t(ideal); return true;
}
uint64_t GCToOSInterface::GetCurrentThreadIdForLogging()
{
    u64 id; CheckResult(svcGetThreadId(&id, CUR_THREAD_HANDLE)); return id;
}
uint32_t GCToOSInterface::GetCurrentProcessId()
{
    u64 id; CheckResult(svcGetProcessId(&id, CUR_PROCESS_HANDLE)); return uint32_t(id);
}
size_t GCToOSInterface::GetCacheSizePerLogicalCpu(bool) { return 2 * 1024 * 1024; }
bool GCToOSInterface::SetThreadAffinity(uint16_t core)
{
    return core < 64 && s_affinity.Contains(core) && R_SUCCEEDED(svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, uint64_t(1) << core));
}
bool GCToOSInterface::BoostThreadPriority() { return false; }
const AffinitySet* GCToOSInterface::SetGCThreadsAffinitySet(uintptr_t mask, const AffinitySet* config)
{
    for (unsigned i=0; i<64; ++i)
        if ((!config->IsEmpty() && !config->Contains(i)) || (mask && !(mask & (uintptr_t(1) << i)))) s_affinity.Remove(i);
    return &s_affinity;
}
size_t GCToOSInterface::GetVirtualMemoryLimit()
{
    // nxvm can alias heap backing only inside the kernel stack region.
    // Advertising the full ASLR range makes region GC reserve unusable space.
    u64 size; CheckResult(svcGetInfo(&size, InfoType_StackRegionSize, CUR_PROCESS_HANDLE, 0)); return size;
}
size_t GCToOSInterface::GetVirtualMemoryMaxAddress()
{
    u64 base, size;
    CheckResult(svcGetInfo(&base, InfoType_StackRegionAddress, CUR_PROCESS_HANDLE, 0));
    CheckResult(svcGetInfo(&size, InfoType_StackRegionSize, CUR_PROCESS_HANDLE, 0)); return base + size - 1;
}
uint64_t GCToOSInterface::GetPhysicalMemoryLimit(bool* restricted)
{
    if (restricted) *restricted = true;
    return nxvm_stats().capacity;
}
void GCToOSInterface::GetMemoryStatus(uint64_t limit, uint32_t* load, uint64_t* available, uint64_t* pagefile)
{
    auto stats = nxvm_stats();
    uint64_t capacity = stats.capacity;
    if (limit && limit < capacity) capacity = limit;
    uint64_t free = capacity > stats.committed ? capacity - stats.committed : 0;
    if (load) *load = capacity ? uint32_t(100 - (free * 100 / capacity)) : 100;
    if (available) *available = free;
    if (pagefile) *pagefile = 0; // Horizon has no swap backing for this allocator.
}
int64_t GCToOSInterface::QueryPerformanceCounter() { return armGetSystemTick(); }
int64_t GCToOSInterface::QueryPerformanceFrequency() { return armGetSystemTickFreq(); }
uint64_t GCToOSInterface::GetLowPrecisionTimeStamp() { return armGetSystemTick() / (armGetSystemTickFreq() / 1000); }
uint32_t GCToOSInterface::GetTotalProcessorCount() { return s_cpuCount; }
bool GCToOSInterface::CanEnableGCNumaAware() { return false; }
bool GCToOSInterface::GetNumaInfo(uint16_t*, uint32_t*) { return false; }
bool GCToOSInterface::CanEnableGCCPUGroups() { return false; }
bool GCToOSInterface::GetCPUGroupInfo(uint16_t*, uint32_t*) { return false; }
bool GCToOSInterface::GetProcessorForHeap(uint16_t heap, uint16_t* core, uint16_t* node)
{
    for (unsigned i=0; i<64; ++i)
        if (s_affinity.Contains(i) && heap-- == 0) { *core=i; *node=NUMA_NODE_UNDEFINED; return true; }
    return false;
}
bool GCToOSInterface::ParseGCHeapAffinitizeRangesEntry(const char** text, size_t* first, size_t* last)
{
    return ParseIndexOrRange(text, first, last);
}
bool CLRCriticalSection::Initialize()
{
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr)) return false;
    int rc=pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (!rc) rc=pthread_mutex_init(&m_cs.mutex, &attr);
    pthread_mutexattr_destroy(&attr); return !rc;
}
void CLRCriticalSection::Destroy() { if (pthread_mutex_destroy(&m_cs.mutex)) abort(); }
void CLRCriticalSection::Enter() { if (pthread_mutex_lock(&m_cs.mutex)) abort(); }
void CLRCriticalSection::Leave() { if (pthread_mutex_unlock(&m_cs.mutex)) abort(); }
