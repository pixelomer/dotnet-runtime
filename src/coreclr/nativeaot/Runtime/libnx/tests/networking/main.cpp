#include <switch.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
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
int main()
{
    FILE* f=fopen("sdmc:/switch/nativeaot-networking-test.txt","w");
    if(!f) return 1;
    fprintf(f,"BEGIN NativeAOT Horizon managed networking\n"); fclose(f);
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
