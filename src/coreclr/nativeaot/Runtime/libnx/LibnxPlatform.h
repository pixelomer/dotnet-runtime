#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Narrow C boundary avoids the libnx Thread / runtime Thread name collision.
uint32_t LibnxGetCurrentThreadHandle(void);
bool LibnxGetCurrentStackBounds(void** low, void** high);
void* LibnxGetModuleBase(void* address);
bool LibnxSetDataPermission(void* address, size_t size, int permission);
void LibnxFlushInstructionCache(void* address, size_t size);
#ifdef __cplusplus
}
#endif
