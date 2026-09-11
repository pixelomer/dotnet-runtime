#pragma once
#include <libs/Common/libnx_threads.h>
#ifdef __cplusplus
extern "C" {
#endif
bool LibnxPausePalThread(void* handle, ThreadContext* context);
bool LibnxResumePalThread(void* handle);
#ifdef __cplusplus
}
#endif
