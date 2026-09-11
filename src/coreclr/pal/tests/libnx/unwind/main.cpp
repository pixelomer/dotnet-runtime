#include "pal.h"
#include "pal/libnx/unwind.h"
#include <atomic>
#include <pthread.h>
#include <malloc.h>

extern "C" { unsigned __nx_applet_exit_mode = 1; }
extern "C" void ProbeUnwindFrame(CONTEXT*, void (*)(CONTEXT*), uint64_t*);
extern "C" void ProbeUnwindLeaf();
extern "C" char __code_start[], __rodata_start[], __eh_frame_start[], __eh_frame_end[];
extern "C" char __eh_frame_hdr_start[], __eh_frame_hdr_end[];
static FILE* output;
static std::atomic<unsigned> checks{0};
static void check(bool value, const char* name)
{
    unsigned count = ++checks;
    if (!value) { fprintf(output, "FAIL %s check=%u\n", name, count); abort(); }
}
static CorUnix::LibnxUnwindSections sections(bool index)
{
    return {reinterpret_cast<uintptr_t>(__code_start), reinterpret_cast<uintptr_t>(__rodata_start),
        reinterpret_cast<uintptr_t>(__eh_frame_start), static_cast<size_t>(__eh_frame_end - __eh_frame_start),
        index ? reinterpret_cast<uintptr_t>(__eh_frame_hdr_start) : 0,
        index ? static_cast<size_t>(__eh_frame_hdr_end - __eh_frame_hdr_start) : 0};
}
static void inspect(CONTEXT* context)
{
    auto original = *context;
    KNONVOLATILE_CONTEXT_POINTERS pointers{};
    check(CorUnix::LibnxUnwindNativeFrame(context, &pointers), "unwind actual inner frame");
    const PDWORD64 homes[] = {pointers.X19, pointers.X20, pointers.X21, pointers.X22,
        pointers.X23, pointers.X24, pointers.X25, pointers.X26, pointers.X27, pointers.X28};
    for (unsigned i = 0; i < 10; ++i)
    {
        check(context->X[i + 19] == 0x1000u + i + 19, "recovered nonvolatile GPR");
        check(reinterpret_cast<uintptr_t>(homes[i]) == original.Sp + i * 8, "GPR home is actual spill");
        check(*homes[i] == context->X[i + 19], "GPR home value");
    }
    const PDWORD64 fpHomes[] = {pointers.D8, pointers.D9, pointers.D10, pointers.D11,
        pointers.D12, pointers.D13, pointers.D14, pointers.D15};
    for (unsigned i = 0; i < 8; ++i)
    {
        check(context->V[i + 8].Low == 0x8000u + i + 8, "recovered D bits");
        check(reinterpret_cast<uintptr_t>(fpHomes[i]) == original.Sp + 96 + i * 8, "D home is actual spill");
    }
    check(context->Sp == original.Sp + 176, "caller SP from CFI");
    check(reinterpret_cast<uintptr_t>(pointers.Fp) == original.Sp + 80, "FP home");
    check(reinterpret_cast<uintptr_t>(pointers.Lr) == original.Sp + 88, "LR home");
    check(context->Pc == *pointers.Lr, "caller PC");
    check((context->ContextFlags & CONTEXT_UNWOUND_TO_CALL) != 0, "return-address context flag");

    // The unindexed path must decode the same saved state without a global cache.
    CONTEXT scan = original;
    KNONVOLATILE_CONTEXT_POINTERS scanHomes{};
    check(CorUnix::LibnxUnwindFrame(&scan, &scanHomes, sections(false)), "EH-frame scan");
    check(memcmp(&scan, context, sizeof(scan)) == 0, "indexed and scanned register results");
    check(memcmp(&scanHomes, &pointers, sizeof(pointers)) == 0, "indexed and scanned home results");

    // Simulate moving-GC updates: the real assembly epilogue must reload these.
    *pointers.X19 = 0x123456789abcdef0ull;
    *pointers.D8 = 0xfedcba9876543210ull;
}
static void* worker(void*)
{
    for (unsigned n = 0; n < 64; ++n)
    {
        CONTEXT context{}; context.ContextFlags = CONTEXT_FULL;
        uint64_t returned[2]{};
        ProbeUnwindFrame(&context, inspect, returned);
        check(returned[0] == 0x123456789abcdef0ull, "updated spill restored into X19");
        check(returned[1] == 0xfedcba9876543210ull, "updated spill restored into D8");
    }
    return nullptr;
}
int main()
{
    output = fopen("sdmc:/switch/coreclr-unwind-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN CoreCLR native DWARF unwind; no managed runtime\n");
    CONTEXT leaf{};
    leaf.ContextFlags = CONTEXT_FULL | CONTEXT_EXCEPTION_ACTIVE;
    leaf.Pc = reinterpret_cast<uintptr_t>(&ProbeUnwindLeaf);
    leaf.Lr = 0x1234; leaf.Sp = 0x5678; leaf.X19 = 0x7777;
    KNONVOLATILE_CONTEXT_POINTERS pointers; memset(&pointers, 0xa5, sizeof(pointers));
    check(CorUnix::LibnxUnwindNativeFrame(&leaf, &pointers), "exact first-instruction context");
    check(leaf.Pc == 0x1234 && leaf.Sp == 0x5678, "leaf return in LR");
    check(leaf.X19 == 0 && pointers.X19 == nullptr, "undefined register has no stale saved home");
    KNONVOLATILE_CONTEXT_POINTERS empty{};
    check(memcmp(&pointers, &empty, sizeof(pointers)) == 0, "unsaved registers have no stack homes");
    CONTEXT bad{}; bad.Pc = 1; bad.Sp = 1;
    CONTEXT before = bad;
    memset(&pointers, 0xa5, sizeof(pointers)); auto beforePointers = pointers;
    check(!CorUnix::LibnxUnwindNativeFrame(&bad, &pointers), "unknown code rejected");
    check(memcmp(&bad, &before, sizeof(bad)) == 0, "failed unwind preserves context");
    check(memcmp(&pointers, &beforePointers, sizeof(pointers)) == 0, "failed unwind preserves home output");
    for (unsigned round = 0; round < 16; ++round)
    {
        pthread_t threads[4];
        for (auto& thread : threads) check(pthread_create(&thread, nullptr, worker, nullptr) == 0, "create worker");
        for (auto& thread : threads) check(pthread_join(thread, nullptr) == 0, "join worker");
        fprintf(output, "ROUND %u checks=%u native_used=%zu\n", round, checks.load(), mallinfo().uordblks);
    }
    fprintf(output, "PASS checks=%u frames=4096 threads=64\n", checks.load());
    fclose(output);
    return 0;
}
