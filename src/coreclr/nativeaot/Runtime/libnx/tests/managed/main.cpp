#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
extern "C" {
// NativeAOT remains resident for process lifetime. Request a proper application
// exit so hbmenu never inherits live GC threads or aliased heap backing.
u32 __nx_applet_exit_mode = 1;
#include "../../nxvm.h"
int ManagedStressMain();
void LibnxRuntimeDiagnostic(const char* stage)
{
    FILE* file=fopen("sdmc:/switch/nativeaot-managed-stress.txt","a");
    if (!file) abort();
    fprintf(file,"startup=%s\n",stage);
    fclose(file);
}
void ProbeReport(int phase, long value)
{
    FILE* file=fopen("sdmc:/switch/nativeaot-managed-stress.txt","a");
    if (!file) abort();
    fprintf(file,"phase=%d value=%ld\n",phase,value);
    fclose(file);
}
unsigned long ProbeCommittedBytes() { return nxvm_stats().committed; }
}
int main()
{
    FILE* file=fopen("sdmc:/switch/nativeaot-managed-stress.txt","w");
    if (!file) return 1;
    fprintf(file,"BEGIN native launcher; source-built Horizon NativeAOT\n");
    fclose(file);
    int result=0;
    for(int round=0;round<16;round++) {
        result=ManagedStressMain();
        FILE* progress=fopen("sdmc:/switch/nativeaot-managed-stress.txt","a");
        if (!progress) abort();
        auto stats=nxvm_stats();
        auto heap=mallinfo();
        fprintf(progress,"ROUND %d result=%d committed=%zu reserved=%zu native_used=%d\n",
            round,result,stats.committed,stats.reserved,heap.uordblks);
        fclose(progress);
        if(result) break;
    }
    file=fopen("sdmc:/switch/nativeaot-managed-stress.txt","a");
    if (!file) return 1;
    fprintf(file,"END result=%d\n",result);
    fclose(file);
    return result;
}
