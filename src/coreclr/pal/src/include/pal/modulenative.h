#pragma once
#include <stddef.h>
#if defined(TARGET_LIBNX)
struct NativeModuleInfo { const char* name; void* base; };
void* NativeOpenModule(const char* name);
int NativeCloseModule(void* handle);
void* NativeModuleSymbol(void* handle, const char* name);
bool NativeModuleFromAddress(const void* address, NativeModuleInfo* info);
const char* NativeModuleError();
struct NativeModuleRange { void* start; size_t size; };
bool NativeModuleRanges(void* base, NativeModuleRange (&ranges)[3]);
#else
#include <dlfcn.h>
struct NativeModuleInfo { const char* name; void* base; };
inline void* NativeOpenModule(const char* name) { return dlopen(name, RTLD_LAZY); }
inline int NativeCloseModule(void* handle) { return dlclose(handle); }
inline void* NativeModuleSymbol(void* handle, const char* name) { return dlsym(handle, name); }
inline bool NativeModuleFromAddress(const void* address, NativeModuleInfo* info) {
    Dl_info native;
    if (!dladdr(address, &native)) return false;
    info->name = native.dli_fname;
    info->base = native.dli_fbase;
    return true;
}
inline const char* NativeModuleError() { return dlerror(); }
#endif
