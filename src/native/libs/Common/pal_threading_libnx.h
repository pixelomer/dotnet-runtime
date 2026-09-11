#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Process-lifetime reaper; callbacks must return normally (not pthread_exit).
int LibnxCreateDetachedThread(size_t stackSize, void* (*entry)(void*), void* argument);
#ifdef __cplusplus
}
#endif
