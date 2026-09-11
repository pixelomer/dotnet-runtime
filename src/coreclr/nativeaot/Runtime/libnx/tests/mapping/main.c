#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>
#include "../../nxvm.h"
extern void* SystemNative_MMap(void*,uint64_t,int32_t,int32_t,intptr_t,int64_t);
extern int32_t SystemNative_MUnmap(void*,uint64_t);
extern int32_t SystemNative_MProtect(void*,uint64_t,int32_t);
static FILE* logFile;
static unsigned checks,failures;
#define CHECK(expr) do { checks++; if(!(expr)) { failures++; fprintf(logFile,"FAIL line=%d errno=%d\n",__LINE__,errno); fflush(logFile); } } while(0)
enum { NONE=0, READ=1, RW=3, EXEC=4, PRIVATE_ANON=0x12 };
static void check_zero(const unsigned char* p,size_t length)
{
    if(!p) return;
    for(size_t i=0;i<length;i++) CHECK(p[i]==0);
}
int main(void)
{
    logFile=fopen("sdmc:/switch/nativeaot-mapping-test.txt","w");
    if(!logFile) return 1;
    CHECK(nxvm_init(4*1024*1024));
    CHECK(nxvm_ensure_initialized(512*1024*1024));
    CHECK(nxvm_stats().capacity==4*1024*1024);
    unsigned char* p=SystemNative_MMap(NULL,4*1024*1024,NONE,PRIVATE_ANON,-1,0);
    CHECK(p!=NULL);
    if(!p) goto done;
    MemoryInfo mi; u32 page;
    CHECK(R_SUCCEEDED(svcQueryMemory(&mi,&page,(u64)p)) && mi.type==MemType_Unmapped && mi.perm==Perm_None);
    CHECK(nxvm_stats().committed==0);
    CHECK(SystemNative_MProtect(p,65536,RW)==0);
    CHECK(R_SUCCEEDED(svcQueryMemory(&mi,&page,(u64)p)) && mi.perm==Perm_Rw);
    check_zero(p,65536);
    memset(p,0xa5,65536);
    CHECK(SystemNative_MProtect(p,65536,RW)==0);
    for(size_t i=0;i<65536;i++) CHECK(p[i]==0xa5);
    CHECK(SystemNative_MProtect(p+65536,65536,RW)==0);
    check_zero(p+65536,65536);
    CHECK(nxvm_stats().committed==131072);
    CHECK(SystemNative_MProtect(p,65536,NONE)==-1 && errno==ENOTSUP);
    CHECK(SystemNative_MProtect(p,65536,READ)==-1 && errno==ENOTSUP);
    CHECK(SystemNative_MProtect(p,65536,EXEC)==-1 && errno==ENOTSUP);
    CHECK(p[0]==0xa5 && p[65535]==0xa5);
    CHECK(SystemNative_MUnmap(p,4096)==-1 && errno==EINVAL);
    CHECK(p[0]==0xa5);
    CHECK(SystemNative_MUnmap(p,4*1024*1024)==0);
    CHECK(nxvm_stats().committed==0 && nxvm_stats().reserved==0);
    CHECK(SystemNative_MUnmap(p,4*1024*1024)==-1 && errno==EINVAL);
    int nativeUsed=-1;
    for(int cycle=0;cycle<1024;cycle++) {
        p=SystemNative_MMap(NULL,16383,RW,PRIVATE_ANON,-1,0);
        CHECK(p!=NULL);
        if(!p) break;
        check_zero(p,16383);
        memset(p,0x5a,16383);
        CHECK(SystemNative_MUnmap(p,16383)==0);
        CHECK(nxvm_stats().committed==0 && nxvm_stats().reserved==0);
        if(cycle==0) nativeUsed=mallinfo().uordblks;
    }
    CHECK(mallinfo().uordblks==nativeUsed);
    CHECK(SystemNative_MMap(NULL,4096,EXEC,PRIVATE_ANON,-1,0)==NULL && errno==ENOTSUP);
    CHECK(SystemNative_MMap(NULL,4096,RW,0x11,-1,0)==NULL && errno==ENOTSUP);
    CHECK(SystemNative_MMap(NULL,4096,RW,PRIVATE_ANON,0,0)==NULL && errno==ENOTSUP);
    CHECK(SystemNative_MMap(NULL,0,RW,PRIVATE_ANON,-1,0)==NULL && errno==EINVAL);
    void* ordinary=malloc(4096);
    CHECK(ordinary!=NULL);
    CHECK(SystemNative_MUnmap(ordinary,4096)==-1 && errno==EINVAL);
    free(ordinary);
done:
    CHECK(nxvm_destroy());
    fprintf(logFile,"END checks=%u failures=%u\n",checks,failures);
    fclose(logFile);
    return failures ? 1 : 0;
}
