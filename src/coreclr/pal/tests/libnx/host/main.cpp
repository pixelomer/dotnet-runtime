#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <dirent.h>
#ifdef HOST_SOAK_PROBE
#include <malloc.h>
#endif
#include "coreclrhost.h"
#if defined(HOST_SUSPENSION_PROBE) || defined(HOST_BCL_PROBE)
#include <pthread.h>
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/arm/counter.h>
}
#endif
#ifdef HOST_SOCKET_PROBE
extern "C" {
#include <switch.h>
void HostSocketPollBegin();
uint64_t HostSocketPollEnd();
}
static bool excessiveSocketPolls;
#endif
#ifdef HOST_SUSPENSION_PROBE
static int suspensionControl[4]; // stop, timed out, armed generation (-1 exits), completed generation
static uint64_t suspensionStart;
static void* SuspensionWatchdog(void*)
{
    int observed = 0;
    uint64_t begin = 0;
    while (true) {
        int armed = __atomic_load_n(&suspensionControl[2], __ATOMIC_ACQUIRE);
        if (armed < 0) return nullptr;
        if (armed != observed) { observed = armed; begin = armGetSystemTick(); }
        if (armed > __atomic_load_n(&suspensionControl[3], __ATOMIC_ACQUIRE) &&
            armTicksToNs(armGetSystemTick() - begin) >= 3000000000ULL) {
            __atomic_store_n(&suspensionControl[1], 1, __ATOMIC_RELEASE);
            __atomic_store_n(&suspensionControl[0], 1, __ATOMIC_RELEASE);
            return nullptr;
        }
        svcSleepThread(1000000);
    }
}
#endif
#ifdef HOST_BCL_PROBE
static int bclComplete;
#ifdef HOST_SOAK_PROBE
static constexpr uint64_t BclTimeoutSeconds = 240;
#else
static constexpr uint64_t BclTimeoutSeconds = 120;
#endif
static void* BclWatchdog(void*)
{
    uint64_t begin = armGetSystemTick();
    while (!__atomic_load_n(&bclComplete, __ATOMIC_ACQUIRE)) {
        // Do not acquire application or stdio locks after the deadline.
        if (armTicksToNs(armGetSystemTick() - begin) >= BclTimeoutSeconds * 1000000000ULL) svcExitProcess();
        svcSleepThread(10000000);
    }
    return nullptr;
}
#endif
extern "C" { unsigned __nx_applet_exit_mode = 1; }
static FILE* output;
extern "C" void HostProtectionTrace(void* address, size_t size, unsigned protection, int result, unsigned error)
{
    if (output) fprintf(output, "VirtualProtect address=%p size=%zu protect=%x result=%d error=%u\n", address, size, protection, result, error);
}
extern "C" void LibnxRuntimeDiagnostic(const char* text) { if (output) fprintf(output, "NATIVE: %s\n", text); }
extern "C" void HostStackReservationTrace(size_t size, void* result) { if (output) fprintf(output, "StackReservation size=%zu result=%p\n", size, result); }
extern "C" void HostDumpStackMap();
extern "C" void HostFileTrace(const uint16_t* path, void* result, unsigned error)
{
    char text[256]; size_t length = 0;
    if (path) for (; length < sizeof(text) - 1 && path[length]; ++length)
        text[length] = path[length] < 128 ? char(path[length]) : '?';
    text[length] = 0;
    if (output) fprintf(output, "CreateFile path=%s result=%p error=%u\n", text, result, error);
}
extern "C" void HostManagedProgress(int phase, int value) {
#ifdef HOST_SOCKET_PROBE
    if (phase == 85) HostSocketPollBegin();
    if (phase == 86) {
        uint64_t calls = HostSocketPollEnd();
        fprintf(output, "POLL_IDLE calls=%llu window_ms=%d\n", (unsigned long long)calls, value);
        excessiveSocketPolls |= calls > 100;
    }
#endif
#ifdef HOST_SOAK_PROBE
    if (phase >= 90 || phase == 52 || phase == 54) {
        FILE* progress = fopen("sdmc:/switch/coreclr-soak-progress.txt", "a");
        if (progress) {
            uint64_t used = 0;
            Result processResult = svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
            fprintf(progress, "phase=%d value=%d native_used=%zu process_used=%llu process_info_result=%08x\n", phase, value, mallinfo().uordblks, (unsigned long long)used, processResult);
            fclose(progress);
        }
    }
#endif
    if (output) fprintf(output, "MANAGED phase=%d value=%d\n", phase, value);
#ifdef HOST_SUSPENSION_PROBE
    if (phase == 50) {
        suspensionStart = armGetSystemTick();
        __atomic_add_fetch(&suspensionControl[2], 1, __ATOMIC_RELEASE);
    }
    if (phase == 51) {
        __atomic_store_n(&suspensionControl[3], __atomic_load_n(&suspensionControl[2], __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
        if (output) fprintf(output, "GC elapsed_ns=%llu\n", (unsigned long long)armTicksToNs(armGetSystemTick() - suspensionStart));
    }
    if (phase == 53) {
        __atomic_store_n(&suspensionControl[0], 1, __ATOMIC_RELEASE);
        __atomic_store_n(&suspensionControl[2], -1, __ATOMIC_RELEASE);
    }
#endif
}
extern "C" void HostFlushDiagnostics() { fflush(nullptr); }
static void error_writer(const char* text) { fprintf(output, "CORECLR: %s\n", text); }
int main(int argc, char** argv)
{
    output = fopen("sdmc:/switch/coreclr-host-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    if (freopen("sdmc:/switch/coreclr-host-stderr.txt", "w", stderr))
        setvbuf(stderr, nullptr, _IONBF, 0);
    if (freopen("sdmc:/switch/coreclr-host-stdout.txt", "w", stdout))
        setvbuf(stdout, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN embedded CoreCLR host initialize=%p\n", reinterpret_cast<void*>(coreclr_initialize));
    if (argc < 1 || !argv[0]) { fprintf(output, "FAIL missing loader executable path\n"); return 1; }
    // The filesystem FIFO debugger transport is explicitly unsupported.
    // Exercise ordinary runtime startup with the upstream opt-out setting.
    setenv("DOTNET_EnableDiagnostics_Debugger", "0", 1);
#ifdef HOST_SUSPENSION_PROBE
    setenv("DOTNET_TieredCompilation", "0", 1);
#endif
#ifdef HOST_MINOPTS
    setenv("DOTNET_JITMinOpts", "1", 1);
#endif
#ifdef HOST_JIT_TRACE
    remove("sdmc:/switch/coreclr-jit-disasm.txt");
    setenv("DOTNET_JitStdOutFile", "sdmc:/switch/coreclr-jit-disasm.txt", 1);
    setenv("DOTNET_JitDisasm", "*", 1);
    setenv("DOTNET_JitDisasmSummary", "1", 1);
    setenv("DOTNET_JitDisasmWithCodeBytes", "1", 1);
#endif
#ifdef HOST_SOCKET_PROBE
    SocketInitConfig socketConfig = *socketGetDefaultInitConfig();
    socketConfig.sb_efficiency = 8;
    Result socketResult = socketInitialize(&socketConfig);
    fprintf(output, "SOCKET_INIT result=%08x sb_efficiency=%u\n", socketResult, socketConfig.sb_efficiency);
    if (R_FAILED(socketResult)) return 1;
    // The socket engine is a background thread. BSD remains alive until exit.
#endif
#ifdef HOST_SOAK_PROBE
    remove("sdmc:/switch/coreclr-soak-progress.txt");
    setenv("DOTNET_TieredCompilation", "1", 1);
    setenv("DOTNET_TC_QuickJit", "1", 1);
    setenv("DOTNET_TC_QuickJitForLoops", "1", 1);
    setenv("DOTNET_TC_AggressiveTiering", "1", 1);
    setenv("DOTNET_TC_CallCountingDelayMs", "0", 1);
#ifdef HOST_JIT_TRACE
    setenv("DOTNET_JitDisasm", "Soak:HotLoop", 1);
#endif
#endif
    coreclr_set_error_writer(error_writer);
    void* host = nullptr;
    unsigned domain = 0;
    std::string platformAssemblies;
    DIR* assemblies = opendir("sdmc:/switch/coreclr-probe");
    if (!assemblies) { fprintf(output, "FAIL missing managed deployment\n"); return 1; }
    while (dirent* entry = readdir(assemblies)) {
        size_t length = strlen(entry->d_name);
        if (length < 4 || strcmp(entry->d_name + length - 4, ".dll") != 0 || strcmp(entry->d_name, "Probe.dll") == 0) continue;
        if (!platformAssemblies.empty()) platformAssemblies += ':';
        platformAssemblies += "/switch/coreclr-probe/";
        platformAssemblies += entry->d_name;
    }
    closedir(assemblies);
    const char* keys[] = {"APP_PATHS", "TRUSTED_PLATFORM_ASSEMBLIES", "System.Globalization.Invariant"};
    const char* values[] = {"/switch/coreclr-probe", platformAssemblies.c_str(), "true"};
    int result = coreclr_initialize(argv[0], "Horizon CoreCLR probe", 3, keys, values, &host, &domain);
    fprintf(output, "coreclr_initialize result=%08x host=%p domain=%u\n", result, host, domain);
    if (result < 0) HostDumpStackMap();
    if (result >= 0) {
        unsigned exit_code = 0;
        char reporter[32];
        snprintf(reporter, sizeof(reporter), "%llu", (unsigned long long)reinterpret_cast<uintptr_t>(HostManagedProgress));
        const char* arguments[] = {reporter};
#ifdef HOST_BCL_PROBE
        pthread_t bclWatchdog;
        fprintf(output, "BCL_WATCHDOG timeout_seconds=%llu\n", (unsigned long long)BclTimeoutSeconds);
        if (pthread_create(&bclWatchdog, nullptr, BclWatchdog, nullptr) != 0) abort();
#endif
#ifdef HOST_SUSPENSION_PROBE
        pthread_t watchdog;
        if (pthread_create(&watchdog, nullptr, SuspensionWatchdog, nullptr) != 0) abort();
        char control[32];
        snprintf(control, sizeof(control), "%llu", (unsigned long long)reinterpret_cast<uintptr_t>(suspensionControl));
        const char* suspensionArguments[] = {reporter, control};
        result = coreclr_execute_assembly(host, domain, 2, suspensionArguments, "/switch/coreclr-probe/Probe.dll", &exit_code);
        // Also release the native waiter if managed startup returned early.
        __atomic_store_n(&suspensionControl[2], -1, __ATOMIC_RELEASE);
        pthread_join(watchdog, nullptr);
#else
        result = coreclr_execute_assembly(host, domain, 1, arguments, "/switch/coreclr-probe/Probe.dll", &exit_code);
#endif
#ifdef HOST_BCL_PROBE
        __atomic_store_n(&bclComplete, 1, __ATOMIC_RELEASE);
        pthread_join(bclWatchdog, nullptr);
#endif
#ifdef HOST_SOCKET_PROBE
        if (excessiveSocketPolls) { fprintf(output, "FAIL excessive idle socket polls\n"); exit_code = 112; }
#endif
        fprintf(output, "coreclr_execute_assembly result=%08x exit=%u\n", result, exit_code);
        int latched_exit = 0;
        result = coreclr_shutdown_2(host, domain, &latched_exit);
        fprintf(output, "coreclr_shutdown result=%08x exit=%d\n", result, latched_exit);
    }
    fclose(output);
    output = nullptr;
    return result < 0;
}
