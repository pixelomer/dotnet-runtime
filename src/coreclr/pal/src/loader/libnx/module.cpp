#include "pal/modulenative.h"
#include <minipal/getexepath.h>
#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include <switch/types.h>
#include <switch/nro.h>
// Section-bound symbols use PC-relative references; __start__ is an absolute
// linker symbol and an ordinary GOT reference can remain zero after relocation.
extern char __code_start[] __attribute__((visibility("hidden")));
extern char __end__[] __attribute__((visibility("hidden")));
extern Elf64_Dyn _DYNAMIC[] __attribute__((visibility("hidden")));
}
namespace {
struct ResidentModule {
    const char* path;
    NativeModuleRange ranges[3];
    uintptr_t base;
    size_t size;
    const Elf64_Sym* symbols;
    size_t symbolCount;
    const char* strings;
    size_t stringSize;
    const uint32_t* buckets;
    const uint32_t* chains;
    uint32_t bucketCount;
    bool valid;
};
ResidentModule resident{};
pthread_once_t initialized = PTHREAD_ONCE_INIT;
__thread const char* lastError;
void Error(int code, const char* text) { errno = code; lastError = text; }
bool Contains(uintptr_t address, size_t size) {
    return address >= resident.base && address - resident.base <= resident.size &&
        size <= resident.size - (address - resident.base);
}
void Initialize() {
    resident.base = reinterpret_cast<uintptr_t>(__code_start);
    // These are the resident homebrew image's own headers, supplied by elf2nro.
    // No proprietary module metadata or host ELF program-header fiction.
    const auto* header = reinterpret_cast<const NroHeader*>(__code_start + sizeof(NroStart));
    if (header->magic != NROHEADER_MAGIC || header->size < sizeof(NroStart) + sizeof(NroHeader)) return;
    resident.size = static_cast<size_t>(header->size) + header->bss_size;
    uintptr_t previous = 0;
    for (unsigned i = 0; i < 3; ++i) {
        auto segment = header->segments[i];
        if (segment.file_off < previous || segment.file_off > header->size ||
            segment.size > header->size - segment.file_off) return;
        resident.ranges[i] = {__code_start + segment.file_off, segment.size};
        previous = static_cast<size_t>(segment.file_off) + segment.size;
    }
    if (header->segments[0].file_off != 0 || previous != header->size ||
        reinterpret_cast<uintptr_t>(__end__) > resident.base + resident.size) return;
    resident.ranges[2].size += header->bss_size;
    // libnx initializes these argv globals before calling the application.
    // Copy the actual executable path once; never fabricate a coreclr.so path.
    resident.path = minipal_getexepath();
    resident.valid = true;
    uintptr_t symbols = 0, strings = 0, hash = 0;
    size_t entrySize = 0;
    bool terminated = false;
    for (auto entry = _DYNAMIC; Contains(reinterpret_cast<uintptr_t>(entry), sizeof(*entry)); ++entry) {
        if (entry->d_tag == DT_NULL) { terminated = true; break; }
        switch (entry->d_tag) {
            case DT_SYMTAB: symbols = entry->d_un.d_ptr; break;
            case DT_STRTAB: strings = entry->d_un.d_ptr; break;
            case DT_STRSZ: resident.stringSize = entry->d_un.d_val; break;
            case DT_SYMENT: entrySize = entry->d_un.d_val; break;
            case DT_HASH: hash = entry->d_un.d_ptr; break;
        }
    }
    // devkitPro's linker emits the standard System V hash table. GNU-hash-only
    // images need a separate decoder; don't infer symbol counts from padding.
    if (!terminated || entrySize != sizeof(Elf64_Sym) || !hash || !symbols || !strings ||
        hash > resident.size || symbols > resident.size || strings > resident.size ||
        !Contains(resident.base + hash, 2 * sizeof(uint32_t))) return;
    auto table = reinterpret_cast<const uint32_t*>(resident.base + hash);
    size_t count = table[1], buckets = table[0];
    if (!buckets || !count || !Contains(resident.base + hash, (2 + buckets + count) * sizeof(uint32_t)) ||
        !Contains(resident.base + symbols, count * sizeof(Elf64_Sym)) ||
        !Contains(resident.base + strings, resident.stringSize)) return;
    resident.symbols = reinterpret_cast<const Elf64_Sym*>(resident.base + symbols);
    resident.symbolCount = count;
    resident.strings = reinterpret_cast<const char*>(resident.base + strings);
    resident.buckets = table + 2;
    resident.chains = table + 2 + buckets;
    resident.bucketCount = buckets;
}
bool Ready() {
    pthread_once(&initialized, Initialize);
    if (resident.valid) return true;
    Error(ENOEXEC, "Resident image is not a supported libnx NRO");
    return false;
}
uint32_t SymbolHash(const char* name) {
    uint32_t value = 0;
    for (auto p = reinterpret_cast<const unsigned char*>(name); *p; ++p) {
        value = (value << 4) + *p;
        uint32_t high = value & 0xf0000000;
        if (high) value ^= high >> 24;
        value &= ~high;
    }
    return value;
}
}
void* NativeOpenModule(const char* name) {
    if (!Ready()) return nullptr;
    if (!name) return &resident;
    if (resident.path) {
        if (strcmp(name, resident.path) == 0) return &resident;
        char resolved[PATH_MAX];
        if (realpath(name, resolved) && strcmp(resolved, resident.path) == 0) return &resident;
    }
    Error(ENOTSUP, "Loading external native modules on Horizon is not implemented");
    return nullptr;
}
int NativeCloseModule(void* handle) {
    if (handle != &resident) { Error(EINVAL, "Invalid native module handle"); return -1; }
    // Resident executable lifetime is owned by the homebrew loader. Releasing
    // a lookup reference does not unload it, just as for dlopen(NULL) on Unix.
    return 0;
}
void* NativeModuleSymbol(void* handle, const char* name) {
    if (handle != &resident || !name || !*name) { Error(EINVAL, "Invalid module or symbol name"); return nullptr; }
    if (!Ready()) return nullptr;
    if (!resident.symbols) { Error(ENOTSUP, "Resident NRO has no supported dynamic symbol table"); return nullptr; }
    uint32_t index = resident.buckets[SymbolHash(name) % resident.bucketCount];
    for (size_t visited = 0; index != STN_UNDEF && visited < resident.symbolCount; ++visited) {
        if (index >= resident.symbolCount) break;
        const auto& symbol = resident.symbols[index];
        unsigned binding = ELF64_ST_BIND(symbol.st_info), type = ELF64_ST_TYPE(symbol.st_info);
        unsigned visibility = ELF64_ST_VISIBILITY(symbol.st_other);
        if ((binding == STB_GLOBAL || binding == STB_WEAK) &&
            (visibility == STV_DEFAULT || visibility == STV_PROTECTED) &&
            (type == STT_FUNC || type == STT_OBJECT || type == STT_NOTYPE) &&
            symbol.st_shndx != SHN_UNDEF && symbol.st_shndx < SHN_LORESERVE &&
            symbol.st_name < resident.stringSize &&
            memchr(resident.strings + symbol.st_name, 0, resident.stringSize - symbol.st_name) &&
            strcmp(resident.strings + symbol.st_name, name) == 0 &&
            symbol.st_value < resident.size && Contains(resident.base + symbol.st_value, symbol.st_size))
            return reinterpret_cast<void*>(resident.base + symbol.st_value);
        index = resident.chains[index];
    }
    Error(ENOENT, "Symbol is not exported by the resident native module");
    return nullptr;
}
bool NativeModuleFromAddress(const void* address, NativeModuleInfo* info) {
    if (!info || !Ready() || !Contains(reinterpret_cast<uintptr_t>(address), 1)) {
        Error(ENOENT, "Address does not belong to the resident native module"); return false;
    }
    if (!resident.path) { Error(ENOENT, "Homebrew loader did not supply an executable path"); return false; }
    info->base = __code_start;
    info->name = resident.path;
    return true;
}
bool NativeModuleRanges(void* base, NativeModuleRange (&ranges)[3]) {
    if (!Ready() || base != __code_start) { Error(EINVAL, "Unknown native module base"); return false; }
    for (unsigned i = 0; i < 3; ++i) ranges[i] = resident.ranges[i];
    return true;
}
const char* NativeModuleError() { const char* value = lastError; lastError = nullptr; return value; }
