#include "pal.h"
#include "pal/context.h"

extern "C" { unsigned __nx_applet_exit_mode = 1; }
static FILE* output;
static unsigned checks;
static void check(bool value, const char* name)
{
    ++checks;
    if (!value) { fprintf(output, "FAIL %s check=%u\n", name, checks); abort(); }
}
static bool bytesEqual(const void* a, const void* b, size_t size)
{
    return memcmp(a, b, size) == 0;
}
int main()
{
    output = fopen("sdmc:/switch/coreclr-context-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN CoreCLR native-context conversion; no runtime execution\n");
    ThreadContext input{};
    for (unsigned i = 0; i < 29; ++i) input.cpu_gprs[i].x = 0x123456789abcdef0ull + i;
    input.fp = 0x100001; input.lr = 0x200002; input.sp = 0x300003;
    input.pc.x = 0x400004; input.psr = 0x60000000;
    for (unsigned i = 0; i < sizeof(input.fpu_gprs); ++i)
        reinterpret_cast<unsigned char*>(input.fpu_gprs)[i] = static_cast<unsigned char>(i ^ (i >> 8));
    input.fpcr = 0x02000000; input.fpsr = 0x08000000;
    input.tpidr = 0xabcdef01;
    const unsigned groups[] = {CONTEXT_CONTROL, CONTEXT_INTEGER, CONTEXT_FLOATING_POINT};
    // Every subset, plus unsupported register requests and context metadata.
    for (unsigned subset = 0; subset < 8; ++subset)
    {
        unsigned flags = CONTEXT_ARM64;
        for (unsigned i = 0; i < 3; ++i) if (subset & (1u << i)) flags |= groups[i];
        CONTEXT managed; memset(&managed, 0xa5, sizeof(managed));
        CONTEXT sentinel = managed;
        CONTEXTFromNativeContext(&input, &managed, flags | CONTEXT_DEBUG_REGISTERS | CONTEXT_EXCEPTION_ACTIVE);
        check(managed.ContextFlags == (flags | CONTEXT_EXCEPTION_ACTIVE), "only available register groups advertised");
        check(managed.Pc == ((subset & 1) ? input.pc.x : sentinel.Pc), "PC selective copy");
        check(managed.Sp == ((subset & 1) ? input.sp : sentinel.Sp), "SP selective copy");
        check(managed.Fp == ((subset & 1) ? input.fp : sentinel.Fp), "FP selective copy");
        check(managed.Lr == ((subset & 1) ? input.lr : sentinel.Lr), "LR selective copy");
        check(managed.Cpsr == ((subset & 1) ? input.psr : sentinel.Cpsr), "PSTATE selective copy");
        for (unsigned i = 0; i < 29; ++i)
            check(managed.X[i] == ((subset & 2) ? input.cpu_gprs[i].x : sentinel.X[i]), "all integer registers");
        check(bytesEqual(managed.V, (subset & 4) ? static_cast<const void*>(input.fpu_gprs) : sentinel.V, sizeof(managed.V)), "all 128-bit SIMD values");
        check(managed.Fpcr == ((subset & 4) ? input.fpcr : sentinel.Fpcr), "FPCR selective copy");
        check(managed.Fpsr == ((subset & 4) ? input.fpsr : sentinel.Fpsr), "FPSR selective copy");
        check(bytesEqual(managed.Bvr, sentinel.Bvr, sizeof(managed.Bvr)), "debug storage untouched");

        ThreadContext returned; memset(&returned, 0x5a, sizeof(returned));
        ThreadContext expected; memcpy(&expected, &returned, sizeof(expected));
        if (subset & 1) {
            expected.fp = input.fp; expected.lr = input.lr; expected.sp = input.sp;
            expected.pc = input.pc; expected.psr = input.psr;
        }
        if (subset & 2) memcpy(expected.cpu_gprs, input.cpu_gprs, sizeof(input.cpu_gprs));
        if (subset & 4) {
            memcpy(expected.fpu_gprs, input.fpu_gprs, sizeof(input.fpu_gprs));
            expected.fpcr = input.fpcr; expected.fpsr = input.fpsr;
        }
        CONTEXTToNativeContext(&managed, &returned);
        check(bytesEqual(&returned, &expected, sizeof(returned)), "roundtrip, unrequested storage and padding");
        check(returned.tpidr == 0x5a5a5a5a5a5a5a5aull, "libnx TLS preserved");
    }
    check(reinterpret_cast<uintptr_t>(GetNativeContextPC(&input)) == input.pc.x, "native PC accessor");
    check(reinterpret_cast<uintptr_t>(GetNativeContextSP(&input)) == input.sp, "native SP accessor");
    fprintf(output, "PASS checks=%u subsets=8\n", checks);
    fclose(output);
    return 0;
}
