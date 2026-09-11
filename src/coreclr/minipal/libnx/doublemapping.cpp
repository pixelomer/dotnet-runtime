#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "minipal.h"
#include "doublemapping.h"
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
constexpr size_t Capacity = size_t(512) << 20;
struct Chunk {
    Chunk* next;
    uintptr_t primary;
    size_t size, references;
    void* backing;
    Handle handle;
    bool executable, primaryMapped;
};
struct View { View* next; uintptr_t address, primary; size_t size; };
struct Region {
    Region* next;
    uintptr_t base;
    size_t size, offset;
    Chunk* chunks;
    View* views;
};
struct Mapper {
    Mapper* next;
    uintptr_t primary, writable;
    VirtmemReservation *primaryReservation, *writableReservation;
    Region* regions;
    bool closing;
};
Mutex mutex;
Mapper* mappers;
struct Lock { Lock() { mutexLock(&mutex); } ~Lock() { mutexUnlock(&mutex); } };
bool Aligned(size_t n) { return (n & (Page - 1)) == 0; }
bool Range(uintptr_t start, size_t size) { return size && Aligned(start) && Aligned(size) && size <= UINTPTR_MAX - start; }
bool Overlaps(uintptr_t a, size_t as, uintptr_t b, size_t bs) { return a < b + bs && b < a + as; }
Mapper* FindMapper(void* handle) {
    for (Mapper* m = mappers; m; m = m->next) if (m == handle) return m;
    return nullptr;
}
Region* FindRegion(uintptr_t address, size_t size, Mapper** mapper = nullptr) {
    if (!Range(address, size)) return nullptr;
    for (Mapper* m = mappers; m; m = m->next)
        for (Region* r = m->regions; r; r = r->next)
            if (address >= r->base && address - r->base < r->size && size <= r->size - (address - r->base)) {
                if (mapper) *mapper = m;
                return r;
            }
    return nullptr;
}
Chunk* FindChunk(Region* r, uintptr_t address) {
    for (Chunk* c = r->chunks; c; c = c->next)
        if (address >= c->primary && address - c->primary < c->size) return c;
    return nullptr;
}
void Require(Result rc) {
    // Failed cleanup must never free a heap source which is still kernel-locked
    // or mapped. Terminate rather than publish corrupted allocator ownership.
    if (R_FAILED(rc)) abort();
}
void DestroyChunk(Chunk* c) {
    if (c->references) abort();
    if (c->primaryMapped)
        Require(svcUnmapProcessCodeMemory(c->handle, c->primary, reinterpret_cast<uintptr_t>(c->backing), c->size));
    // The process handle is borrowed from libnx's environment, not ours to close.
    free(c->backing); free(c);
}
Chunk* CreateChunk(Mapper* m, uintptr_t primary, size_t size, bool executable) {
    Chunk* c = static_cast<Chunk*>(calloc(1, sizeof(Chunk)));
    if (!c) return nullptr;
    c->primary = primary;
    c->size = size; c->executable = executable; c->handle = INVALID_HANDLE;
    c->backing = aligned_alloc(Page, size);
    if (!c->backing) { free(c); return nullptr; }
    c->handle = envGetOwnProcessHandle();
    memset(c->backing, 0, size);
    armDCacheFlush(c->backing, size);
    Result rc = svcMapProcessCodeMemory(c->handle, primary, reinterpret_cast<uintptr_t>(c->backing), size);
    if (R_FAILED(rc)) { free(c->backing); free(c); return nullptr; }
    c->primaryMapped = true;
    rc = svcSetProcessMemoryPermission(c->handle, primary, size, executable ? Perm_Rx : Perm_Rw);
    if (R_FAILED(rc)) { DestroyChunk(c); return nullptr; }
    if (executable) armICacheInvalidate(reinterpret_cast<void*>(primary), size);
    return c;
}
void MaybeDestroyMapper(Mapper* m) {
    if (!m->closing || m->regions) return;
    Mapper** link = &mappers;
    while (*link != m) link = &(*link)->next;
    *link = m->next;
    virtmemLock();
    virtmemRemoveReservation(m->primaryReservation);
    virtmemRemoveReservation(m->writableReservation);
    virtmemUnlock();
    free(m);
}
}

bool VMToOSInterface::CreateDoubleMemoryMapper(void** handle, size_t* maximum) {
    if (!handle || !maximum || envGetOwnProcessHandle() == INVALID_HANDLE ||
        !envIsSyscallHinted(0x73) || !envIsSyscallHinted(0x74) || !envIsSyscallHinted(0x75) ||
        !envIsSyscallHinted(0x77) || !envIsSyscallHinted(0x78)) return false;
    Mapper* m = static_cast<Mapper*>(calloc(1, sizeof(Mapper)));
    if (!m) return false;
    // Virtual arenas only: physical backing is allocated on commitment. libnx
    // reservations prevent its other users from taking either arena's holes.
    virtmemLock();
    m->primary = reinterpret_cast<uintptr_t>(virtmemFindCodeMemory(Capacity, Page));
    if (m->primary) m->primaryReservation = virtmemAddReservation(reinterpret_cast<void*>(m->primary), Capacity);
    if (m->primaryReservation) {
        m->writable = reinterpret_cast<uintptr_t>(virtmemFindCodeMemory(Capacity, Page));
        if (m->writable) m->writableReservation = virtmemAddReservation(reinterpret_cast<void*>(m->writable), Capacity);
    }
    if (!m->writableReservation && m->primaryReservation) virtmemRemoveReservation(m->primaryReservation);
    virtmemUnlock();
    if (!m->writableReservation) { free(m); return false; }
    Lock lock;
    m->next = mappers; mappers = m;
    *handle = m; *maximum = Capacity;
    return true;
}
void VMToOSInterface::DestroyDoubleMemoryMapper(void* handle) {
    Lock lock;
    Mapper* m = FindMapper(handle);
    if (!m) return;
    // Like closing a Unix backing fd, existing allocations can outlive the
    // mapper's public lifetime. Retire arenas only after their final release.
    m->closing = true;
    MaybeDestroyMapper(m);
}
void* VMToOSInterface::ReserveDoubleMappedMemory(void* handle, size_t offset, size_t size, const void* lower, const void* upper) {
    if (!Range(offset, size) || offset > Capacity || size > Capacity - offset) return nullptr;
    Lock lock;
    Mapper* m = FindMapper(handle);
    if (!m || m->closing) return nullptr;
    for (Region* r = m->regions; r; r = r->next)
        if (Overlaps(offset, size, r->offset, r->size)) return nullptr;
    uintptr_t start = reinterpret_cast<uintptr_t>(lower), end = reinterpret_cast<uintptr_t>(upper);
    bool fixed = start && start == end;
    if (fixed) {
        if (!Range(start, size) || start < m->primary || start - m->primary > Capacity - size) return nullptr;
    } else {
        if (!start || start < m->primary) start = m->primary;
        if (!end || end > m->primary + Capacity) end = m->primary + Capacity;
        if (start > UINTPTR_MAX - (Page - 1)) return nullptr;
        start = (start + Page - 1) & ~(uintptr_t)(Page - 1);
        if (start >= end || size > end - start) return nullptr;
    }
    // Sorted primary allocations allow a deterministic first-fit scan within
    // mandatory bounds. Requests outside the owned arena fail, never escape it.
    for (Region* r = m->regions; r; r = r->next) {
        if (!Overlaps(start, size, r->base, r->size)) continue;
        if (fixed) return nullptr;
        start = r->base + r->size;
        if (start >= end || size > end - start) return nullptr;
    }
    Region* r = static_cast<Region*>(calloc(1, sizeof(Region)));
    if (!r) return nullptr;
    r->base = start; r->size = size; r->offset = offset;
    Region** link = &m->regions;
    while (*link && (*link)->base < start) link = &(*link)->next;
    r->next = *link; *link = r;
    return reinterpret_cast<void*>(start);
}
void* VMToOSInterface::CommitDoubleMappedMemory(void* address, size_t size, bool executable) {
    Lock lock;
    uintptr_t start = reinterpret_cast<uintptr_t>(address);
    Mapper* m = nullptr; Region* r = FindRegion(start, size, &m);
    if (!r) return nullptr;
    for (Chunk* c = r->chunks; c; c = c->next)
        if (Overlaps(start, size, c->primary, c->size) && c->executable != executable) return nullptr;
    Chunk* pending = nullptr;
    uintptr_t end = start + size;
    for (uintptr_t current = start; current < end;) {
        Chunk* c = FindChunk(r, current);
        if (c) { current = c->primary + c->size; continue; }
        uintptr_t next = end;
        for (Chunk* existing = r->chunks; existing; existing = existing->next)
            if (existing->primary > current && existing->primary < next) next = existing->primary;
        c = CreateChunk(m, current, next - current, executable);
        if (!c) {
            while (pending) { Chunk* old = pending; pending = pending->next; DestroyChunk(old); }
            return nullptr;
        }
        c->next = pending; pending = c; current = next;
    }
    while (pending) { Chunk* c = pending; pending = pending->next; c->next = r->chunks; r->chunks = c; }
    return address;
}
void* VMToOSInterface::GetRWMapping(void* handle, void* address, size_t offset, size_t size) {
    Lock lock;
    uintptr_t start = reinterpret_cast<uintptr_t>(address);
    Mapper* m = nullptr; Region* r = FindRegion(start, size, &m);
    if (!r || m != handle || offset != r->offset + (start - r->base)) return nullptr;
    // The API asks for committed RX memory. A view spanning holes or RW data
    // is invalid; ownership is checked before asking Horizon to map it.
    for (uintptr_t current = start; current < start + size;) {
        Chunk* c = FindChunk(r, current);
        if (!c || !c->executable || c->references == SIZE_MAX) return nullptr;
        current = c->primary + c->size;
    }
    View* view = static_cast<View*>(calloc(1, sizeof(View)));
    if (!view) return nullptr;
    // CoreCLR identifies writer blocks by address containment. Each request
    // must therefore own a disjoint virtual view, including requests for the
    // same or overlapping RX bytes (as with separate Unix mmap calls).
    uintptr_t writable = m->writable;
    bool moved;
    do {
        moved = false;
        for (Region* existing = m->regions; existing; existing = existing->next)
            for (View* live = existing->views; live; live = live->next)
                if (Overlaps(writable, size, live->address, live->size)) {
                    writable = live->address + live->size;
                    moved = true;
                }
        if (writable - m->writable > Capacity || size > Capacity - (writable - m->writable)) {
            free(view); return nullptr;
        }
    } while (moved);
    uintptr_t current = start;
    for (; current < start + size;) {
        Chunk* c = FindChunk(r, current);
        size_t bytes = c->primary + c->size - current;
        if (bytes > start + size - current) bytes = start + size - current;
        Result rc = svcMapProcessMemory(reinterpret_cast<void*>(writable + current - start), c->handle, current, bytes);
        if (R_FAILED(rc)) {
            for (uintptr_t undo = start; undo < current;) {
                Chunk* prior = FindChunk(r, undo);
                size_t priorBytes = prior->primary + prior->size - undo;
                if (priorBytes > current - undo) priorBytes = current - undo;
                Require(svcUnmapProcessMemory(reinterpret_cast<void*>(writable + undo - start), prior->handle, undo, priorBytes));
                undo += priorBytes;
            }
            free(view); return nullptr;
        }
        current += bytes;
    }
    for (Chunk* c = r->chunks; c; c = c->next)
        if (Overlaps(start, size, c->primary, c->size)) ++c->references;
    view->address = writable; view->primary = start; view->size = size;
    view->next = r->views; r->views = view;
    return reinterpret_cast<void*>(view->address);
}
bool VMToOSInterface::ReleaseRWMapping(void* address, size_t size) {
    Lock lock;
    uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (!Range(start, size)) return false;
    for (Mapper* m = mappers; m; m = m->next) {
        for (Region* r = m->regions; r; r = r->next) {
            View** link = &r->views;
            while (*link && ((*link)->address != start || (*link)->size != size)) link = &(*link)->next;
            if (!*link) continue;
            View* view = *link;
            // Publish only this alias, then retire precisely the segments that
            // were mapped for it. Other views remain independently writable.
            armDCacheFlush(reinterpret_cast<void*>(start), size);
            armICacheInvalidate(reinterpret_cast<void*>(view->primary), size);
            for (uintptr_t current = view->primary; current < view->primary + size;) {
                Chunk* c = FindChunk(r, current);
                size_t bytes = c->primary + c->size - current;
                if (bytes > view->primary + size - current) bytes = view->primary + size - current;
                Require(svcUnmapProcessMemory(reinterpret_cast<void*>(start + current - view->primary), c->handle, current, bytes));
                --c->references;
                current += bytes;
            }
            *link = view->next; free(view);
            return true;
        }
    }
    return false;
}
bool VMToOSInterface::ReleaseDoubleMappedMemory(void* handle, void* address, size_t offset, size_t size) {
    Lock lock;
    Mapper* m = FindMapper(handle);
    if (!m) return false;
    Region** link = &m->regions;
    while (*link && (*link)->base != reinterpret_cast<uintptr_t>(address)) link = &(*link)->next;
    Region* r = *link;
    if (!r || r->size != size || r->offset != offset || r->views) return false;
    while (r->chunks) { Chunk* c = r->chunks; r->chunks = c->next; DestroyChunk(c); }
    *link = r->next; free(r);
    MaybeDestroyMapper(m);
    return true;
}

extern "C" bool LibnxIsOwnedCodeMemory(const void* address, size_t size) {
    uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (!size || size > UINTPTR_MAX - start) return false;
    uintptr_t page = start & ~(uintptr_t)(Page - 1);
    if (start + size > UINTPTR_MAX - (Page - 1)) return false;
    size_t bytes = ((start + size + Page - 1) & ~(uintptr_t)(Page - 1)) - page;
    Lock lock;
    Region* r = FindRegion(page, bytes);
    if (!r) return false;
    for (uintptr_t current = page; current < page + bytes;) {
        Chunk* c = FindChunk(r, current);
        if (!c || !c->executable) return false;
        current = c->primary + c->size;
    }
    return true;
}

extern "C" bool LibnxFlushCodeMemory(const void* address, size_t size) {
    uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (!size || size > UINTPTR_MAX - start) return false;
    uintptr_t page = start & ~(uintptr_t)(Page - 1);
    if (start + size > UINTPTR_MAX - (Page - 1)) return false;
    size_t bytes = ((start + size + Page - 1) & ~(uintptr_t)(Page - 1)) - page;
    Lock lock;
    Region* r = FindRegion(page, bytes);
    if (!r) return false;
    for (uintptr_t current = page; current < page + bytes;) {
        Chunk* c = FindChunk(r, current);
        if (!c || !c->executable) return false;
        current = c->primary + c->size;
    }
    for (View* view = r->views; view; view = view->next) {
        if (!Overlaps(page, bytes, view->primary, view->size)) continue;
        uintptr_t first = page > view->primary ? page : view->primary;
        uintptr_t last = page + bytes < view->primary + view->size ? page + bytes : view->primary + view->size;
        armDCacheFlush(reinterpret_cast<void*>(view->address + first - view->primary), last - first);
    }
    armICacheInvalidate(reinterpret_cast<void*>(page), bytes);
    return true;
}

// Template sharing is optional, as on Windows. A null CreateTemplate selects
// the existing UnlockedInterleavedLoaderHeap dynamic path: separate adjacent
// RW-data/RX-code commitments, code generator, and ordinary writer holders.
// Keep that upstream implementation instead of adding Horizon-specific stubs.
void* VMToOSInterface::CreateTemplate(void*, size_t, void (*)(uint8_t*, uint8_t*, size_t)) { return nullptr; }
bool VMToOSInterface::AllocateThunksFromTemplateRespectsStartAddress() { return false; }
void* VMToOSInterface::AllocateThunksFromTemplate(void*, size_t, void*, void (*)(uint8_t*, size_t)) { return nullptr; }
bool VMToOSInterface::FreeThunksFromTemplate(void*, size_t) { return false; }
