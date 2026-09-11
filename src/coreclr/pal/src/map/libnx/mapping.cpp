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
struct Entry { Backing* backing; size_t offset; int protection; bool reserved, writableAllowed, dataState, executableAllowed; size_t writers; };
struct Region { Region* next; uintptr_t base; size_t pages; Entry* entries; VirtmemReservation* reservation; };
struct ViewSegment { ViewSegment* next; size_t offset, bytes; };
struct View { View* next; ViewSegment* segments; uintptr_t primary, writable; size_t bytes; void* result; VirtmemReservation* reservation; };
View* views;
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
    for (size_t i = 0; i < bytes / Page; ++i) r->entries[first + i] = {backing, i * Page, protection, true, anonymous || (flags & MapPrivate) != 0, permission == Perm_Rw, (protection & MapExecute) != 0, 0};
    if (protection & MapExecute) armICacheInvalidate(reinterpret_cast<void*>(base), bytes);
    return reinterpret_cast<void*>(base);
}
static int UnmapLocked(void* address, size_t size) {
    size_t bytes; uintptr_t base = reinterpret_cast<uintptr_t>(address);
    if (!Round(size, bytes) || !Range(base, bytes)) { errno = EINVAL; return -1; }
    Region* r = Find(base, bytes);
    if (!r) { errno = EINVAL; return -1; }
    size_t first = (base - r->base) / Page;
    for (size_t i = first; i < first + bytes / Page; ++i)
        if (r->entries[i].writers) { errno = EBUSY; return -1; }
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
    // Distinguish foreign memory from a rejected operation on our own mapping.
    if (!r) {
        for (Region* other = regions; other; other = other->next)
            if (base < other->base + other->pages * Page && other->base < base + bytes) {
                errno = EINVAL; return -1;
            }
        errno = ENOENT; return -1;
    }
    size_t first = (base - r->base) / Page, end = first + bytes / Page;
    for (size_t i = first; i < end; ++i) {
        const Entry& e = r->entries[i];
        if (!e.backing) { errno = EINVAL; return -1; }
        if ((protection & MapWrite) && !e.writableAllowed) { errno = EACCES; return -1; }
        // Executability is a mapping-time capability. AliasCode -> AliasCodeData
        // is irreversible, so executable views use writers even while temporarily
        // R/None. Non-executable views can change data permissions but never gain X.
        // This also preserves API capabilities after a rolled-back protection call.
        if (((protection & MapExecute) && !e.executableAllowed) ||
            ((protection & MapWrite) && e.executableAllowed)) { errno = ENOTSUP; return -1; }
    }
    size_t i = first;
    for (; i < end; ++i) {
        Entry& e = r->entries[i];
        void* page = reinterpret_cast<void*>(r->base + i * Page);
        Result rc = e.dataState ? svcSetMemoryPermission(page, Page, permission) :
            svcSetProcessMemoryPermission(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(page), Page, permission);
        if (R_FAILED(rc)) break;
        if (permission == Perm_Rw) e.dataState = true;
    }
    if (i != end) {
        // Restore permissions changed before the failure. Data-state pages can
        // restore R/None/RW, and prevalidation excluded an irreversible RX->RW.
        while (i > first) {
            Entry& e = r->entries[--i]; u32 old; ToPermission(e.protection, old);
            void* page = reinterpret_cast<void*>(r->base + i * Page);
            Require(e.dataState ? svcSetMemoryPermission(page, Page, old) :
                svcSetProcessMemoryPermission(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(page), Page, old));
        }
        errno = ENOTSUP; return -1;
    }
    for (size_t n = first; n < end; ++n) r->entries[n].protection = protection;
    return 0;
}
void* NativeAcquireWritableView(void* address, size_t size) {
    uintptr_t value = reinterpret_cast<uintptr_t>(address), base = value & ~(Page - 1);
    size_t bytes;
    if (size == 0 || size > UINTPTR_MAX - value || !Round(size + (value - base), bytes)) { errno = EINVAL; return nullptr; }
    if (!envIsSyscallHinted(0x74) || !envIsSyscallHinted(0x75)) { errno = ENOTSUP; return nullptr; }
    Lock lock;
    Region* r = Find(base, bytes);
    if (!r) { errno = EINVAL; return nullptr; }
    size_t first = (base - r->base) / Page;
    for (size_t i = first; i < first + bytes / Page; ++i) {
        const Entry& e = r->entries[i];
        if (!e.backing || !e.writableAllowed || e.writers == SIZE_MAX) { errno = EACCES; return nullptr; }
    }
    View* v = static_cast<View*>(calloc(1, sizeof(View)));
    if (!v) { errno = ENOMEM; return nullptr; }
    v->primary = base; v->bytes = bytes;
    virtmemLock();
    v->writable = reinterpret_cast<uintptr_t>(virtmemFindCodeMemory(bytes, Page));
    if (v->writable) v->reservation = virtmemAddReservation(reinterpret_cast<void*>(v->writable), bytes);
    virtmemUnlock();
    if (!v->reservation) { free(v); errno = ENOMEM; return nullptr; }
    // Map each source backing/protection run separately. Horizon requires a
    // homogeneous source memory state; adjacent PE sections need not share it.
    size_t mapped = 0;
    while (mapped < bytes) {
        size_t index = first + mapped / Page, count = 1;
        const Entry& e = r->entries[index];
        while (mapped + count * Page < bytes) {
            const Entry& next = r->entries[index + count];
            if (next.backing != e.backing || next.offset != e.offset + count * Page ||
                next.protection != e.protection || next.dataState != e.dataState) break;
            ++count;
        }
        ViewSegment* segment = static_cast<ViewSegment*>(calloc(1, sizeof(ViewSegment)));
        if (!segment) break;
        segment->offset = mapped; segment->bytes = count * Page;
        Result rc = svcMapProcessMemory(reinterpret_cast<void*>(v->writable + mapped), envGetOwnProcessHandle(), base + mapped, segment->bytes);
        if (R_FAILED(rc)) { free(segment); break; }
        segment->next = v->segments; v->segments = segment;
        mapped += segment->bytes;
    }
    if (mapped != bytes) {
        while (v->segments) {
            ViewSegment* segment = v->segments; v->segments = segment->next;
            Require(svcUnmapProcessMemory(reinterpret_cast<void*>(v->writable + segment->offset),
                envGetOwnProcessHandle(), base + segment->offset, segment->bytes));
            free(segment);
        }
        virtmemLock(); virtmemRemoveReservation(v->reservation); virtmemUnlock();
        free(v); errno = ENOMEM; return nullptr;
    }
    for (size_t i = first; i < first + bytes / Page; ++i) ++r->entries[i].writers;
    v->result = reinterpret_cast<void*>(v->writable + (value - base));
    v->next = views; views = v;
    return v->result;
}
void NativeReleaseWritableView(void* address) {
    if (!address) return;
    Lock lock;
    View** link = &views;
    while (*link && (*link)->result != address) link = &(*link)->next;
    if (!*link) abort();
    View* v = *link;
    // Publish through the writable alias before retiring it. The executable
    // virtual address never changed and remains usable by other threads.
    armDCacheFlush(reinterpret_cast<void*>(v->writable), v->bytes);
    armICacheInvalidate(reinterpret_cast<void*>(v->primary), v->bytes);
    while (v->segments) {
        ViewSegment* segment = v->segments; v->segments = segment->next;
        Require(svcUnmapProcessMemory(reinterpret_cast<void*>(v->writable + segment->offset),
            envGetOwnProcessHandle(), v->primary + segment->offset, segment->bytes));
        free(segment);
    }
    Region* r = Find(v->primary, v->bytes);
    if (!r) abort();
    size_t first = (v->primary - r->base) / Page;
    for (size_t i = first; i < first + v->bytes / Page; ++i) {
        if (!r->entries[i].writers) abort();
        --r->entries[i].writers;
    }
    virtmemLock(); virtmemRemoveReservation(v->reservation); virtmemUnlock();
    *link = v->next; free(v);
}
int NativeDiscard(void*, size_t) {
    // POSIX_MADV_DONTNEED is advisory and must not discard private modifications.
    // With pinned buffered backing there is no file pager to reclaim clean pages.
    return 0;
}
