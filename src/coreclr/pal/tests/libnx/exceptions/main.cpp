#include "pal/thread.hpp"
#include "pal/seh.hpp"
#include "pal/threadnative.h"
#include <atomic>
#include <malloc.h>
extern "C" {
#include <switch/kernel/svc.h>
unsigned __nx_applet_exit_mode = 1;
void ProbeFault(uint64_t* output);
extern char ProbeFaultInstruction[], ProbeFaultContinuation[];
}
static FILE* output;
static std::atomic<unsigned> checks{0}, faults{0};
static thread_local unsigned depth;
static void check(bool yes, const char* name) {
    unsigned n = ++checks;
    if (!yes) { fprintf(output, "FAIL %s check=%u\n", name, n); abort(); }
}
static BOOL safe(CONTEXT* context, EXCEPTION_RECORD*) {
    return context->Pc == reinterpret_cast<uintptr_t>(ProbeFaultInstruction);
}
static BOOL handler(PAL_SEHException* exception) {
    auto context = exception->GetContextRecord();
    auto record = exception->GetExceptionRecord();
    ++faults;
    check(!exception->RecordsOnStack, "real PAL exception record promotion");
    check(record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters == 2 &&
        record->ExceptionInformation[0] == 0 && record->ExceptionInformation[1] == 0, "EL0 read fault translated");
    check(context->X16 == 0x1616 && context->X17 == 0x1717, "full captured scratch registers");
    void *low, *high;
    check(NativeGetCurrentStackBounds(&low, &high), "dispatch executes on original thread stack");
    if (depth == 0) {
        depth = 1;
        uint64_t nested[5]{};
        ProbeFault(nested);
        check(nested[0] == 42 && nested[1] == 0x1616 && nested[2] == 0x1717, "nested exception restores outer dispatch stack");
        depth = 0;
    }
    CONTEXT walk{};
    RtlCaptureContext(&walk);
    bool reached = false;
    for (unsigned i=0; i<16; ++i) {
        if (walk.Pc == reinterpret_cast<uintptr_t>(ProbeFaultInstruction)) { reached = true; break; }
        if (!PAL_VirtualUnwind(&walk, nullptr)) break;
    }
    check(reached && walk.Sp == context->Sp, "existing PAL unwind transition reaches interrupted context");
    context->X0 = 42;
    context->Pc = reinterpret_cast<uintptr_t>(ProbeFaultContinuation);
    return TRUE;
}
static void* worker(void*) {
    check(SEHEnable(nullptr) == NO_ERROR, "enable actual PAL thread exceptions");
    for (unsigned i=0; i<64; ++i) {
        uint64_t result[5]{};
        ProbeFault(result);
        check(result[0] == 42, "resume handler-modified integer value");
        check(result[1] == 0x1616 && result[2] == 0x1717, "kernel return preserves X16 and X17");
        check(result[3] == 0x3030 && result[4] == 0x3131, "kernel return preserves volatile SIMD values");
    }
    check(SEHDisable(nullptr) == NO_ERROR, "retire PAL exception state and registration");
    return nullptr;
}
int main() {
    output = fopen("sdmc:/switch/coreclr-exception-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN real PAL SEH dispatch and full-context restoration; no managed runtime\n");
    // Initialize the real PAL TLS key, leaving its value null: this probe uses
    // PAL hardware dispatch without constructing full runtime thread objects.
    check(pthread_key_create(&CorUnix::thObjKey, nullptr) == 0, "initialize PAL TLS key for native boundary test");
    PAL_SetHardwareExceptionHandler(handler, safe);
    for (unsigned round=0; round<8; ++round) {
        pthread_t threads[4];
        for (auto& thread: threads) check(pthread_create(&thread, nullptr, worker, nullptr) == 0, "create fault worker");
        for (auto& thread: threads) check(pthread_join(thread, nullptr) == 0, "join fault worker");
        fprintf(output, "ROUND %u checks=%u faults=%u native_used=%zu\n", round, checks.load(), faults.load(), mallinfo().uordblks);
    }
    check(pthread_key_delete(CorUnix::thObjKey) == 0, "release native test PAL key");
    fprintf(output, "PASS checks=%u faults=%u threads=32\n", checks.load(), faults.load());
    fclose(output); return 0;
}
