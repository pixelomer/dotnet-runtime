#include "pal/thread.hpp"
#include "pal/modulenative.h"
#include "pal/libnx/workerchannel.h"
#include "pal/threadnative.h"
#include "pal/process.h"
#include <atomic>
#include <malloc.h>
#include <fcntl.h>
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/arm/counter.h>
unsigned __nx_applet_exit_mode = 1;
__thread uintptr_t ProbeTlsValue = 0xabc123;
uintptr_t* ProbeAssemblyTls();
uintptr_t* ProbeNativeAotAssemblyTls();
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
static void testAssemblyTls() {
    check(ProbeAssemblyTls() == &ProbeTlsValue && *ProbeAssemblyTls() == 0xabc123, "assembly TLS agrees with compiler TLS on a fresh thread");
    check(ProbeNativeAotAssemblyTls() == &ProbeTlsValue && *ProbeNativeAotAssemblyTls() == 0xabc123, "NativeAOT assembly TLS agrees with compiler TLS");
    *ProbeAssemblyTls() = reinterpret_cast<uintptr_t>(&ProbeTlsValue);
    check(*ProbeNativeAotAssemblyTls() == reinterpret_cast<uintptr_t>(&ProbeTlsValue), "assembly TLS write reaches this thread's actual variable");
}
static void* worker(void*) {
    testAssemblyTls();
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
    testAssemblyTls();
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
static void testMemoryProbe() {
    auto memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 12288, MEM_RESERVE, PAGE_READWRITE));
    check(memory != nullptr, "reserve PAL probe range");
    check(!PAL_ProbeMemory(memory, 1, FALSE), "probe rejects uncommitted address");
    check(VirtualAlloc(memory, 8192, MEM_COMMIT, PAGE_READWRITE) == memory, "commit two probe pages");
    memset(memory, 0x69, 8192);
    check(PAL_ProbeMemory(memory + 4095, 2, TRUE), "writable probe crosses two committed pages");
    check(!PAL_ProbeMemory(memory + 8191, 2, FALSE), "probe rejects trailing uncommitted page");
    DWORD old;
    check(!VirtualProtect(memory + 4096, 4096, PAGE_READONLY, &old) && GetLastError() == ERROR_NOT_SUPPORTED, "data allocator rejects unsupported protection change");
    check(PAL_ProbeMemory(memory, 8192, FALSE), "readable probe spans both committed pages");
    alignas(4096) static const unsigned char readOnly[4096] = {0x45};
    check(PAL_ProbeMemory(const_cast<unsigned char*>(readOnly), sizeof(readOnly), FALSE), "resident read-only data is readable");
    check(!PAL_ProbeMemory(const_cast<unsigned char*>(readOnly), sizeof(readOnly), TRUE), "resident read-only data rejects write");
    auto mutableView = const_cast<volatile unsigned char*>(readOnly);
    for (unsigned life = 0; life < 256; ++life) {
        check(VirtualProtect(const_cast<unsigned char*>(readOnly), 4096, PAGE_READWRITE, &old) && old == PAGE_READONLY, "enable resident data initialization using kernel permission");
        mutableView[0] = static_cast<unsigned char>(life);
        check(VirtualProtect(const_cast<unsigned char*>(readOnly), 4096, PAGE_READONLY, &old) && old == PAGE_READWRITE, "restore resident data read-only protection");
        check(mutableView[0] == static_cast<unsigned char>(life) && !PAL_ProbeMemory(const_cast<unsigned char*>(readOnly), 4096, TRUE), "resident data retains bytes and rejects writes after initialization");
    }
    check(!VirtualProtect(reinterpret_cast<void*>(ModuleProbeFunction), 4, PAGE_READWRITE, &old), "reject destructive writable transition of resident text");
    check(ModuleProbeFunction(7) == 52, "resident text still executes after rejected transition");
    check(memory[0] == 0x69 && memory[8191] == 0x69, "probe leaves caller bytes unchanged");
    check(!PAL_ProbeMemory(reinterpret_cast<void*>(UINTPTR_MAX - 3), 8, FALSE), "probe rejects address overflow");
    check(PAL_ProbeMemory(nullptr, 0, FALSE), "empty range is vacuously valid");
    check(!PAL_ProbeMemory(nullptr, 1, FALSE), "null nonempty range is unreadable");
    check(VirtualFree(memory, 0, MEM_RELEASE), "release probe range");
    check(!PAL_ProbeMemory(memory, 1, FALSE), "probe rejects retired mapping");
    check(PAL_ProbeMemory(reinterpret_cast<void*>(ModuleProbeFunction), 4, FALSE), "probe sees resident executable outside PAL allocator");
    check(!PAL_ProbeMemory(reinterpret_cast<void*>(ModuleProbeFunction), 4, TRUE), "probe rejects write into resident executable");
    auto mutex = CreateMutexW(nullptr, FALSE, W("coreclr-horizon-unsupported-named-mutex"));
    check(mutex == nullptr && GetLastError() == ERROR_NOT_SUPPORTED, "named mutex rejects unsupported shared namespace");
    mutex = CreateMutexW(nullptr, FALSE, nullptr);
    check(mutex != nullptr && WaitForSingleObject(mutex, 0) == WAIT_OBJECT_0, "unnamed mutex retains normal acquisition");
    check(ReleaseMutex(mutex) && CloseHandle(mutex), "unnamed mutex retains release and ownership retirement");
}
static bool testMappingFailures() {
    const char* path = "sdmc:/switch/coreclr-pal-empty-input.bin";
    FILE* seed = fopen(path, "wb");
    check(seed != nullptr && fclose(seed) == 0, "create test-owned empty input");
    auto empty = CreateFileW(W("sdmc:/switch/coreclr-pal-empty-input.bin"), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(empty != INVALID_HANDLE_VALUE, "open empty PAL input");
    // Put a real file at descriptor zero so accidental cleanup is observable.
    // Preserve and restore stdin even on the expected pre-fix regression path.
    int savedInput = dup(0);
    int control = open("sdmc:/switch/coreclr-pal-file-input.bin", O_RDONLY);
    check(savedInput >= 0 && control >= 0 && dup2(control, 0) == 0, "install descriptor-zero sentinel");
    int nextDescriptor = dup(control);
    check(nextDescriptor >= 0 && close(nextDescriptor) == 0, "record available descriptor before failed mappings");
    unsigned damaged = 0;
    for (unsigned life = 0; life < 64; ++life) {
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            HANDLE mapping;
            if (scenario == 0) mapping = CreateFileMappingW(nullptr, nullptr, PAGE_READONLY, 0, 0, nullptr);
            else if (scenario == 1) mapping = CreateFileMappingW(empty, nullptr, PAGE_READWRITE, 0, 4, nullptr);
            else if (scenario == 2) mapping = CreateFileMappingW(empty, nullptr, PAGE_READONLY, 0, 0, nullptr);
            else mapping = CreateFileMappingW(empty, nullptr, PAGE_READONLY, 0, 4, nullptr);
            DWORD error = GetLastError();
            const DWORD expected[] = {ERROR_INVALID_PARAMETER, ERROR_ACCESS_DENIED, ERROR_FILE_INVALID, ERROR_NOT_ENOUGH_MEMORY};
            check(mapping == nullptr && error == expected[scenario], "mapping failure retains its error contract");
            char bytes[4];
            bool intact = lseek(0, 0, SEEK_SET) == 0 && read(0, bytes, 4) == 4 && memcmp(bytes, "FILE", 4) == 0;
            if (!intact) {
                ++damaged;
                check(dup2(control, 0) == 0, "repair sentinel after observed pre-fix cleanup failure");
            }
            check(lseek(control, 0, SEEK_SET) == 0 && read(control, bytes, 4) == 4 && memcmp(bytes, "FILE", 4) == 0,
                "failed mapping preserves independent control descriptor");
            int available = dup(control);
            check(available == nextDescriptor && close(available) == 0, "failed mapping does not leak duplicated descriptors");
        }
    }
    auto file = CreateFileW(W("sdmc:/switch/coreclr-pal-file-input.bin"), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(file != INVALID_HANDLE_VALUE && close(0) == 0, "make descriptor zero available for a successful mapping");
    auto mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    check(mapping != nullptr, "successful mapping can own descriptor zero");
    auto view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 4);
    check(view != nullptr && memcmp(view, "FILE", 4) == 0 && CloseHandle(mapping), "live view retains mapping with descriptor zero");
    char bytes[4];
    check(lseek(0, 0, SEEK_SET) == 0 && read(0, bytes, 4) == 4 && memcmp(bytes, "FILE", 4) == 0,
        "mapping descriptor zero remains live until final view retirement");
    check(UnmapViewOfFile(view) && CloseHandle(file), "retire mapping owning descriptor zero");
    errno = 0;
    check(read(0, bytes, 1) == -1 && errno == EBADF, "successful mapping closes its owned descriptor zero");
    check(dup2(savedInput, 0) == 0 && close(savedInput) == 0 && close(control) == 0, "restore original stdin and sentinel ownership");
    check(CloseHandle(empty) && unlink(path) == 0, "retire empty test input");
    fprintf(output, "MAPPING_FAILURES attempts=256 damaged_descriptor_zero=%u\n", damaged);
    return damaged == 0;
}
static bool testFiles() {
    char path[512];
    check(GetFullPathNameA("sdmc:/switch/.././switch/missing-coreclr-probe.dll", sizeof(path), path, nullptr) &&
          strcmp(path, "sdmc:/switch/missing-coreclr-probe.dll") == 0, "mounted absolute path is not prefixed with cwd");
    check(GetFullPathNameA("sdmc:/../../switch/.", sizeof(path), path, nullptr) &&
          strcmp(path, "sdmc:/switch") == 0, "mounted root survives parent normalization");
    check(GetFullPathNameA("sdmc:/switch/..", sizeof(path), path, nullptr) &&
          strcmp(path, "sdmc:/") == 0, "mounted trailing parent resolves to device root");
    check(GetFullPathNameA("/switch/../switch", sizeof(path), path, nullptr) &&
          strcmp(path, "/switch") == 0, "default device absolute path retains Unix spelling");
    auto missing = CreateFileW(W("sdmc:/switch/missing-coreclr-probe.dll"), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(missing == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, "missing mounted file has FILE_NOT_FOUND result");
    // Close the native writer before opening the file through PAL: Horizon FS
    // does not promise simultaneous read access to an active write handle.
    FILE* seed = fopen("sdmc:/switch/coreclr-pal-file-input.bin", "wb");
    check(seed != nullptr, "create native input for PAL file test");
    check(fwrite("FILE", 1, 4, seed) == 4 && fclose(seed) == 0, "finish native input before PAL open");
    errno = 0;
    check(fcntl(0, F_DUPFD_CLOEXEC, 0) == -1 && errno == EOPNOTSUPP, "unsupported libnx fcntl cannot masquerade as a descriptor");
    for (unsigned life = 0; life < 64; ++life) {
        auto file = CreateFileW(W("sdmc:/switch/coreclr-pal-file-input.bin"), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "PAL opens actual native test input");
        unsigned char magic[2]; DWORD read = 0;
        check(ReadFile(file, magic, sizeof(magic), &read, nullptr) && read == 2 && magic[0] == 'F' && magic[1] == 'I', "PAL reads real file contents");
        auto mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        check(mapping != nullptr, "mapping duplicates a real native file description");
        auto view = static_cast<const unsigned char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 4));
        check(view != nullptr && memcmp(view, "FILE", 4) == 0, "PAL creates a real read-only file snapshot");
        check(ReadFile(file, magic, 2, &read, nullptr) && read == 2 && magic[0] == 'L' && magic[1] == 'E', "mapping read preserves the shared native file cursor");
        check(UnmapViewOfFile(view) && CloseHandle(file), "retire first view and original file handle");
        view = static_cast<const unsigned char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 4));
        check(view != nullptr && memcmp(view, "FILE", 4) == 0, "mapping remains usable after original file closes");
        check(CloseHandle(mapping), "close mapping handle with live view");
        auto control = CreateFileW(W("sdmc:/switch/coreclr-pal-file-input.bin"), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(control != INVALID_HANDLE_VALUE && UnmapViewOfFile(view), "retire last view while another file handle is alive");
        check(ReadFile(control, magic, 2, &read, nullptr) && read == 2 && magic[0] == 'F', "mapping retirement did not close another descriptor");
        check(CloseHandle(control), "retire final test file handle");
    }
    bool failuresPass = testMappingFailures();
    check(unlink("sdmc:/switch/coreclr-pal-file-input.bin") == 0, "retire test-owned input");
    return failuresPass;
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
    testAssemblyTls();
    if (!testFiles()) {
        fprintf(output, "FAIL mapping failure cleanup damaged an unrelated descriptor\n");
        PAL_Shutdown();
        fclose(output);
        return 1;
    }
    testMemoryProbe();
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
