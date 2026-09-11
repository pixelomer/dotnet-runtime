#include <switch.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
extern "C" {
u32 __nx_applet_exit_mode = 1;
int ManagedFileSystemMain();
void LibnxRuntimeDiagnostic(const char* stage)
{
    FILE* f=fopen("sdmc:/switch/nativeaot-filesystem-test.txt","a");
    if (!f) abort();
    fprintf(f,"startup=%s\n",stage); fclose(f);
}
void ProbeReport(int phase,long value)
{
    FILE* f=fopen("sdmc:/switch/nativeaot-filesystem-test.txt","a");
    if (!f) abort();
    fprintf(f,"phase=%d value=%ld native_used=%d\n",phase,value,mallinfo().uordblks); fclose(f);
}
void ProbeError(const char* error) { LibnxRuntimeDiagnostic(error); }
}
int main()
{
    FILE* f=fopen("sdmc:/switch/nativeaot-filesystem-test.txt","w");
    if(!f) return 1;
    fprintf(f,"BEGIN NativeAOT Horizon managed filesystem\n"); fclose(f);
    int result=ManagedFileSystemMain();
    f=fopen("sdmc:/switch/nativeaot-filesystem-test.txt","a");
    if(!f) return 1;
    fprintf(f,"END result=%d\n",result); fclose(f);
    return result;
}
