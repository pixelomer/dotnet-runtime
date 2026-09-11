#pragma once
#include <stddef.h>
#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif
// Process-lifetime reaper; normal return and pthread_exit both run the completion
// TLS destructor. Resources are released only after kernel thread termination.
int LibnxCreateDetachedThreadWithAttributes(pthread_t* nativeThread, const pthread_attr_t* attrs,
    void* (*entry)(void*), void* argument);
int LibnxCreateDetachedThread(size_t stackSize, void* (*entry)(void*), void* argument);
#ifdef __cplusplus
}
#endif
