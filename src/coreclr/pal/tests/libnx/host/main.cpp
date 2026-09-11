#include <cstdio>
#include <cstdlib>
#include "coreclrhost.h"
extern "C" { unsigned __nx_applet_exit_mode = 1; }
static FILE* output;
extern "C" void HostProtectionTrace(void* address, size_t size, unsigned protection, int result, unsigned error)
{
    fprintf(output, "VirtualProtect address=%p size=%zu protect=%x result=%d error=%u\n", address, size, protection, result, error);
}
extern "C" void LibnxRuntimeDiagnostic(const char* text) { fprintf(output, "NATIVE: %s\n", text); }
extern "C" void HostStackReservationTrace(size_t size, void* result) { fprintf(output, "StackReservation size=%zu result=%p\n", size, result); }
extern "C" void HostDumpStackMap();
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
    fprintf(output, "BEGIN embedded CoreCLR host\n");
    if (argc < 1 || !argv[0]) { fprintf(output, "FAIL missing loader executable path\n"); return 1; }
    // The filesystem FIFO debugger transport is explicitly unsupported.
    // Exercise ordinary runtime startup with the upstream opt-out setting.
    setenv("DOTNET_EnableDiagnostics_Debugger", "0", 1);
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
        result = coreclr_execute_assembly(host, domain, 0, nullptr, "/switch/coreclr-probe/Probe.dll", &exit_code);
        fprintf(output, "coreclr_execute_assembly result=%08x exit=%u\n", result, exit_code);
        int latched_exit = 0;
        result = coreclr_shutdown_2(host, domain, &latched_exit);
        fprintf(output, "coreclr_shutdown result=%08x exit=%d\n", result, latched_exit);
    }
    fclose(output);
    return result < 0;
}
