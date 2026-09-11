#include "pal/mapnative.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/kernel/mutex.h>
#include <switch/kernel/virtmem.h>
#include <switch/runtime/env.h>
#include <switch/arm/cache.h>
#include <switch/result.h>
}
namespace {
constexpr size_t Page = 4096;
struct Backing { void* memory; size_t livePages; };
struct Entry { Backing* backing; size_t offset; int protection; bool reserved, writableAllowed; };
struct Region { Region* next; uintptr_t base; size_t pages; Entry* entries; VirtmemReservation* reservation; };
Mutex mutex;
Region* regions;
struct Lock { Lock() { mutexLock(&mutex); } ~Lock() { mutexUnlock(&mutex); } };
bool Round(size_t size, size_t& rounded) {
    if (!size || size > SIZE_MAX - (Page - 1)) return false;
    rounded = (size + Page - 1) & ~(Page - 1); return true;
}
bool Range(uintptr_t base, size_t size) { return !(base & (Page - 1)) && size <= UINTPTR_MAX - base; }
void* Fail(int error) { errno = error; return MapFailed; }
Region* Find(uintptr_t base, size_t size) {
    for (Region* r = regions; r; r = r->next)
        if (base >= r->base && base - r->base < r->pages * Page && size <= r->pages * Page - (base - r->base)) return r;
    return nullptr;
}
bool ToPermission(int protection, u32& permission) {
    switch (protection) {
        case MapNone: permission = Perm_None; return true;
        case MapRead: permission = Perm_R; return true;
        case MapWrite: case MapRead | MapWrite: permission = Perm_Rw; return true;
        case MapExecute: case MapRead | MapExecute: permission = Perm_Rx; return true;
        default: return false; // No RWX mapping.
    }
}
void Require(Result rc) { if (R_FAILED(rc)) abort(); }
void RemoveEmpty(Region* r) {
    for (size_t i = 0; i < r->pages; ++i) if (r->entries[i].reserved) return;
    Region** link = &regions; while (*link != r) link = &(*link)->next;
    *link = r->next;
    virtmemLock(); virtmemRemoveReservation(r->reservation); virtmemUnlock();
    free(r->entries); free(r);
}
Region* Reserve(size_t size) {
    Region* r = static_cast<Region*>(calloc(1, sizeof(Region)));
    if (!r) return nullptr;
    r->pages = size / Page;
    r->entries = static_cast<Entry*>(calloc(r->pages, sizeof(Entry)));
    if (!r->entries) { free(r); return nullptr; }
    virtmemLock();
    r->base = reinterpret_cast<uintptr_t>(virtmemFindCodeMemory(size, Page));
    if (r->base) r->reservation = virtmemAddReservation(reinterpret_cast<void*>(r->base), size);
    virtmemUnlock();
    if (!r->reservation) { free(r->entries); free(r); return nullptr; }
    for (size_t i = 0; i < r->pages; ++i) r->entries[i].reserved = true;
    r->next = regions; regions = r; return r;
}
void RemoveNew(Region* r) {
    for (size_t i = 0; i < r->pages; ++i) r->entries[i].reserved = false;
    RemoveEmpty(r);
}
}

void* NativeMap(void* address, size_t size, int protection, int flags, int fd, off_t offset) {
    size_t bytes; u32 permission;
    if (!Round(size, bytes) || offset < 0 || (offset & (Page - 1)) ||
        (flags & ~(MapPrivate | MapShared | MapAnonymous | MapFixed)) ||
        ((flags & (MapPrivate | MapShared)) != MapPrivate && (flags & (MapPrivate | MapShared)) != MapShared)) return Fail(EINVAL);
    if (!ToPermission(protection, permission)) return Fail(ENOTSUP);
    bool anonymous = (flags & MapAnonymous) != 0, fixed = (flags & MapFixed) != 0;
    if ((!anonymous && fd < 0) || (anonymous && offset != 0)) return Fail(EINVAL);
    // There is no Horizon file pager. Read-only file views are pinned snapshots;
    // writable shared-file mappings cannot be represented by buffered copies.
    if (!anonymous && (flags & MapShared) && (protection & MapWrite)) return Fail(ENOTSUP);
    Handle process = envGetOwnProcessHandle();
    if (process == INVALID_HANDLE || !envIsSyscallHinted(0x73) || !envIsSyscallHinted(0x77) || !envIsSyscallHinted(0x78)) return Fail(ENOTSUP);
    uintptr_t base = reinterpret_cast<uintptr_t>(address);
    if (fixed && (!base || !Range(base, bytes))) return Fail(EINVAL);
    Lock lock;
    Region* r = fixed ? Find(base, bytes) : Reserve(bytes);
    if (!r) return Fail(fixed ? EINVAL : ENOMEM);
    bool isNew = !fixed;
    if (isNew) base = r->base;
    size_t first = (base - r->base) / Page;
    for (size_t i = first; i < first + bytes / Page; ++i) {
        // PE placement replaces reserved holes. Replacing live mappings is a
        // separate operation; reject it without discarding the old contents.
        if (!r->entries[i].reserved || r->entries[i].backing) {
            if (isNew) RemoveNew(r);
            return Fail(EBUSY);
        }
    }
    if (anonymous && protection == MapNone) return reinterpret_cast<void*>(base);
    Backing* backing = static_cast<Backing*>(calloc(1, sizeof(Backing)));
    if (backing) backing->memory = aligned_alloc(Page, bytes);
    if (!backing || !backing->memory) {
        free(backing); if (isNew) RemoveNew(r); return Fail(ENOMEM);
    }
    memset(backing->memory, 0, bytes);
    int error = 0;
    if (!anonymous) {
        struct stat info;
        if (fstat(fd, &info) != 0) error = errno;
        else if (offset >= info.st_size || size > static_cast<uint64_t>(INT64_MAX - offset)) error = EINVAL;
        else {
            size_t available = static_cast<uint64_t>(info.st_size - offset) < bytes ? size_t(info.st_size - offset) : bytes;
            // A short final page is zero-filled. Whole pages past EOF would
            // require a SIGBUS-style pager fault and are rejected instead.
            if (available <= bytes - Page) error = EINVAL;
            for (size_t done = 0; !error && done < available;) {
                ssize_t n = pread(fd, static_cast<char*>(backing->memory) + done, available - done, offset + done);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) error = n < 0 ? errno : EIO;
                else done += n;
            }
        }
    }
    if (!error) {
        armDCacheFlush(backing->memory, bytes);
        Result rc = svcMapProcessCodeMemory(process, base, reinterpret_cast<uintptr_t>(backing->memory), bytes);
        if (R_FAILED(rc)) error = ENOMEM;
        else {
            rc = svcSetProcessMemoryPermission(process, base, bytes, permission);
            if (R_FAILED(rc)) {
                Require(svcUnmapProcessCodeMemory(process, base, reinterpret_cast<uintptr_t>(backing->memory), bytes));
                error = ENOTSUP;
            }
        }
    }
    if (error) {
        free(backing->memory); free(backing); if (isNew) RemoveNew(r); return Fail(error);
    }
    backing->livePages = bytes / Page;
    for (size_t i = 0; i < bytes / Page; ++i) r->entries[first + i] = {backing, i * Page, protection, true, anonymous || (flags & MapPrivate) != 0};
    if (protection & MapExecute) armICacheInvalidate(reinterpret_cast<void*>(base), bytes);
    return reinterpret_cast<void*>(base);
}
static int UnmapLocked(void* address, size_t size) {
    size_t bytes; uintptr_t base = reinterpret_cast<uintptr_t>(address);
    if (!Round(size, bytes) || !Range(base, bytes)) { errno = EINVAL; return -1; }
    Region* r = Find(base, bytes);
    if (!r) { errno = EINVAL; return -1; }
    size_t first = (base - r->base) / Page;
    // An image's independent sections can be retired in any order. Keep the
    // original backing allocation until every mapped page referring to it is gone.
    for (size_t i = first; i < first + bytes / Page;) {
        Entry& entry = r->entries[i];
        if (!entry.backing) { entry.reserved = false; ++i; continue; }
        Backing* backing = entry.backing; size_t count = 1;
        while (i + count < first + bytes / Page && r->entries[i + count].backing == backing &&
               r->entries[i + count].offset == entry.offset + count * Page) ++count;
        Result rc = svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), r->base + i * Page,
            reinterpret_cast<uintptr_t>(backing->memory) + entry.offset, count * Page);
        if (R_FAILED(rc)) { errno = EIO; return -1; }
        backing->livePages -= count;
        for (size_t n = 0; n < count; ++n) r->entries[i + n] = {};
        if (!backing->livePages) { free(backing->memory); free(backing); }
        i += count;
    }
    RemoveEmpty(r); return 0;
}
int NativeUnmap(void* address, size_t size) {
    Lock lock; return UnmapLocked(address, size);
}
int NativeReleaseImageReservation(const void* address) {
    Lock lock;
    Region* r = Find(reinterpret_cast<uintptr_t>(address), 1);
    return r ? UnmapLocked(reinterpret_cast<void*>(r->base), r->pages * Page) : 0;
}
int NativeProtect(void* address, size_t size, int protection) {
    size_t bytes; uintptr_t base = reinterpret_cast<uintptr_t>(address); u32 permission;
    if (!Round(size, bytes) || !Range(base, bytes) || !ToPermission(protection, permission)) { errno = EINVAL; return -1; }
    Lock lock;
    Region* r = Find(base, bytes);
    if (!r) { errno = EINVAL; return -1; }
    size_t first = (base - r->base) / Page;
    for (size_t i = first; i < first + bytes / Page; ++i)
        if (!r->entries[i].backing) { errno = EINVAL; return -1; }
        else if ((protection & MapWrite) && !r->entries[i].writableAllowed) { errno = EACCES; return -1; }
    Result rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(), base, bytes, permission);
    if (R_FAILED(rc)) { errno = ENOTSUP; return -1; }
    for (size_t i = first; i < first + bytes / Page; ++i) r->entries[i].protection = protection;
    if (protection & MapExecute) { armDCacheFlush(address, bytes); armICacheInvalidate(address, bytes); }
    return 0;
}
int NativeDiscard(void*, size_t) {
    // POSIX_MADV_DONTNEED is advisory and must not discard private modifications.
    // With pinned buffered backing there is no file pager to reclaim clean pages.
    return 0;
}
