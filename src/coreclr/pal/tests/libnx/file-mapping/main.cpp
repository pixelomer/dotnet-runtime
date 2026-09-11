#include "pal.h"
#include "pal/mapnative.h"
#include <atomic>
#include <pthread.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/result.h>
unsigned __nx_applet_exit_mode = 1;
}
static FILE* output;
static thread_local unsigned failProtect, failMap;
extern "C" Result __real_svcSetProcessMemoryPermission(Handle, u64, u64, u32);
extern "C" Result __wrap_svcSetProcessMemoryPermission(Handle handle, u64 address, u64 size, u32 permission) {
    if (failProtect && --failProtect == 0) return 0xce01;
    return __real_svcSetProcessMemoryPermission(handle, address, size, permission);
}
extern "C" Result __real_svcMapProcessMemory(void*, Handle, u64, u64);
extern "C" Result __wrap_svcMapProcessMemory(void* destination, Handle process, u64 source, u64 size) {
    if (failMap && --failMap == 0) return 0xce01;
    return __real_svcMapProcessMemory(destination, process, source, size);
}
static std::atomic<unsigned> checks{0};
static int fd;
static unsigned char pattern[4 * 4096 + 53];
static void check(bool yes, const char* name) {
    unsigned n = ++checks;
    if (!yes) { fprintf(output, "FAIL %s check=%u errno=%d\n", name, n, errno); abort(); }
}
static u32 permission(void* address) {
    MemoryInfo info{}; u32 page;
    check(R_SUCCEEDED(svcQueryMemory(&info, &page, reinterpret_cast<u64>(address))), "kernel query");
    return info.perm;
}
static void* worker(void*) {
    for (unsigned cycle = 0; cycle < 32; ++cycle) {
        auto image = static_cast<unsigned char*>(NativeMap(nullptr, 6 * 4096, MapNone, MapPrivate | MapAnonymous, -1, 0));
        check(image != MapFailed, "reserve image layout");
        check(permission(image) == Perm_None, "reservation inaccessible");
        check(NativeMap(image, 4096, MapRead, MapPrivate | MapFixed, fd, 0) == image, "place header snapshot");
        check(NativeMap(image + 8192, 8192, MapRead, MapPrivate | MapFixed, fd, 8192) == image + 8192, "place section leaving gap");
        check(memcmp(image, pattern, 4096) == 0 && memcmp(image + 8192, pattern + 8192, 8192) == 0, "file data at virtual offsets");
        check(permission(image) == Perm_R && permission(image + 4096) == Perm_None, "read-only data and guard hole");
        check(NativeMap(image, 4096, MapRead, MapPrivate | MapFixed, fd, 4096) == MapFailed, "live replacement rejected intact");
        check(memcmp(image, pattern, 4096) == 0, "failed replacement preserves header");
        DWORD old = 0;
        check(VirtualProtect(image, 4096, PAGE_NOACCESS, &old) && permission(image) == Perm_None, "no-access retains backing");
        check(VirtualProtect(image, 4096, PAGE_READONLY, &old) && old == PAGE_NOACCESS && memcmp(image, pattern, 4096) == 0, "restore access without data loss");
        check(VirtualProtect(image + 8192, 4096, PAGE_READWRITE, &old) && old == PAGE_READONLY, "PAL reprotect private section");
        image[8192] ^= 0xff;
        check(VirtualProtect(image + 8192, 4096, PAGE_READONLY, &old) && old == PAGE_READWRITE, "restore section protection");
        check(NativeDiscard(image + 8192, 4096) == 0 && image[8192] == (unsigned char)(pattern[8192] ^ 0xff), "advisory discard retains private edits");
        // Partial retirement of a two-page backing allocation must keep its
        // second page and heap ownership alive until that page is also retired.
        check(NativeUnmap(image + 8192, 4096) == 0, "partial backing retirement");
        check(memcmp(image + 12288, pattern + 12288, 4096) == 0, "remaining backing page intact");
        check(NativeUnmap(image + 12288, 4096) == 0, "final backing page retirement");
        check(NativeUnmap(image, 4096) == 0, "retire header");
        check(NativeReleaseImageReservation(image + 4096) == 0, "retire gaps and alignment padding");
        auto tail = static_cast<unsigned char*>(NativeMap(nullptr, 53, MapRead, MapShared, fd, 16384));
        check(tail != MapFailed && memcmp(tail, pattern + 16384, 53) == 0, "short final file page");
        bool zeros = true; for (unsigned i = 53; i < 4096; ++i) zeros &= tail[i] == 0;
        check(zeros, "tail page zero padding");
        check(!VirtualProtect(tail, 4096, PAGE_READWRITE, &old), "read-only shared snapshot cannot become writable");
        check(PAL_LOADAcquireWritableView(tail, 53) == nullptr, "shared snapshot cannot acquire private writer");
        check(NativeUnmap(tail, 53) == 0, "unmap non-page length");
        auto code = static_cast<unsigned char*>(NativeMap(nullptr, 4096, MapRead | MapExecute, MapPrivate, fd, 0));
        check(code != MapFailed, "private executable section backing");
        check(!VirtualProtect(code, 4096, PAGE_READWRITE, &old) && permission(code) == Perm_Rx, "RX-to-RW rejects irreversible state conversion intact");
        auto writable = static_cast<unsigned char*>(PAL_LOADAcquireWritableView(code, 4096));
        check(writable && writable != code && permission(writable) == Perm_Rw && permission(code) == Perm_Rx, "simultaneous image RW and RX aliases");
        const uint32_t words[] = {0xd2800540, 0xd65f03c0}; // MOVZ X0,#42; RET
        memcpy(writable, words, sizeof(words));
        auto overlap = static_cast<unsigned char*>(PAL_LOADAcquireWritableView(code + 4, 4));
        check(overlap && memcmp(overlap, words + 1, 4) == 0, "overlapping unaligned writer view");
        check(NativeUnmap(code, 4096) == -1 && errno == EBUSY, "live writer pins primary backing");
        check(NativeReleaseImageReservation(code) == -1 && errno == EBUSY, "whole image retirement rejects live writer intact");
        PAL_LOADReleaseWritableView(writable);
        check(permission(writable) == Perm_None && permission(code) == Perm_Rx, "writer alias retired without removing executable primary");
        check(reinterpret_cast<uint64_t(*)()>(code)() == 42, "execute privately relocated section");
        check(memcmp(overlap, words + 1, 4) == 0, "overlapping writer survives other release");
        PAL_LOADReleaseWritableView(overlap);
        writable = static_cast<unsigned char*>(PAL_LOADAcquireWritableView(code, 4096));
        check(writable != nullptr, "reacquire writer for already executed code");
        reinterpret_cast<uint32_t*>(writable)[0] = 0xd2800560; // MOVZ X0,#43
        PAL_LOADReleaseWritableView(writable);
        check(reinterpret_cast<uint64_t(*)()>(code)() == 43, "modified instruction published after prior execution");
        check(VirtualProtect(code, 4096, PAGE_NOACCESS, &old) && old == PAGE_EXECUTE_READ, "executable page can become inaccessible");
        check(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old) && old == PAGE_NOACCESS, "restore execute without writable state");
        check(reinterpret_cast<uint64_t(*)()>(code)() == 43, "code retained after no-access transition");
        check(NativeUnmap(code, 4096) == 0, "retire executable section");
        auto small = static_cast<unsigned char*>(NativeMap(nullptr, 1, MapRead, MapPrivate, fd, 0));
        check(small != MapFailed && memcmp(small, pattern, 4096) == 0, "whole last page reflects file bytes");
        check(NativeUnmap(small, 1) == 0, "retire small view");
        check(NativeMap(nullptr, 8192, MapRead, MapPrivate, fd, 16384) == MapFailed, "whole pages beyond EOF rejected");
        check(NativeMap(nullptr, 4096, MapRead | MapWrite, MapShared, fd, 0) == MapFailed, "shared writable file rejected");
        check(NativeMap(nullptr, SIZE_MAX, MapRead, MapPrivate, fd, 0) == MapFailed, "overflow rejected");
        image = static_cast<unsigned char*>(NativeMap(nullptr, 8192, MapNone, MapAnonymous | MapPrivate, -1, 0));
        check(image != MapFailed, "failed-load reservation");
        check(NativeMap(image, 4096, MapRead, MapPrivate | MapFixed, -1, 0) == MapFailed, "file failure before first section");
        check(NativeReleaseImageReservation(image) == 0, "cleanup image with no recorded sections");
    }
    return nullptr;
}
static void failureChecks() {
    auto memory = static_cast<unsigned char*>(NativeMap(nullptr, 8192, MapRead, MapPrivate, fd, 0));
    check(memory != MapFailed, "rollback test allocation");
    failProtect = 2;
    check(NativeProtect(memory, 8192, MapRead | MapWrite) == -1, "injected second-page protect failure");
    check(permission(memory) == Perm_R && permission(memory + 4096) == Perm_R, "failed protection restores earlier page permissions");
    check(memcmp(memory, pattern, 8192) == 0, "failed protection preserves bytes");
    // Page zero is now AliasCodeData even though its R permission was restored.
    // A mixed-state range must validate completely before changing either page.
    check(NativeProtect(memory, 8192, MapRead | MapExecute) == -1, "data-state cannot be promoted to executable");
    check(permission(memory) == Perm_R && permission(memory + 4096) == Perm_R, "unsupported mixed-state protection leaves whole range intact");
    check(NativeProtect(memory, 8192, MapRead | MapWrite) == 0, "mixed code/data states become writable using correct kernel APIs");
    DWORD old;
    check(!VirtualProtect(memory, 8192, PAGE_EXECUTE_READ, &old), "PAL does not bypass owned data-to-code rejection");
    failMap = 1;
    check(PAL_LOADAcquireWritableView(memory, 8192) == nullptr, "injected writer mapping failure");
    auto writer = static_cast<unsigned char*>(PAL_LOADAcquireWritableView(memory + 4090, 16));
    check(writer != nullptr && memcmp(writer, pattern + 4090, 16) == 0, "writer retry spans pages after rollback");
    PAL_LOADReleaseWritableView(writer);
    check(NativeUnmap(memory, 8192) == 0, "retire rollback test allocation");
    // A real PE image has independently backed adjacent sections.
    memory = static_cast<unsigned char*>(NativeMap(nullptr, 8192, MapNone, MapAnonymous | MapPrivate, -1, 0));
    check(memory != MapFailed, "reserve cross-section alias test");
    check(NativeMap(memory, 4096, MapRead | MapExecute, MapPrivate | MapFixed, fd, 0) == memory, "first independent section");
    check(NativeMap(memory + 4096, 4096, MapRead, MapPrivate | MapFixed, fd, 4096) == memory + 4096, "second independent section");
    writer = static_cast<unsigned char*>(PAL_LOADAcquireWritableView(memory + 4090, 16));
    check(writer && memcmp(writer, pattern + 4090, 16) == 0, "writer crosses independently backed sections");
    memset(writer, 0x5a, 16);
    PAL_LOADReleaseWritableView(writer);
    check(memory[4090] == 0x5a && memory[4105] == 0x5a && permission(memory) == Perm_Rx && permission(memory + 4096) == Perm_R, "cross-section write preserves both primary protections");
    check(NativeReleaseImageReservation(memory) == 0, "retire independent sections");
}
int main() {
    output = fopen("sdmc:/switch/coreclr-filemap-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN PAL native file mapping and VirtualProtect; no managed runtime\n");
    for (unsigned i = 0; i < sizeof(pattern); ++i) pattern[i] = (i * 37 + i / 4096) & 255;
    FILE* file = fopen("sdmc:/switch/coreclr-filemap-probe.bin", "wb");
    check(file && fwrite(pattern, 1, sizeof(pattern), file) == sizeof(pattern), "write owned test input");
    check(fclose(file) == 0, "close input writer");
    fd = open("sdmc:/switch/coreclr-filemap-probe.bin", O_RDONLY);
    check(fd >= 0, "open input descriptor");
    check(lseek(fd, 37, SEEK_SET) == 37, "set independent descriptor position");
    unsigned char byte;
    check(pread(fd, &byte, 1, sizeof(pattern)) == 0, "positional read at EOF");
    check(pread(fd, &byte, 1, -1) == -1 && errno == EINVAL, "negative read offset rejected");
    check(pread(-1, &byte, 1, 0) == -1 && errno == EBADF, "invalid read descriptor rejected");
    failureChecks();
    for (unsigned round = 0; round < 8; ++round) {
        pthread_t threads[4];
        for (auto& t : threads) check(pthread_create(&t, nullptr, worker, nullptr) == 0, "create worker");
        for (auto& t : threads) check(pthread_join(t, nullptr) == 0, "join worker");
        check(lseek(fd, 0, SEEK_CUR) == 37, "concurrent positional reads preserve descriptor position");
        fprintf(output, "ROUND %u checks=%u native_used=%zu\n", round, checks.load(), mallinfo().uordblks);
    }
    unsigned char verify[sizeof(pattern)];
    check(pread(fd, verify, sizeof(verify), 0) == sizeof(verify) && memcmp(verify, pattern, sizeof(pattern)) == 0, "private edits never changed file");
    close(fd);
    fprintf(output, "PASS checks=%u cycles=1024 threads=32\n", checks.load());
    fclose(output); return 0;
}
