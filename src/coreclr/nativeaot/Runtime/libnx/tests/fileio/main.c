#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
extern int32_t SystemNative_PRead(intptr_t,void*,int32_t,int64_t);
extern int32_t SystemNative_PWrite(intptr_t,void*,int32_t,int64_t);
extern int32_t SystemNative_Read(intptr_t,void*,int32_t);
extern int32_t SystemNative_Write(intptr_t,const void*,int32_t);
extern int64_t SystemNative_LSeek(intptr_t,int64_t,int32_t);
extern int32_t SystemNative_Close(intptr_t);
extern intptr_t SystemNative_Dup(intptr_t);
static int fd;
static unsigned failures, checks;
static FILE* logFile;
static void CheckAt(bool condition,int line) {
    __atomic_add_fetch(&checks,1,__ATOMIC_RELAXED);
    if (!condition) { __atomic_add_fetch(&failures,1,__ATOMIC_RELAXED);
        fprintf(logFile,"FAIL line=%d errno=%d\n",line,errno);fflush(logFile); }
}
#define Check(condition) CheckAt((condition),__LINE__)
static void* Worker(void* arg) {
    unsigned index=(uintptr_t)arg;
    unsigned char data[256],readback[256];
    for(unsigned round=0;round<1000;round++) {
        memset(data,(unsigned char)(round+index),sizeof(data));
        int64_t offset=index*sizeof(data);
        Check(SystemNative_PWrite(fd,data,sizeof(data),offset)==sizeof(data));
        Check(SystemNative_PRead(fd,readback,sizeof(readback),offset)==sizeof(readback));
        Check(memcmp(data,readback,sizeof(data))==0);
    }
    return NULL;
}
int main(void) {
    const char* name="sdmc:/switch/nativeaot-fileio-test.bin";
    FILE* log=fopen("sdmc:/switch/nativeaot-fileio-test.txt","w");
    if(!log) return 1;
    logFile=log;
    fd=open(name,O_RDWR|O_CREAT|O_EXCL,0600);
    if(fd<0) {fprintf(log,"FAIL exclusive create errno=%d\n",errno);fclose(log);return 1;}
    Check(SystemNative_LSeek(fd,123,SEEK_SET)==123);
    pthread_t threads[4];unsigned started=0;
    for(unsigned i=0;i<4;i++) {
        int error=pthread_create(&threads[i],NULL,Worker,(void*)(uintptr_t)i);
        Check(error==0);if(error)break;started++;
    }
    for(unsigned i=0;i<started;i++) Check(pthread_join(threads[i],NULL)==0);
    Check(lseek(fd,0,SEEK_CUR)==123);
    unsigned char data[16];
    Check(SystemNative_PRead(fd,data,sizeof(data),4096)==0);
    Check(lseek(fd,0,SEEK_CUR)==123);
    Check(SystemNative_PRead(fd,data,sizeof(data),1024)==0);
    Check(SystemNative_PRead(fd,data,sizeof(data),1020)==4);
    Check(lseek(fd,0,SEEK_CUR)==123);
    errno=0;Check(SystemNative_PRead(fd,data,sizeof(data),-1)==-1 && errno==EINVAL);
    Check(lseek(fd,0,SEEK_CUR)==123);
    Check(SystemNative_LSeek(fd,0,SEEK_SET)==0);
    Check(SystemNative_Read(fd,data,sizeof(data))==sizeof(data));
    Check(lseek(fd,0,SEEK_CUR)==16);
    memset(data,0x6a,sizeof(data));
    Check(SystemNative_Write(fd,data,sizeof(data))==sizeof(data));
    Check(lseek(fd,0,SEEK_CUR)==32);
    int duplicate=(int)SystemNative_Dup(fd);
    Check(duplicate>=0 && duplicate!=fd);
    if(duplicate>=0) {
        Check(SystemNative_LSeek(duplicate,23,SEEK_SET)==23);
        Check(lseek(fd,0,SEEK_CUR)==23);
        Check(SystemNative_PRead(duplicate,data,sizeof(data),0)==sizeof(data));
        Check(lseek(fd,0,SEEK_CUR)==23);
        Check(SystemNative_Close(duplicate)==0);
        Check(SystemNative_Read(fd,data,sizeof(data))==sizeof(data));
    }
    errno=0;Check(SystemNative_Dup(-1)==-1 && errno==EBADF);
    Check(SystemNative_Close(fd)==0);
    fd=open(name,O_RDONLY);
    Check(fd>=0);
    errno=0;Check(SystemNative_PWrite(fd,data,sizeof(data),4)==-1);
    Check(lseek(fd,0,SEEK_CUR)==0);
    Check(SystemNative_Close(fd)==0);
    Check(unlink(name)==0);
    fprintf(log,"%s checks=%u failures=%u; 4x1000 positional I/O cycles\n",failures?"FAIL":"PASS",checks,failures);
    fclose(log);
    return failures?1:0;
}
