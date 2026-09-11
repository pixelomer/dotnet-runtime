#pragma once
#include <stdbool.h>
#include <switch/arm/thread_context.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct LibnxThreadRegistration LibnxThreadRegistration;
// Register before managed execution; unregister on this same thread before
// its owning libnx/pthread object closes the kernel handle.
LibnxThreadRegistration* LibnxRegisterCurrentThread(void);
bool LibnxUnregisterThread(LibnxThreadRegistration* registration);
bool LibnxPauseRegisteredThread(LibnxThreadRegistration* registration, ThreadContext* context);
bool LibnxResumeRegisteredThread(LibnxThreadRegistration* registration);
void LibnxFlushProcessWriteBuffers(void);
// Runtime PAL handles wrap the registration above.
bool LibnxPausePalThread(void* handle, ThreadContext* context);
bool LibnxResumePalThread(void* handle);
#ifdef __cplusplus
}
#endif
