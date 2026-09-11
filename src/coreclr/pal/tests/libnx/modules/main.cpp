#include "pal/thread.hpp"
#include "pal/modulenative.h"
#include <atomic>
#include <malloc.h>
extern "C" {
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
int main(int argc, char** argv) {
    output = fopen("sdmc:/switch/coreclr-module-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN production PAL native module boundary; no managed runtime\n");
    if (argc == 0) { fprintf(output, "FAIL loader provided no argv\n"); fclose(output); return 1; }
    check(pthread_key_create(&CorUnix::thObjKey, nullptr) == 0, "initialize real PAL TLS key for native boundary test");
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
    fprintf(output, "PASS checks=%u workers=32 lookups=2048\n", checks.load());
    check(pthread_key_delete(CorUnix::thObjKey) == 0, "release native test PAL TLS key");
    fclose(output); return 0;
}
