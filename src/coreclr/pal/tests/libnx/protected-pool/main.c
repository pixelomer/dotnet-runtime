// Original tests of the production shared allocator, using actual Horizon SVCs.
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nxvm.h"

u32 __nx_applet_exit_mode = 1;
static FILE *output;
static unsigned checks;
static int fail_permission_after;
Result __real_svcSetMemoryPermission(void *, u64, u32);
Result __wrap_svcSetMemoryPermission(void *address, u64 size, u32 permission) {
    if (fail_permission_after && --fail_permission_after == 0) return 0xce01;
    return __real_svcSetMemoryPermission(address, size, permission);
}
static void check(bool ok, const char *name) {
    ++checks;
    if (!ok) { fprintf(output, "FAIL %s check=%u svc=%08x\n", name, checks, nxvm_stats().last_svc_error); abort(); }
}
static bool permission(void *address, u32 expected) {
    MemoryInfo info; u32 page;
    return R_SUCCEEDED(svcQueryMemory(&info, &page, (uintptr_t)address)) && info.perm == expected;
}
static size_t blocks(void *address, size_t size) {
    uintptr_t at = (uintptr_t)address, end = at + size;
    size_t count = 0;
    while (at < end) {
        MemoryInfo info; u32 page;
        if (R_FAILED(svcQueryMemory(&info, &page, at)) || info.addr + info.size <= at) return SIZE_MAX;
        at = info.addr + info.size; ++count;
    }
    return count;
}
typedef struct { unsigned failures; } Worker;
static void worker(void *argument) {
    Worker *w = argument;
    for (unsigned life = 0; life < 128; ++life) {
        unsigned char *p = nxvm_reserve(16384, 65536);
        if (!p || !nxvm_commit(p, 8192)) { ++w->failures; return; }
        if (p[0] || p[8191]) ++w->failures;
        memset(p, 0x5b, 8192);
        if (!nxvm_decommit(p, 8192) || !nxvm_commit(p, 8192)) { ++w->failures; return; }
        if (p[0] || p[8191]) ++w->failures;
        if (!nxvm_release(p, 16384)) { ++w->failures; return; }
    }
}
static void suite(bool protected_data) {
    const size_t page = 4096, capacity = (size_t)64 << 20;
    check(protected_data ? nxvm_init_protected(capacity) : nxvm_init(capacity), "initialize backend");
    check(nxvm_ensure_initialized(capacity / 2) && nxvm_stats().capacity == capacity, "reuse configured pool");
    check(!nxvm_init_protected(capacity), "reject second initialization");
    check(nxvm_virtual_capacity() == capacity * (protected_data ? 1 : 2), "documented virtual capacity");
    unsigned char *sparse = nxvm_reserve(capacity + page, page);
    if (protected_data) check(!sparse, "bounded data alias rejects oversized reservation");
    else {
        check(sparse && nxvm_commit(sparse, page) && nxvm_commit(sparse + capacity, page), "stack backend retains sparse overcommit");
        check(nxvm_release(sparse, capacity + page), "release sparse reservation");
    }
    unsigned char *p = nxvm_reserve(32 * 1024 * 1024, 65536);
    check(p && !((uintptr_t)p & 65535), "aligned reservation");
    check(!nxvm_destroy(), "reject destroy with live reservations");
    check(permission(p - page, Perm_None) && permission(p + 32 * 1024 * 1024, Perm_None), "guards inaccessible");
    check(!nxvm_commit(p + 1, page) && !nxvm_commit(p, SIZE_MAX), "reject invalid commitment");
    check(nxvm_commit(p + page, page), "preexisting page");
    p[page] = 0x6d;
    if (protected_data) {
        fail_permission_after = 2;
        check(!nxvm_commit(p, page * 3), "injected second commit failure");
        check(nxvm_stats().committed == page && !nxvm_stats().poisoned, "rollback accounting");
        check(!nxvm_is_committed(p, page) && nxvm_is_committed(p + page, page) &&
              !nxvm_is_committed(p + 2 * page, page), "rollback preserves previous commitment");
        check(permission(p, Perm_None) && permission(p + page, Perm_Rw) && p[page] == 0x6d, "rollback permissions and bytes");
    }
    check(nxvm_commit(p, page * 3) && p[0] == 0 && p[page] == 0x6d && p[page * 2] == 0, "overlap preserves old bytes");
    for (size_t i = 0; i < 8192; ++i) {
        check(nxvm_commit(p + i * page, page), "individual page commit");
        p[i * page] = 0x7c;
    }
    size_t count = blocks(p, 32 * 1024 * 1024);
    fprintf(output, "BACKEND protected=%d pages=8192 blocks=%zu\n", protected_data, count);
    check(protected_data ? count == 1 : count >= 8190, "expected mapping coalescence");
    check(nxvm_stats().committed == 32 * 1024 * 1024, "idempotent accounting");
    check(nxvm_decommit(p + page, 2 * page) && nxvm_decommit(p + page, 2 * page), "idempotent decommit");
    check(permission(p + page, Perm_None) && p[0] == 0x7c && p[3 * page] == 0x7c, "decommit preserves neighbors");
    check(nxvm_commit(p + page, 2 * page) && p[page] == 0 && p[3 * page - 1] == 0, "fresh recommit zeroes all bytes");
    check(!nxvm_release(p, page), "reject partial release");
    check(nxvm_release(p, 32 * 1024 * 1024), "release all pages");
    unsigned char *reuse = nxvm_reserve(32 * 1024 * 1024, 65536);
    check(reuse == p && nxvm_commit(reuse, page) && reuse[0] == 0, "reuse zeroed virtual hole");
    check(nxvm_release(reuse, 32 * 1024 * 1024), "release reused hole");
    Thread threads[8]; Worker workers[8] = {0};
    for (unsigned i = 0; i < 8; ++i) {
        check(R_SUCCEEDED(threadCreate(&threads[i], worker, &workers[i], NULL, 0x10000, 0x2c, -2)), "create allocator worker");
        check(R_SUCCEEDED(threadStart(&threads[i])), "start allocator worker");
    }
    for (unsigned i = 0; i < 8; ++i) {
        check(R_SUCCEEDED(threadWaitForExit(&threads[i])) && R_SUCCEEDED(threadClose(&threads[i])) &&
              !workers[i].failures, "concurrent lifetimes");
    }
    NxvmStats stats = nxvm_stats();
    check(!stats.reservations && !stats.reserved && !stats.committed && !stats.poisoned, "all ownership retired");
    check(nxvm_destroy() && !nxvm_stats().capacity, "destroy releases alias and backing");
    fprintf(output, "PASS backend=%d checks=%u lifetimes=1024\n", protected_data, checks);
}
int main(void) {
    output = fopen("sdmc:/switch/coreclr-protected-pool.txt", "w");
    if (!output) return 1;
    setvbuf(output, NULL, _IONBF, 0);
    suite(false);
    fail_permission_after = 1;
    check(!nxvm_init_protected(64 * 1024 * 1024) && !nxvm_stats().capacity &&
          !nxvm_stats().poisoned, "failed initialization releases all ownership");
    suite(true);
    // Verify that cleanup permits returning to the original backend.
    check(nxvm_init(64 * 1024 * 1024) && nxvm_destroy(), "backend lifecycle reset");
    check(nxvm_init_protected(64 * 1024 * 1024), "initialize failure-isolation case");
    void *p = nxvm_reserve(8192, 4096);
    check(p && nxvm_commit(p, 8192), "commit failure-isolation pages");
    fail_permission_after = 1;
    check(!nxvm_decommit(p, 8192) && nxvm_stats().poisoned && nxvm_stats().committed == 8192,
          "failed decommit retains ownership and poisons pool");
    check(!nxvm_commit(p, 8192) && !nxvm_release(p, 8192) && !nxvm_destroy(), "poisoned pool fails closed");
    // Process teardown reclaims this intentionally poisoned final instance.
    fprintf(output, "END checks=%u failures=0\n", checks);
    fclose(output); return 0;
}
