#include "pal.h"
#include "pal/debug.h"
#include "../../../../minipal/minipal.h"
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/result.h>
}
#include <atomic>
#include <initializer_list>
#include <pthread.h>
#include <malloc.h>
extern "C" { unsigned __nx_applet_exit_mode = 1; }
// Link-time wrappers inject one failure at the Horizon boundary; successful
// operations still use the real kernel. Production allocator has no test hooks.
static std::atomic<int> failCreate{0}, failOwner{0}, failSlave{0};
static bool injected(std::atomic<int>& counter) {
    int value = counter.load();
    return value > 0 && counter.fetch_sub(1) == 1;
}
extern "C" Result __real_svcCreateCodeMemory(Handle*, void*, u64);
extern "C" Result __real_svcControlCodeMemory(Handle, CodeMapOperation, void*, u64, u64);
extern "C" Result __wrap_svcCreateCodeMemory(Handle* handle, void* source, u64 size) {
    if (injected(failCreate)) return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    return __real_svcCreateCodeMemory(handle, source, size);
}
extern "C" Result __wrap_svcControlCodeMemory(Handle handle, CodeMapOperation op, void* address, u64 size, u64 permission) {
    if ((op == CodeMapOperation_MapOwner && injected(failOwner)) ||
        (op == CodeMapOperation_MapSlave && injected(failSlave)))
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    return __real_svcControlCodeMemory(handle, op, address, size, permission);
}
static FILE* output;
static std::atomic<unsigned> checks{0};
using VM = VMToOSInterface;
using Function = uint64_t (*)();
static Function sharedFunction;
static void check(bool yes, const char* name) {
    unsigned n = ++checks;
    if (!yes) { fprintf(output, "FAIL %s check=%u\n", name, n); abort(); }
}
static u32 permission(void* p) {
    MemoryInfo info{}; u32 page;
    check(R_SUCCEEDED(svcQueryMemory(&info, &page, reinterpret_cast<u64>(p))), "query permission");
    return info.perm;
}
static void emit(void* rw, unsigned number) {
    uint32_t words[] = {0xd2800000u | (number << 5), 0xd65f03c0u};
    memcpy(rw, words, sizeof(words));
}
static bool zero(const unsigned char* p, size_t size) {
    for (size_t i = 0; i < size; ++i) if (p[i]) return false;
    return true;
}
struct Work { void* mapper; unsigned id; };
static void* worker(void* argument) {
    Work& work = *static_cast<Work*>(argument);
    size_t offset = size_t(work.id) * 65536;
    for (unsigned i = 0; i < 64; ++i) {
        check(sharedFunction() == 2748, "main-thread publication visible to workers");
        auto p = static_cast<unsigned char*>(VM::ReserveDoubleMappedMemory(work.mapper, offset, 32768, nullptr, nullptr));
        check(p != nullptr, "reserve reused logical offset");
        check(permission(p) == Perm_None, "reservation initially inaccessible");
        check(VM::CommitDoubleMappedMemory(p, 8192, true) == p, "first two-page code chunk");
        check(VM::CommitDoubleMappedMemory(p + 8192, 4096, true) == p + 8192, "adjacent independent code chunk");
        check(VM::CommitDoubleMappedMemory(p + 4096, 8192, true) == p + 4096, "idempotent overlapping commit");
        check(permission(p) == Perm_Rx && permission(p + 12288) == Perm_None, "only committed pages executable");
        auto whole = static_cast<unsigned char*>(VM::GetRWMapping(work.mapper, p, offset, 12288));
        check(whole != nullptr && whole != p, "contiguous writable view across objects");
        check(zero(whole, 12288), "fresh executable backing zero");
        check(permission(whole) == Perm_Rw, "writable alias permissions");
        auto partial = static_cast<unsigned char*>(VM::GetRWMapping(work.mapper, p + 4096, offset + 4096, 4096));
        check(partial == whole + 4096, "partial view shares alias");
        check(!VM::ReleaseDoubleMappedMemory(work.mapper, p, offset, 32768), "live views prevent release");
        emit(whole, 123); emit(whole + 8192, 456);
        check(DBG_FlushInstructionCache(p, 12288), "production PAL cache publication while views live");
        check(reinterpret_cast<Function>(p)() == 123 && reinterpret_cast<Function>(p + 8192)() == 456, "execute both chunks");
        check(VM::ReleaseRWMapping(whole, 12288), "release large overlapping view");
        check(permission(partial) == Perm_Rw && permission(whole + 8192) == Perm_None, "only still-referenced object retains alias");
        emit(partial, 789);
        check(DBG_FlushInstructionCache(p + 4096, 8), "publish partial cached view");
        check(reinterpret_cast<Function>(p + 4096)() == 789, "partial view function");
        check(VM::ReleaseRWMapping(partial, 4096), "release final view");
        check(permission(whole) == Perm_None, "final writable alias really unmapped");
        check(reinterpret_cast<Function>(p)() == 123, "RX remains executable after owner unmap");
        check(!VM::ReleaseRWMapping(partial, 4096), "stale view rejected");
        check(!VM::GetRWMapping(work.mapper, p, offset + 4096, 4096), "offset mismatch rejected");
        check(!VM::GetRWMapping(work.mapper, p + 12288, offset + 12288, 4096), "uncommitted view rejected");
        check(!VM::CommitDoubleMappedMemory(p, 4096, false), "conflicting recommit rejected");
        check(reinterpret_cast<Function>(p)() == 123, "rejected recommit preserves code");
        // Follow the upstream dynamic interleaved-stub path: data before code.
        check(VM::CommitDoubleMappedMemory(p + 20480, 4096, false) == p + 20480, "adjacent loader data RW");
        check(zero(p + 20480, 4096), "data zero on commitment");
        *reinterpret_cast<uint64_t*>(p + 20480) = 987;
        check(VM::CommitDoubleMappedMemory(p + 16384, 4096, true) == p + 16384, "interleaved code RX");
        auto rw = static_cast<uint32_t*>(VM::GetRWMapping(work.mapper, p + 16384, offset + 16384, 4096));
        check(rw != nullptr, "interleaved writable view");
        // ADR X0, .; ADD X0,X0,#1,LSL#12; LDR X0,[X0]; RET.
        const uint32_t code[] = {0x10000000, 0x91400400, 0xf9400000, 0xd65f03c0};
        memcpy(rw, code, sizeof(code));
        check(DBG_FlushInstructionCache(p + 16384, sizeof(code)), "publish PC-relative stub");
        check(reinterpret_cast<Function>(p + 16384)() == 987, "RX-relative code reaches adjacent writable data");
        check(VM::ReleaseRWMapping(rw, 4096), "release stub writer");
        check(permission(p + 20480) == Perm_Rw, "data remains writable");
        check(VM::ReleaseDoubleMappedMemory(work.mapper, p, offset, 32768), "release exact reservation");
    }
    return nullptr;
}
int main() {
    output = fopen("sdmc:/switch/coreclr-exec-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN CoreCLR VMToOSInterface CodeMemory; no managed runtime\n");
    for (unsigned round = 0; round < 8; ++round) {
        void* mapper = nullptr; size_t capacity = 0;
        check(VM::CreateDoubleMemoryMapper(&mapper, &capacity), "create mapper arenas");
        check(capacity >= 262144, "reported virtual capacity");
        auto p = static_cast<unsigned char*>(VM::ReserveDoubleMappedMemory(mapper, 0, 65536, nullptr, nullptr));
        check(p != nullptr, "initial arena placement");
        check(!VM::ReserveDoubleMappedMemory(mapper, 0, 4096, nullptr, nullptr), "live logical offset overlap rejected");
        check(VM::ReleaseDoubleMappedMemory(mapper, p, 0, 65536), "release initial range");
        auto fixed = VM::ReserveDoubleMappedMemory(mapper, 4096, 4096, p + 8192, p + 8192);
        check(fixed == p + 8192, "fixed reservation in arena");
        auto bounded = VM::ReserveDoubleMappedMemory(mapper, 8192, 4096, p, p + 8192);
        check(bounded == p, "mandatory bounds honored");
        check(!VM::ReserveDoubleMappedMemory(mapper, capacity, 4096, nullptr, nullptr), "capacity bound");
        check(!VM::ReserveDoubleMappedMemory(mapper, 12288, 4096, p, p + 4096), "occupied bounds fail without escape");
        check(!VM::ReleaseDoubleMappedMemory(mapper, fixed, 0, 4096), "wrong release offset rejected");
        check(VM::ReleaseDoubleMappedMemory(mapper, fixed, 4096, 4096), "release fixed allocation");
        check(VM::ReleaseDoubleMappedMemory(mapper, bounded, 8192, 4096), "release bounded allocation");
        // Preexisting middle chunk divides the transaction into two fresh runs.
        p = static_cast<unsigned char*>(VM::ReserveDoubleMappedMemory(mapper, 0, 16384, nullptr, nullptr));
        check(p && VM::CommitDoubleMappedMemory(p + 4096, 4096, true) == p + 4096, "failure-test middle chunk");
        void* writer = VM::GetRWMapping(mapper, p + 4096, 4096, 4096);
        check(writer != nullptr, "failure-test initial writer"); emit(writer, 321);
        check(VM::ReleaseRWMapping(writer, 4096), "publish preexisting function");
        for (auto* fault : {&failCreate, &failOwner, &failSlave}) {
            *fault = 2;
            check(!VM::CommitDoubleMappedMemory(p, 12288, true), "second fresh-run failure");
            check(permission(p) == Perm_None && permission(p + 8192) == Perm_None, "fresh runs rolled back");
            check(reinterpret_cast<Function>(p + 4096)() == 321, "commit rollback retains old function");
        }
        check(VM::CommitDoubleMappedMemory(p, 12288, true) == p, "retry commit after rollback");
        failOwner = 2;
        check(!VM::GetRWMapping(mapper, p, 0, 12288), "second writable-object mapping failure");
        // All temporary view maps must have been removed; a fresh request succeeds.
        writer = VM::GetRWMapping(mapper, p, 0, 12288);
        check(writer != nullptr, "retry view after rollback");
        check(VM::ReleaseRWMapping(writer, 12288), "release recovered view");
        check(VM::ReleaseDoubleMappedMemory(mapper, p, 0, 16384), "retire all failure-test ownership");
        auto shared = static_cast<unsigned char*>(VM::ReserveDoubleMappedMemory(mapper, 262144, 4096, nullptr, nullptr));
        check(shared && VM::CommitDoubleMappedMemory(shared, 4096, true) == shared, "shared function commitment");
        writer = VM::GetRWMapping(mapper, shared, 262144, 4096);
        check(writer != nullptr, "shared function writer"); emit(writer, 2748);
        check(DBG_FlushInstructionCache(shared, 8), "publish shared function with cached writer");
        sharedFunction = reinterpret_cast<Function>(shared);
        Work work[4]; pthread_t threads[4];
        for (unsigned i = 0; i < 4; ++i) {
            work[i] = {mapper, i};
            check(pthread_create(&threads[i], nullptr, worker, &work[i]) == 0, "create worker");
        }
        for (auto& t : threads) check(pthread_join(t, nullptr) == 0, "join worker");
        check(VM::ReleaseRWMapping(writer, 4096), "retire shared writer after joins");
        check(VM::ReleaseDoubleMappedMemory(mapper, shared, 262144, 4096), "retire shared function after joins");
        // Closing the mapper defers retirement while a caller still owns data.
        p = static_cast<unsigned char*>(VM::ReserveDoubleMappedMemory(mapper, 0, 4096, nullptr, nullptr));
        check(p && VM::CommitDoubleMappedMemory(p, 4096, false) == p, "live allocation at mapper close");
        p[0] = 42; VM::DestroyDoubleMemoryMapper(mapper);
        check(!VM::ReserveDoubleMappedMemory(mapper, 4096, 4096, nullptr, nullptr), "closed mapper rejects new reservations");
        check(p[0] == 42, "mapper close preserves live allocation");
        check(VM::ReleaseDoubleMappedMemory(mapper, p, 0, 4096), "last release retires closed mapper");
        fprintf(output, "ROUND %u checks=%u native_used=%zu\n", round, checks.load(), mallinfo().uordblks);
    }
    fprintf(output, "PASS checks=%u lifetimes=2048 threads=32\n", checks.load());
    fclose(output); return 0;
}
