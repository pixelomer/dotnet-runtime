#include "pal/thread.hpp"
#include "pal/modulenative.h"
#include "pal/libnx/workerchannel.h"
#include "pal/threadnative.h"
#include "pal/process.h"
#include <atomic>
#include <malloc.h>
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/arm/counter.h>
unsigned __nx_applet_exit_mode = 1;
__attribute__((visibility("default"), used, noinline)) unsigned ModuleProbeFunction(unsigned value) { return value * 7 + 3; }
__attribute__((visibility("default"), used)) unsigned ModuleProbeData = 0x12345678;
__attribute__((visibility("hidden"), used)) unsigned ModuleProbeHidden = 123;
}
static FILE* output;
static std::atomic<unsigned> checks{0};
static void check(bool yes, const char* name) {
    unsigned count = ++checks;
    if (!yes) { fprintf(output, "FAIL %s check=%u pal_error=%u errno=%d native_error=%s\n", name, count, GetLastError(), errno, NativeModuleError()); abort(); }
}
static void* worker(void*) {
    for (unsigned i = 0; i < 64; ++i) {
        auto handle = PAL_LoadLibraryDirect(nullptr);
        check(handle != nullptr, "open real resident module");
        auto function = reinterpret_cast<unsigned(*)(unsigned)>(PAL_GetProcAddressDirect(handle, "ModuleProbeFunction"));
        check(function == ModuleProbeFunction && function(i) == i * 7 + 3, "resolve and execute exported function");
        check(reinterpret_cast<void*>(PAL_GetProcAddressDirect(handle, "ModuleProbeData")) == &ModuleProbeData, "resolve exported object");
        check(!PAL_GetProcAddressDirect(handle, "ModuleProbeHidden"), "hidden symbol remains hidden");
        check(PAL_GetLoadLibraryError() != nullptr && PAL_GetLoadLibraryError() == nullptr, "thread-local native error consumed once");
        check(PAL_FreeLibraryDirect(handle), "release resident lookup reference");
    }
    return nullptr;
}
struct ChannelRead {
    LibnxWorkerChannel* channel;
    std::atomic<bool> entered{false};
    int result = -2;
    uint8_t byte = 0;
};
static void* readChannel(void* argument) {
    auto work = static_cast<ChannelRead*>(argument);
    work->entered = true;
    work->result = work->channel->Read(&work->byte, 1, -1);
    return nullptr;
}
static void testChannel() {
    LibnxWorkerChannel channel;
    uint8_t value = 0;
    check(channel.Read(&value, 1, 0) == 0, "empty nonblocking native channel");
    uint64_t start = armGetSystemTick();
    check(channel.Read(&value, 1, 20) == 0, "native channel timeout");
    uint64_t elapsed = armTicksToNs(armGetSystemTick() - start);
    check(elapsed >= 19000000 && elapsed < 1000000000, "timeout measured on Horizon monotonic counter");
    unsigned count = 0;
    while (count < 65536 && channel.Write(static_cast<uint8_t>(count)) == 1) ++count;
    check(count > 0 && count < 65536 && errno == EAGAIN, "bounded native channel backpressure");
    for (unsigned i = 0; i < count; ++i)
        check(channel.Read(&value, 1, 0) == 1 && value == static_cast<uint8_t>(i), "native channel FIFO order");
    ChannelRead work; work.channel = &channel;
    pthread_t thread;
    check(pthread_create(&thread, nullptr, readChannel, &work) == 0, "create native channel waiter");
    while (!work.entered) svcSleepThread(1000000);
    check(channel.Write(73) == 1, "wake native channel waiter");
    check(pthread_join(thread, nullptr) == 0 && work.result == 1 && work.byte == 73, "native condition wake transfers byte");
    check(channel.Write(91) == 1, "queue command before close");
    channel.Close();
    check(channel.Write(92) == -1 && errno == EPIPE, "closed channel rejects writes");
    check(channel.Read(&value, 1, -1) == 1 && value == 91, "close drains accepted commands");
    check(channel.Read(&value, 1, -1) == 0, "drained channel reports EOF");
    LibnxWorkerChannel closing;
    ChannelRead closingWork; closingWork.channel = &closing;
    check(pthread_create(&thread, nullptr, readChannel, &closingWork) == 0, "create shutdown waiter");
    while (!closingWork.entered) svcSleepThread(1000000);
    closing.Close();
    check(pthread_join(thread, nullptr) == 0 && closingWork.result == 0, "close wakes blocked native reader");
}
struct PalWork { HANDLE ready; HANDLE finish; DWORD id; std::atomic<unsigned> calls{0}; };
static DWORD palWorker(void* argument) {
    auto work = static_cast<PalWork*>(argument);
    uint64_t tid;
    check(svcGetThreadId(&tid, CUR_THREAD_HANDLE) == 0 && GetCurrentThreadId() == tid && work->id == tid, "PAL worker has actual kernel thread identity");
    void *low, *high;
    check(NativeGetCurrentStackBounds(&low, &high), "PAL worker native stack bounds");
    ++work->calls;
    check(SetEvent(work->ready), "PAL worker signals real PAL event");
    check(WaitForSingleObject(work->finish, 5000) == WAIT_OBJECT_0, "PAL worker waits on finish event");
    return 123;
}
static void testPalThreads() {
    for (unsigned round = 0; round < 32; ++round) {
        PalWork work;
        work.ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        work.finish = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        check(work.ready != nullptr && work.finish != nullptr, "create PAL event objects");
        HANDLE thread = CreateThread(nullptr, 128 * 1024, palWorker, &work, CREATE_SUSPENDED, &work.id);
        check(thread != nullptr, "create suspended PAL thread");
        check(work.calls == 0 && WaitForSingleObject(work.ready, 0) == WAIT_TIMEOUT, "suspended worker cannot execute entry");
        check(ResumeThread(thread) == 1, "resume PAL startup semaphore once");
        check(WaitForSingleObject(work.ready, 5000) == WAIT_OBJECT_0, "PAL event wait observes worker");
        check(ResumeThread(thread) == static_cast<DWORD>(-1) && GetLastError() == ERROR_BAD_COMMAND, "second resume cannot repost startup semaphore");
        check(SetEvent(work.finish), "release worker finish event");
        check(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0, "PAL thread object signals exit");
        DWORD exitCode = 0;
        CorUnix::CPalThread* data = nullptr;
        CorUnix::IPalObject* object = nullptr;
        auto current = CorUnix::InternalGetCurrentThread();
        check(CorUnix::InternalGetThreadDataFromHandle(current, thread, &data, &object) == NO_ERROR, "retain actual PAL thread object after exit");
        check(data->GetExitCode(&exitCode) && exitCode == 123 && work.calls == 1, "PAL thread exit code and single entry");
        object->ReleaseReference(current);
        check(CloseHandle(thread) && CloseHandle(work.ready) && CloseHandle(work.finish), "release PAL thread and event handles");
        if ((round + 1) % 8 == 0) {
            // Thread-handle signaling precedes native TLS retirement/reaping.
            svcSleepThread(2000000);
            fprintf(output, "PAL_THREAD_BATCH %u native_used=%zu\n", (round + 1) / 8, mallinfo().uordblks);
        }
    }
}
int main(int argc, char** argv) {
    output = fopen("sdmc:/switch/coreclr-startup-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN production PAL native modules and PAL startup; no managed runtime\n");
    if (argc == 0) { fprintf(output, "FAIL loader provided no argv\n"); fclose(output); return 1; }
    auto initialized = PAL_InitializeCoreCLR(argv[0], TRUE);
    fprintf(output, "PAL_InitializeCoreCLR result=%u errno=%d\n", initialized, errno);
    if (initialized != NO_ERROR) { fclose(output); return 1; }
    uint64_t pid;
    check(svcGetProcessId(&pid, CUR_PROCESS_HANDLE) == 0 && GetCurrentProcessId() == pid, "PAL process identity matches Horizon");
    check(OpenProcess(0, FALSE, static_cast<DWORD>(pid + 1)) == nullptr && GetLastError() == ERROR_NOT_SUPPORTED, "foreign process handles explicitly unsupported");
    testChannel();
    testPalThreads();
    FlushProcessWriteBuffers();
    fprintf(output, "PAL_THREADS_CHANNEL_AND_PROCESS_BARRIER_PASSED checks=%u\n", checks.load());
    auto pal = PAL_GetPalHostModule();
    check(pal != nullptr, "real PAL host module initialization");
    check(reinterpret_cast<void*>(GetProcAddress(pal, "ModuleProbeFunction")) == reinterpret_cast<void*>(ModuleProbeFunction), "PAL module bookkeeping resolves exports");
    NativeModuleInfo info{};
    check(NativeModuleFromAddress(reinterpret_cast<void*>(ModuleProbeFunction), &info), "actual NRO address ownership");
    fprintf(output, "resident_path=%s\n", info.name);
    check(PAL_GetSymbolModuleBase(&ModuleProbeData) == info.base, "PAL native module base");
    check(!NativeModuleFromAddress(reinterpret_cast<void*>(1), &info), "reject unknown address");
    check(!NativeOpenModule("module-that-does-not-exist.so") && errno == ENOTSUP, "external modules explicitly unsupported");
    auto reopened = NativeOpenModule(info.name);
    check(reopened != nullptr && NativeCloseModule(reopened) == 0, "reopen actual executable by path");
    check(NativeCloseModule(reinterpret_cast<void*>(1)) == -1, "reject invalid handle");
    int size = PAL_CopyModuleData(info.base, nullptr, nullptr);
    check(size > 0, "query native NRO segment extent");
    auto snapshot = static_cast<unsigned char*>(malloc(size + 32));
    check(snapshot != nullptr, "allocate independent module snapshot");
    memset(snapshot, 0xa5, size + 32);
    check(PAL_CopyModuleData(info.base, snapshot + 16, snapshot + 16 + size - 1) == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "reject short snapshot before copying");
    check(snapshot[16] == 0xa5 && snapshot[size + 15] == 0xa5, "short snapshot writes no bytes");
    check(PAL_CopyModuleData(info.base, snapshot + 16, snapshot + 16 + size) == size, "copy actual loaded NRO segments and BSS");
    auto offset = reinterpret_cast<uintptr_t>(&ModuleProbeData) - reinterpret_cast<uintptr_t>(info.base);
    check(*reinterpret_cast<unsigned*>(snapshot + 16 + offset) == ModuleProbeData, "snapshot preserves exported data");
    check(snapshot[0] == 0xa5 && snapshot[size + 31] == 0xa5, "snapshot respects bounds");
    free(snapshot);
    for (unsigned round = 0; round < 8; ++round) {
        pthread_t threads[4];
        for (auto& thread : threads) check(pthread_create(&thread, nullptr, worker, nullptr) == 0, "create lookup worker");
        for (auto& thread : threads) check(pthread_join(thread, nullptr) == 0, "join lookup worker");
        fprintf(output, "ROUND %u checks=%u native_used=%zu\n", round, checks.load(), mallinfo().uordblks);
    }
    fprintf(output, "PASS checks=%u pal_workers=32 lookup_workers=32 lookups=2048\n", checks.load());
    PAL_Shutdown();
    fprintf(output, "PAL_SHUTDOWN_RETURNED\n");
    fclose(output); return 0;
}
