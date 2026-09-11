#include "nxvm.h"
#include <switch.h>
#include <stdlib.h>
#include <string.h>

#define PAGE ((size_t)0x1000)
#define NEW_PAGE UINT32_C(0x80000000)
#define INDEX_MASK UINT32_C(0x7fffffff)

typedef struct Region {
    struct Region *next;
    unsigned char *address;
    size_t pages;
    uint32_t *backing; // Pool page index + 1; high bit marks a commit transaction.
    VirtmemReservation *reservation;
} Region;

static Mutex lock;
static unsigned char *pool, *used;
static size_t pool_pages, used_pages;
static Region *regions;
static NxvmStats stats;

static bool valid_size(size_t n) { return n && !(n & (PAGE - 1)); }
static Region *find_region(void *address, size_t bytes, size_t *offset) {
    uintptr_t a = (uintptr_t)address;
    if (!valid_size(bytes) || (a & (PAGE - 1))) return NULL;
    for (Region *r = regions; r; r = r->next) {
        uintptr_t base = (uintptr_t)r->address;
        size_t size = r->pages * PAGE;
        if (a >= base && a - base <= size && bytes <= size - (a - base)) {
            *offset = (a - base) / PAGE;
            return r;
        }
    }
    return NULL;
}

// Caller owns lock. Only unmaps entries selected by this operation. Consecutive
// virtual pages are coalesced only when their backing addresses are contiguous.
static bool unmap_pages(Region *r, size_t first, size_t count, bool new_only) {
    size_t end = first + count;
    for (size_t i = first; i < end;) {
        uint32_t value = r->backing[i];
        if (!value || (new_only && !(value & NEW_PAGE))) { i++; continue; }
        size_t backing = (value & INDEX_MASK) - 1, n = 1;
        while (i + n < end && r->backing[i + n] &&
               (!new_only || (r->backing[i + n] & NEW_PAGE)) &&
               (r->backing[i + n] & INDEX_MASK) == backing + n + 1) n++;
        Result rc = svcUnmapMemory(r->address + i * PAGE, pool + backing * PAGE, n * PAGE);
        if (R_FAILED(rc)) {
            stats.last_svc_error = rc;
            // Retain ownership and backing. A partially failed unmap cannot be
            // reported as a healthy allocator; callers must stop using it.
            stats.poisoned = true;
            return false;
        }
        memset(used + backing, 0, n);
        memset(r->backing + i, 0, n * sizeof(uint32_t));
        used_pages -= n;
        stats.committed = used_pages * PAGE;
        i += n;
    }
    return true;
}

static bool initialize_locked(size_t backing_bytes) {
    bool ok = false;
    if (pool || !valid_size(backing_bytes) || backing_bytes / PAGE >= INDEX_MASK ||
        !envIsSyscallHinted(0x04) || !envIsSyscallHinted(0x05)) goto end;
    unsigned char *p = aligned_alloc(PAGE, backing_bytes);
    unsigned char *u = calloc(backing_bytes / PAGE, 1);
    if (!p || !u) { free(p); free(u); goto end; }
    pool = p; used = u; pool_pages = backing_bytes / PAGE; used_pages = 0;
    stats = (NxvmStats){ .capacity = backing_bytes };
    ok = true;
end:
    return ok;
}

bool nxvm_init(size_t backing_bytes) {
    mutexLock(&lock);
    bool ok = initialize_locked(backing_bytes);
    mutexUnlock(&lock); return ok;
}

bool nxvm_ensure_initialized(size_t backing_bytes) {
    mutexLock(&lock);
    bool ok = pool ? !stats.poisoned : initialize_locked(backing_bytes);
    mutexUnlock(&lock); return ok;
}

bool nxvm_destroy(void) {
    mutexLock(&lock);
    bool ok = pool && !regions && !used_pages && !stats.poisoned;
    if (ok) {
        free(pool); free(used); pool = used = NULL; pool_pages = 0;
        stats = (NxvmStats){0};
    }
    mutexUnlock(&lock); return ok;
}

void *nxvm_reserve(size_t bytes, size_t alignment) {
    if (!alignment) alignment = PAGE;
    if (!valid_size(bytes) || alignment < PAGE || (alignment & (alignment - 1)) ||
        bytes > SIZE_MAX - (alignment - PAGE)) return NULL;
    mutexLock(&lock);
    Region *r = NULL; void *result = NULL;
    if (!pool || stats.poisoned || bytes > SIZE_MAX - stats.reserved) goto end;
    r = calloc(1, sizeof(*r));
    if (!r) goto end;
    r->pages = bytes / PAGE;
    r->backing = calloc(r->pages, sizeof(uint32_t));
    if (!r->backing) goto fail;
    virtmemLock();
    void *raw = virtmemFindStack(bytes + alignment - PAGE, PAGE);
    if (raw && (uintptr_t)raw <= UINTPTR_MAX - (alignment - 1)) {
        r->address = (void *)(((uintptr_t)raw + alignment - 1) & ~(alignment - 1));
        r->reservation = virtmemAddReservation(r->address, bytes);
    }
    virtmemUnlock();
    if (!r->reservation) goto fail;
    r->next = regions; regions = r;
    stats.reserved += bytes; stats.reservations++;
    result = r->address;
    goto end;
fail:
    free(r->backing); free(r);
end:
    mutexUnlock(&lock); return result;
}

bool nxvm_commit(void *address, size_t bytes) {
    mutexLock(&lock);
    bool ok = false; size_t first = 0;
    Region *r = find_region(address, bytes, &first);
    if (!r || stats.poisoned) goto end;
    size_t end_page = first + bytes / PAGE, needed = 0;
    for (size_t i = first; i < end_page; i++) needed += !r->backing[i];
    if (needed > pool_pages - used_pages) goto end;
    for (size_t i = first, cursor = 0; i < end_page;) {
        if (r->backing[i]) { i++; continue; }
        while (cursor < pool_pages && used[cursor]) cursor++;
        size_t n = 0;
        while (i + n < end_page && !r->backing[i + n] &&
               cursor + n < pool_pages && !used[cursor + n]) n++;
        if (!n) goto rollback;
        // These pages are exclusively owned and currently unmapped at source.
        memset(pool + cursor * PAGE, 0, n * PAGE);
        Result rc = svcMapMemory(r->address + i * PAGE, pool + cursor * PAGE, n * PAGE);
        if (R_FAILED(rc)) { stats.last_svc_error = rc; goto rollback; }
        for (size_t j = 0; j < n; j++)
            r->backing[i + j] = NEW_PAGE | (uint32_t)(cursor + j + 1);
        memset(used + cursor, 1, n);
        used_pages += n; stats.committed = used_pages * PAGE;
        i += n; cursor += n;
    }
    for (size_t i = first; i < end_page; i++) r->backing[i] &= INDEX_MASK;
    ok = true; goto end;
rollback:
    unmap_pages(r, first, bytes / PAGE, true);
end:
    mutexUnlock(&lock); return ok;
}

bool nxvm_decommit(void *address, size_t bytes) {
    mutexLock(&lock);
    size_t first = 0; Region *r = find_region(address, bytes, &first);
    bool ok = r && !stats.poisoned && unmap_pages(r, first, bytes / PAGE, false);
    mutexUnlock(&lock); return ok;
}

bool nxvm_release(void *address, size_t bytes) {
    mutexLock(&lock);
    size_t first = 0; Region *r = find_region(address, bytes, &first);
    bool ok = r && !first && bytes == r->pages * PAGE && !stats.poisoned;
    if (!ok || !unmap_pages(r, 0, r->pages, false)) { ok = false; goto end; }
    Region **link = &regions;
    while (*link != r) link = &(*link)->next;
    *link = r->next;
    virtmemLock(); virtmemRemoveReservation(r->reservation); virtmemUnlock();
    stats.reserved -= bytes; stats.reservations--;
    free(r->backing); free(r);
end:
    mutexUnlock(&lock); return ok;
}

NxvmStats nxvm_stats(void) {
    mutexLock(&lock); NxvmStats result = stats; mutexUnlock(&lock); return result;
}

bool nxvm_is_committed(void *address, size_t bytes) {
    mutexLock(&lock);
    size_t first = 0;
    Region *r = find_region(address, bytes, &first);
    bool ok = r && !stats.poisoned;
    if (ok) {
        for (size_t i = first; i < first + bytes / PAGE; i++) {
            if (!r->backing[i]) { ok = false; break; }
        }
    }
    mutexUnlock(&lock);
    return ok;
}
