#pragma once
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Returns true after publishing an owned committed RX range using its matching
// RW alias when mapped. False means this is not a valid owned RX range.
bool LibnxFlushCodeMemory(const void* address, size_t size);
#ifdef __cplusplus
}
#endif
