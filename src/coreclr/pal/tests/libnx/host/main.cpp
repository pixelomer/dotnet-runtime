#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "coreclrhost.h"
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
extern "C" void HostManagedProgress(int phase, int value) { if (output) fprintf(output, "MANAGED phase=%d value=%d\n", phase, value); }
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
#ifdef HOST_JIT_TRACE
    remove("sdmc:/switch/coreclr-jit-disasm.txt");
    setenv("DOTNET_JitStdOutFile", "sdmc:/switch/coreclr-jit-disasm.txt", 1);
    setenv("DOTNET_JitDisasm", "*", 1);
    setenv("DOTNET_JitDisasmSummary", "1", 1);
    setenv("DOTNET_JitDisasmWithCodeBytes", "1", 1);
#endif
    coreclr_set_error_writer(error_writer);
    void* host = nullptr;
    unsigned domain = 0;
    const char* keys[] = {"APP_PATHS", "TRUSTED_PLATFORM_ASSEMBLIES", "System.Globalization.Invariant"};
    const char* values[] = {"/switch/coreclr-probe", "/switch/coreclr-probe/System.Private.CoreLib.dll", "true"};
    int result = coreclr_initialize(argv[0], "Horizon CoreCLR probe", 3, keys, values, &host, &domain);
    fprintf(output, "coreclr_initialize result=%08x host=%p domain=%u\n", result, host, domain);
    if (result < 0) HostDumpStackMap();
    if (result >= 0) {
        unsigned exit_code = 0;
        char reporter[32];
        snprintf(reporter, sizeof(reporter), "%llu", (unsigned long long)reinterpret_cast<uintptr_t>(HostManagedProgress));
        const char* arguments[] = {reporter};
        result = coreclr_execute_assembly(host, domain, 1, arguments, "/switch/coreclr-probe/Probe.dll", &exit_code);
        fprintf(output, "coreclr_execute_assembly result=%08x exit=%u\n", result, exit_code);
        int latched_exit = 0;
        result = coreclr_shutdown_2(host, domain, &latched_exit);
        fprintf(output, "coreclr_shutdown result=%08x exit=%d\n", result, latched_exit);
    }
    fclose(output);
    output = nullptr;
    return result < 0;
}
