#include <switch.h>
#include <pthread.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include "../../LibnxThreads.h"
extern "C" {
uint32_t DuplicateHandle(void*, void*, void*, void**, uint32_t, uint32_t, uint32_t);
uint32_t CloseHandle(void*);
}
static FILE* logFile;
static unsigned checks;
#define CHECK(x) do { if (!(x)) { fprintf(logFile, "FAIL line=%d expression=%s\n", __LINE__, #x); fclose(logFile); abort(); } ++checks; } while(0)
static void* duplicate()
{
    void* handle = nullptr;
    if (!DuplicateHandle((void*)-1, (void*)-2, (void*)-1, &handle, 0, 0, 0)) abort();
    return handle;
}
static void setupWorker()
{
    if (R_FAILED(svcSetThreadPriority(CUR_THREAD_HANDLE, 0x30))) abort();
    if (R_FAILED(svcSetThreadCoreMask(CUR_THREAD_HANDLE, 1, 2))) abort();
}
struct Busy
{
    std::atomic<bool> ready{false}, stop{false};
    std::atomic<uint64_t> counter{0};
    void* handle;
};
static void* busyWorker(void* state)
{
    setupWorker();
    auto& w = *static_cast<Busy*>(state);
    w.handle = duplicate();
    void* second = duplicate(); // Shared registry entry, two PAL handle owners.
    w.ready.store(true, std::memory_order_release);
    while (!w.stop.load(std::memory_order_relaxed))
        w.counter.fetch_add(1, std::memory_order_relaxed);
    if (!CloseHandle(second) || !CloseHandle(w.handle)) abort();
    return nullptr;
}
static void waitReady(std::atomic<bool>& flag)
{
    uint64_t end = armGetSystemTick() + armGetSystemTickFreq() * 5;
    while (!flag.load(std::memory_order_acquire))
    {
        CHECK(armGetSystemTick() < end);
        svcSleepThread(100000);
    }
}
struct Litmus
{
    std::atomic<unsigned> start{0}, done{0}, x{0}, y{0}, observed{0};
    std::atomic<bool> ready{false};
};
static constexpr unsigned litmusRounds = 2000;
static void* litmusWorker(void* state)
{
    setupWorker();
    auto& w = *static_cast<Litmus*>(state);
    void* handle = duplicate();
    w.ready.store(true, std::memory_order_release);
    for (unsigned i = 1; i <= litmusRounds; ++i)
    {
        while (w.start.load(std::memory_order_acquire) != i) __asm__ volatile("yield");
        w.y.store(1, std::memory_order_relaxed);
        w.observed.store(w.x.load(std::memory_order_relaxed), std::memory_order_relaxed);
        w.done.store(i, std::memory_order_release);
    }
    if (!CloseHandle(handle)) abort();
    return nullptr;
}
static std::atomic<unsigned> churnDone{0};
static void* churnWorker(void*)
{
    setupWorker();
    for (unsigned i = 0; i < 200; ++i)
    {
        void* a = duplicate();
        void* b = duplicate();
        for (unsigned n=0; n<100; ++n) __asm__ volatile("yield");
        if (!CloseHandle(a) || !CloseHandle(b)) abort();
    }
    churnDone.fetch_add(1, std::memory_order_release);
    return nullptr;
}
int main()
{
    logFile = fopen("sdmc:/switch/dotnet-thread-registry-probe.txt", "w");
    if (!logFile) return 1;
    fprintf(logFile, "BEGIN registered-thread barrier/pause probe; no managed GC\n");
    fflush(logFile);
    u64 kernelVersion;
    Result meta = svcGetInfo(&kernelVersion, 65000, INVALID_HANDLE, 0);
    fprintf(logFile, "mesosphere_meta_rc=0x%x supported_kernel=0x%lx\n", meta, R_SUCCEEDED(meta) ? kernelVersion : 0UL);
    CHECK(R_SUCCEEDED(meta));
    CHECK(R_SUCCEEDED(svcSetThreadCoreMask(CUR_THREAD_HANDLE, 0, 1)));
    void* own = duplicate();
    Busy busy;
    pthread_t thread;
    CHECK(pthread_create(&thread, nullptr, busyWorker, &busy) == 0);
    waitReady(busy.ready);
    unsigned pauses = 0;
    for (unsigned i=0; i<128; ++i)
    {
        ThreadContext context;
        CHECK(LibnxPausePalThread(busy.handle, &context));
        CHECK(context.pc.x != 0 && context.sp != 0);
        uint64_t stopped = busy.counter.load(std::memory_order_relaxed);
        LibnxFlushProcessWriteBuffers(); // Must not resume a GC-held thread.
        svcSleepThread(1000000);
        CHECK(busy.counter.load(std::memory_order_relaxed) == stopped);
        CHECK(LibnxResumePalThread(busy.handle));
        uint64_t end = armGetSystemTick() + armGetSystemTickFreq();
        while (busy.counter.load(std::memory_order_relaxed) == stopped)
        {
            CHECK(armGetSystemTick() < end);
            svcSleepThread(10000);
        }
        ++pauses;
    }
    busy.stop.store(true, std::memory_order_relaxed);
    CHECK(pthread_join(thread, nullptr) == 0);
    fprintf(logFile, "pause_resume=%u held_threads_remained_stopped=1\n", pauses);
    fflush(logFile);
    Litmus litmus;
    CHECK(pthread_create(&thread, nullptr, litmusWorker, &litmus) == 0);
    waitReady(litmus.ready);
    for (unsigned i=1; i<=litmusRounds; ++i)
    {
        litmus.x.store(0, std::memory_order_relaxed);
        litmus.y.store(0, std::memory_order_relaxed);
        litmus.start.store(i, std::memory_order_release);
        litmus.x.store(1, std::memory_order_relaxed);
        LibnxFlushProcessWriteBuffers();
        unsigned observed = litmus.y.load(std::memory_order_relaxed);
        uint64_t end = armGetSystemTick() + armGetSystemTickFreq();
        while (litmus.done.load(std::memory_order_acquire) != i)
        {
            CHECK(armGetSystemTick() < end);
            svcSleepThread(10000);
        }
        CHECK(observed || litmus.observed.load(std::memory_order_relaxed));
    }
    CHECK(pthread_join(thread, nullptr) == 0);
    fprintf(logFile, "store_buffer_litmus_rounds=%u forbidden_outcomes=0\n", litmusRounds);
    fflush(logFile);
    pthread_t churn[4];
    for (auto& t : churn) CHECK(pthread_create(&t, nullptr, churnWorker, nullptr) == 0);
    uint64_t end = armGetSystemTick() + armGetSystemTickFreq() * 20;
    unsigned flushes = 0;
    while (churnDone.load(std::memory_order_acquire) != 4)
    {
        CHECK(armGetSystemTick() < end);
        LibnxFlushProcessWriteBuffers();
        ++flushes;
        svcSleepThread(10000);
    }
    for (auto& t : churn) CHECK(pthread_join(t, nullptr) == 0);
    CHECK(CloseHandle(own));
    LibnxFlushProcessWriteBuffers(); // Empty registry is valid after unregister.
    fprintf(logFile, "END PASS checks=%u churn_cycles=800 concurrent_flushes=%u\n", checks, flushes);
    fclose(logFile);
    return 0;
}
