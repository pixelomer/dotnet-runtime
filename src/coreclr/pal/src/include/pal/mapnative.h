#pragma once
#include <stddef.h>
#include <sys/types.h>
#if defined(TARGET_LIBNX)
enum { MapNone = 0, MapRead = 1, MapWrite = 2, MapExecute = 4 };
enum { MapPrivate = 1, MapShared = 2, MapAnonymous = 4, MapFixed = 8 };
#define MapFailed ((void*)-1)
void* NativeMap(void* address, size_t size, int protection, int flags, int fd, off_t offset);
int NativeUnmap(void* address, size_t size);
int NativeProtect(void* address, size_t size, int protection);
int NativeDiscard(void* address, size_t size);
// Temporary private-image writer aliases; primary protection remains unchanged.
void* NativeAcquireWritableView(void* address, size_t size);
void NativeReleaseWritableView(void* address);
// Release any unrecorded image reservation (failed load/alignment padding).
int NativeReleaseImageReservation(const void* address);
#else
#include <sys/mman.h>
enum { MapNone = PROT_NONE, MapRead = PROT_READ, MapWrite = PROT_WRITE, MapExecute = PROT_EXEC };
enum { MapPrivate = MAP_PRIVATE, MapShared = MAP_SHARED, MapAnonymous =
#ifdef MAP_ANON
    MAP_ANON,
#else
    MAP_ANONYMOUS,
#endif
    MapFixed = MAP_FIXED };
#define MapFailed MAP_FAILED
inline void* NativeMap(void* address, size_t size, int protection, int flags, int fd, off_t offset) {
    return mmap(address, size, protection, flags, fd, offset);
}
inline int NativeUnmap(void* address, size_t size) { return munmap(address, size); }
inline int NativeProtect(void* address, size_t size, int protection) { return mprotect(address, size, protection); }
inline int NativeDiscard(void* address, size_t size) {
#ifndef TARGET_ANDROID
    return posix_madvise(address, size, POSIX_MADV_DONTNEED);
#else
    return 0;
#endif
}
#endif
