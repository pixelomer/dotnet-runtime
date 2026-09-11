#include <switch.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
extern "C" {
u32 __nx_applet_exit_mode = 1;
int ManagedCallbacksMain();
extern unsigned char LibnxThunkData[];
void LibnxRuntimeDiagnostic(const char* stage)
{
    FILE* f=fopen("sdmc:/switch/nativeaot-callback-test.txt","a");
    if (!f) abort();
    fprintf(f,"startup=%s\n",stage); fclose(f);
}
void ProbeReport(int phase,long value)
{
    FILE* f=fopen("sdmc:/switch/nativeaot-callback-test.txt","a");
    if (!f) abort();
    fprintf(f,"phase=%d value=%ld native_used=%d\n",phase,value,mallinfo().uordblks); fclose(f);
}
using IntegerCallback=long (*)(long,long,long,long,long,long,long,long,long,long);
struct Work { IntegerCallback callback; long result; };
static void* worker(void* opaque)
{
    auto* work=static_cast<Work*>(opaque);
    work->result=work->callback(1,2,3,4,5,6,7,8,9,10);
    return nullptr;
}
long InvokeInteger(IntegerCallback callback,int useWorker)
{
    Work work{callback,0};
    if (useWorker) {
        pthread_t thread;
        if(pthread_create(&thread,nullptr,worker,&work) || pthread_join(thread,nullptr)) abort();
    } else worker(&work);
    return work.result;
}
double InvokeFloat(double (*callback)(double,double,double,double,double,double,double,double,double,double))
{
    return callback(1,2,3,4,5,6,7,8,9,10);
}
int CheckThunkPermissions(void* code)
{
    MemoryInfo ci{},di{}; u32 page;
    return R_SUCCEEDED(svcQueryMemory(&ci,&page,(u64)code)) &&
        R_SUCCEEDED(svcQueryMemory(&di,&page,(u64)LibnxThunkData)) &&
        ci.perm==Perm_Rx && di.perm==Perm_Rw;
}
}
int main()
{
    FILE* f=fopen("sdmc:/switch/nativeaot-callback-test.txt","w");
    if(!f) return 1;
    fprintf(f,"BEGIN NativeAOT delegate callback pool\n"); fclose(f);
    int result=ManagedCallbacksMain();
    f=fopen("sdmc:/switch/nativeaot-callback-test.txt","a");
    if(!f) return 1;
    fprintf(f,"END result=%d\n",result); fclose(f);
    return result;
}
