#include <switch.h>
#include <pthread.h>
#include <cstdio>
#include <cstdint>
#include <malloc.h>
extern "C" { unsigned __nx_applet_exit_mode = 1; int ManagedSocketMain(uintptr_t); }
static FILE* output;
static int complete;
static void Report(int phase, int value) {
    fprintf(output, "MANAGED phase=%d value=%d native_used=%d\n", phase, value, mallinfo().uordblks);
}
static void* Watchdog(void*) {
    uint64_t begin = armGetSystemTick();
    while (!__atomic_load_n(&complete, __ATOMIC_ACQUIRE)) {
        if (armTicksToNs(armGetSystemTick() - begin) >= 120000000000ULL) svcExitProcess();
        svcSleepThread(10000000);
    }
    return nullptr;
}
int main() {
    output = fopen("sdmc:/switch/nativeaot-async-sockets.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    if (!freopen("sdmc:/switch/nativeaot-async-sockets-stderr.txt", "w", stderr) ||
        !freopen("sdmc:/switch/nativeaot-async-sockets-stdout.txt", "w", stdout)) return 1;
    setvbuf(stderr, nullptr, _IONBF, 0);
    setvbuf(stdout, nullptr, _IONBF, 0);
    SocketInitConfig config = *socketGetDefaultInitConfig();
    config.sb_efficiency = 8;
    Result initialized = socketInitialize(&config);
    fprintf(output, "BEGIN NativeAOT async sockets result=%08x sb_efficiency=%u watchdog_seconds=120\n", initialized, config.sb_efficiency);
    if (R_FAILED(initialized)) return 1;
    pthread_t watchdog;
    if (pthread_create(&watchdog, nullptr, Watchdog, nullptr)) return 1;
    int result = ManagedSocketMain(reinterpret_cast<uintptr_t>(Report));
    __atomic_store_n(&complete, 1, __ATOMIC_RELEASE);
    pthread_join(watchdog, nullptr);
    fprintf(output, "END result=%d\n", result);
    fclose(output);
    // Background managed polling owns BSD until process exit.
    return result == 100 ? 0 : 1;
}
