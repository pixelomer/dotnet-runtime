// Original homebrew mapping-budget probe. No managed runtime or game code.
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u32 __nx_applet_exit_mode = 1;
static FILE *logfile;
static unsigned checks, failures;
static void check(int value, const char *name) {
    ++checks;
    if (!value) { ++failures; fprintf(logfile, "FAIL %s\n", name); }
}
static size_t blocks(void *base, size_t size) {
    uintptr_t at = (uintptr_t)base, end = at + size;
    size_t count = 0;
    while (at < end) {
        MemoryInfo info; u32 page;
        Result rc = svcQueryMemory(&info, &page, at);
        if (R_FAILED(rc) || info.addr + info.size <= at) return SIZE_MAX;
        ++count; at = info.addr + info.size;
    }
    return count;
}
int main(void) {
    logfile = fopen("sdmc:/switch/coreclr-data-mapping.txt", "w");
    if (!logfile) return 1;
    setvbuf(logfile, NULL, _IONBF, 0);
    const size_t page = 4096, size = 32 * 1024 * 1024;
    unsigned char *backing = aligned_alloc(page, size);
    if (!backing) { fclose(logfile); return 1; }
    memset(backing, 0, size);
    virtmemLock();
    void *alias = virtmemFindStack(size, page);
    VirtmemReservation *reservation = alias ? virtmemAddReservation(alias, size) : NULL;
    virtmemUnlock();
    if (!reservation) { free(backing); fclose(logfile); return 1; }
    size_t mapped = 0; Result result = 0;
    for (; mapped < size; mapped += page) {
        result = svcMapMemory((char *)alias + mapped, backing + mapped, page);
        if (R_FAILED(result)) break;
        ((unsigned char *)alias)[mapped] = (unsigned char)(mapped / page);
        if ((mapped + page) % (4 * 1024 * 1024) == 0)
            fprintf(logfile, "STACK pages=%zu blocks=%zu\n", (mapped + page) / page, blocks(alias, mapped + page));
    }
    fprintf(logfile, "STACK final_pages=%zu result=%08x blocks=%zu\n", mapped / page, result, blocks(alias, mapped));
    // Resource exhaustion is an observed limit, not an expected-success check.
    for (size_t offset = 0; offset < mapped; offset += page) {
        Result rc = svcUnmapMemory((char *)alias + offset, backing + offset, page);
        if (R_FAILED(rc)) { fprintf(logfile, "FAIL stack cleanup %08x\n", rc); fclose(logfile); return 1; }
        check(backing[offset] == (unsigned char)(offset / page), "stack alias preserves bytes");
    }
    virtmemLock(); virtmemRemoveReservation(reservation); virtmemUnlock();

    Handle process = envGetOwnProcessHandle();
    virtmemLock();
    alias = virtmemFindCodeMemory(size, page);
    reservation = alias ? virtmemAddReservation(alias, size) : NULL;
    virtmemUnlock();
    if (!reservation || process == INVALID_HANDLE) { free(backing); fclose(logfile); return 1; }
    result = svcMapProcessCodeMemory(process, (uintptr_t)alias, (uintptr_t)backing, size);
    fprintf(logfile, "DATA map=%08x\n", result);
    if (R_FAILED(result)) { fclose(logfile); return 1; }
    result = svcSetProcessMemoryPermission(process, (uintptr_t)alias, size, Perm_Rw);
    fprintf(logfile, "DATA convert=%08x\n", result);
    if (R_FAILED(result)) { fclose(logfile); return 1; }
    result = svcSetMemoryPermission(alias, size, Perm_None);
    fprintf(logfile, "DATA protect-none=%08x\n", result);
    if (R_FAILED(result)) { fclose(logfile); return 1; }
    for (size_t offset = 0; offset < size; offset += page) {
        result = svcSetMemoryPermission((char *)alias + offset, page, Perm_Rw);
        if (R_FAILED(result)) { fprintf(logfile, "FAIL data commit page=%zu result=%08x\n", offset / page, result); fclose(logfile); return 1; }
        ((unsigned char *)alias)[offset] = 0xa5;
    }
    size_t data_blocks = blocks(alias, size);
    fprintf(logfile, "DATA pages=%zu blocks=%zu\n", size / page, data_blocks);
    check(data_blocks == 1, "adjacent writable data pages coalesce");
    check(R_SUCCEEDED(svcSetMemoryPermission((char *)alias + page, page, Perm_R)), "data read-only");
    check(((unsigned char *)alias)[page] == 0xa5, "read-only bytes preserved");
    check(R_SUCCEEDED(svcSetMemoryPermission((char *)alias + page, page, Perm_None)), "data no-access");
    MemoryInfo info; u32 page_info;
    check(R_SUCCEEDED(svcQueryMemory(&info, &page_info, (uintptr_t)alias + page)) && info.perm == Perm_None, "no-access query");
    result = svcSetMemoryPermission((char *)alias + page, page, Perm_Rw);
    check(R_SUCCEEDED(result), "data writable again");
    if (R_FAILED(result)) { fclose(logfile); return 1; }
    memset((char *)alias + page, 0, page);
    check(((unsigned char *)alias)[page] == 0, "fresh page can be cleared");
    check(blocks(alias, size) == 1, "recommitted data coalesces again");
    result = svcUnmapProcessCodeMemory(process, (uintptr_t)alias, (uintptr_t)backing, size);
    check(R_SUCCEEDED(result), "whole data alias release");
    if (R_SUCCEEDED(result)) {
        virtmemLock(); virtmemRemoveReservation(reservation); virtmemUnlock();
        free(backing);
    }
    fprintf(logfile, "END checks=%u failures=%u\n", checks, failures);
    fclose(logfile);
    return failures != 0;
}
