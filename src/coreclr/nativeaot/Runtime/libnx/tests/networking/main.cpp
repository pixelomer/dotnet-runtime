#include <switch.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <unicode/udata.h>
#include <unicode/ulocdata.h>
extern "C" {
u32 __nx_applet_exit_mode = 1;
int ManagedNetworkingMain();
void LibnxRuntimeDiagnostic(const char* stage)
{
    FILE* f=fopen("sdmc:/switch/nativeaot-networking-test.txt","a");
    if (!f) abort();
    fprintf(f,"startup=%s\n",stage); fclose(f);
}
void ProbeReport(int phase,long value)
{
    FILE* f=fopen("sdmc:/switch/nativeaot-networking-test.txt","a");
    if (!f) abort();
    fprintf(f,"phase=%d value=%ld native_used=%d\n",phase,value,mallinfo().uordblks); fclose(f);
}
void ProbeError(const char* error) { LibnxRuntimeDiagnostic(error); }
}
static bool initialize_icu()
{
    if (R_FAILED(romfsInit())) return false;
    FILE* data_file = fopen("romfs:/icudt77l.dat", "rb");
    if (!data_file) return false;
    if (fseek(data_file, 0, SEEK_END)) { fclose(data_file); return false; }
    long length = ftell(data_file);
    if (length <= 0 || fseek(data_file, 0, SEEK_SET)) { fclose(data_file); return false; }
    void* data = malloc((size_t)length);
    if (!data) { fclose(data_file); return false; }
    size_t read = fread(data, 1, (size_t)length, data_file);
    fclose(data_file);
    if (read != (size_t)length) { free(data); return false; }
    UErrorCode status = U_ZERO_ERROR;
    udata_setCommonData(data, &status);
    UVersionInfo version;
    if (U_SUCCESS(status)) ulocdata_getCLDRVersion(version, &status);
    // ICU may retain the buffer even on failure. Keep it until process exit.
    if (U_FAILURE(status)) { fprintf(stderr, "ICU initialization failed: %s\n", u_errorName(status)); return false; }
    fprintf(stderr, "ICU CLDR %u.%u.%u.%u\n", version[0],version[1],version[2],version[3]);
    return true;
}
int main()
{
    if (!freopen("sdmc:/switch/nativeaot-networking-stdout.txt", "w", stdout) ||
        !freopen("sdmc:/switch/nativeaot-networking-stderr.txt", "w", stderr)) return 1;
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    FILE* f=fopen("sdmc:/switch/nativeaot-networking-test.txt","w");
    if(!f) return 1;
    fprintf(f,"BEGIN NativeAOT Horizon managed networking\n"); fclose(f);
    if (!initialize_icu()) return 1;
    Result sockets = socketInitializeDefault();
    ProbeReport(0, sockets);
    if (R_FAILED(sockets)) return 1;
    int result=ManagedNetworkingMain();
    f=fopen("sdmc:/switch/nativeaot-networking-test.txt","a");
    if(!f) return 1;
    fprintf(f,"END result=%d\n",result); fclose(f);
    // The managed socket engine owns a background poll thread for process life.
    // Keep BSD services alive until application exit; do not tear them down
    // while that thread can still be polling or waking from its idle sleep.
    return result;
}
