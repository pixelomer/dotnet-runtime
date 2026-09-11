#include "pal/thread.hpp"
#include "pal/context.h"
#include "pal/seh.hpp"
#include "pal/signal.hpp"
#include "pal/threadnative.h"
#include "exceptions.h"
#include <stddef.h>
#include <stdlib.h>
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/runtime/env.h>
__thread LibnxExceptionState* tls_PalLibnxExceptionState;
void RestoreCompleteContext(CONTEXT*, EXCEPTION_RECORD*);
}
using namespace CorUnix;
static_assert(sizeof(ThreadContext) == NXEX_CONTEXT_SIZE, "Native context ABI");
#define CHECK_OFFSET(type, field, offset) static_assert(offsetof(type, field) == offset, "Exception assembly ABI")
CHECK_OFFSET(ThreadContext, fp, 232);
CHECK_OFFSET(ThreadContext, lr, 240);
CHECK_OFFSET(ThreadContext, sp, 248);
CHECK_OFFSET(ThreadContext, pc, 256);
CHECK_OFFSET(ThreadContext, psr, 264);
CHECK_OFFSET(ThreadContext, fpu_gprs, 272);
CHECK_OFFSET(ThreadContext, fpcr, 784);
CHECK_OFFSET(ThreadContext, fpsr, 788);
CHECK_OFFSET(ThreadContext, tpidr, 792);
CHECK_OFFSET(LibnxExceptionState, far, NXEX_FAR);
CHECK_OFFSET(LibnxExceptionState, esr, NXEX_ESR);
CHECK_OFFSET(LibnxExceptionState, description, NXEX_DESC);
CHECK_OFFSET(LibnxExceptionState, stackTop, NXEX_STACK_TOP);
CHECK_OFFSET(LibnxExceptionState, active, NXEX_ACTIVE);
CHECK_OFFSET(LibnxExceptionState, stack, NXEX_STACK);
#undef CHECK_OFFSET

bool g_registered_signal_handlers;
bool g_enable_alternate_stack_check;
bool PAL_LibnxEnableExceptions()
{
    if (tls_PalLibnxExceptionState) return true;
    if (!envIsSyscallHinted(0x28) || !PAL_LibnxExceptionEntryAddress()) return false;
    void *low, *high;
    if (!NativeGetCurrentStackBounds(&low, &high)) return false;
    auto state = static_cast<LibnxExceptionState*>(calloc(1, sizeof(LibnxExceptionState)));
    if (!state) return false;
    state->stackLow = reinterpret_cast<uintptr_t>(low);
    state->stackHigh = reinterpret_cast<uintptr_t>(high);
    state->stackTop = reinterpret_cast<uintptr_t>(state->stack + sizeof(state->stack));
    state->registration = LibnxRegisterCurrentThread();
    if (!state->registration) { free(state); return false; }
    tls_PalLibnxExceptionState = state;
    return true;
}
void PAL_LibnxDisableExceptions()
{
    auto state = tls_PalLibnxExceptionState;
    if (state && state->active) abort();
    if (state && !LibnxUnregisterThread(state->registration)) abort();
    tls_PalLibnxExceptionState = nullptr;
    free(state);
}
BOOL SEHInitializeSignals(CPalThread*, DWORD)
{
    // libnx crt0 owns the process-wide user-exception entry. Per-thread enable
    // supplies the saved context and emergency stack; there is no sigaction ABI.
    g_registered_signal_handlers = envIsSyscallHinted(0x28) && PAL_LibnxExceptionEntryAddress();
    return g_registered_signal_handlers;
}
void SEHCleanupSignals(bool)
{
    // This statically linked runtime stays resident until process exit. Thread
    // state is retired by SEHDisable; no Unix handlers were installed to restore.
    g_registered_signal_handlers = false;
}
void UnmaskActivationSignal() { /* Horizon has no POSIX activation signal mask. */ }
PAL_ERROR InjectActivationInternal(CPalThread*) { return ERROR_NOT_SUPPORTED; }
void SEHSetSignalToIgnore(int) { SetLastError(ERROR_NOT_SUPPORTED); }

namespace {
struct DispatchFrame {
    CONTEXT context;
    EXCEPTION_RECORD record;
    LibnxExceptionState* state;
    uint64_t tpidr;
};
static_assert(sizeof(DispatchFrame) * 2 < NXEX_STACK_SIZE, "Emergency dispatch capacity");

__attribute__((noinline, noreturn)) void DispatchOnStack(DispatchFrame* frame)
{
    // Keep the existing PAL virtual-unwind transition: this local identifies
    // the interrupted context from SEHProcessException's caller frame.
    CONTEXT* volatile contextRecord = &frame->context;
    g_hardware_exception_context_locvar_offset =
        static_cast<int>(reinterpret_cast<const volatile char*>(&contextRecord) - static_cast<const volatile char*>(__builtin_frame_address(0)));
    auto state = frame->state;
    // Ordinary dispatch runs below the original SP. Nested managed faults then
    // create independent frames instead of overwriting a private scratch stack.
    if (frame->record.ExceptionCode != EXCEPTION_STACK_OVERFLOW) state->active = 0;
    bool resume;
    {
        PAL_SEHException exception(&frame->record, &frame->context, true);
        resume = SEHProcessException(&exception);
        if (resume) {
            frame->context = *exception.GetContextRecord();
            frame->record = *exception.GetExceptionRecord();
        }
    }
    if (!resume) abort();
    // Records are released before returning to the interrupted context. Request
    // CoreCLR's existing full-register restore trampoline through Horizon.
    CONTEXTToNativeContext(&frame->context, &state->context);
    state->context.tpidr = frame->tpidr;
    state->active = 2;
    RestoreCompleteContext(&frame->context, &frame->record);
    abort();
}
}

void PAL_LibnxBeginException(LibnxExceptionState* state)
{
    // Kernel exception mode has ended. It is now safe to call native code and
    // prepare an ordinary CoreCLR exception-dispatch frame on the original stack.
    if (state != tls_PalLibnxExceptionState || state->active != 1) abort();
    DispatchFrame emergency{};
    unsigned ec = state->esr >> 26;
    DWORD code;
    size_t parameters = 0, access = 0;
    switch (ec) {
        case 0x24: code = EXCEPTION_ACCESS_VIOLATION; parameters = 2; access = (state->esr >> 6) & 1; break;
        case 0x20: code = EXCEPTION_ACCESS_VIOLATION; parameters = 2; access = 8; break;
        case 0x00: code = EXCEPTION_ILLEGAL_INSTRUCTION; break;
        case 0x22: case 0x26: code = EXCEPTION_DATATYPE_MISALIGNMENT; break;
        case 0x3c: code = EXCEPTION_BREAKPOINT; break;
        default: abort();
    }
    constexpr size_t RedZone = 128;
    uintptr_t sp = state->context.sp;
    bool overflow = sp > state->stackHigh || sp < state->stackLow ||
        sp - state->stackLow < sizeof(DispatchFrame) + RedZone + 32 ||
        (ec == 0x24 && state->far < state->stackLow && state->stackLow - state->far <= 4096);
    DispatchFrame* frame = &emergency;
    if (overflow) code = EXCEPTION_STACK_OVERFLOW;
    else frame = reinterpret_cast<DispatchFrame*>((sp - sizeof(DispatchFrame) - RedZone) & ~uintptr_t(15));
    frame->record = {};
    frame->state = state;
    frame->tpidr = state->context.tpidr;
    frame->context = {};
    CONTEXTFromNativeContext(&state->context, &frame->context, CONTEXT_FULL);
    frame->context.ContextFlags |= CONTEXT_EXCEPTION_ACTIVE;
    frame->record.ExceptionCode = code;
    frame->record.ExceptionFlags = EXCEPTION_IS_SIGNAL;
    frame->record.ExceptionAddress = reinterpret_cast<void*>(state->context.pc.x);
    frame->record.NumberParameters = parameters;
    if (parameters) {
        frame->record.ExceptionInformation[0] = access;
        frame->record.ExceptionInformation[1] = state->far;
    }
    if (overflow) DispatchOnStack(frame);
    // A small synthetic caller frame preserves FP linkage for native walkers;
    // PAL's normal exception transition supplies the precise interrupted context.
    auto fp = reinterpret_cast<uintptr_t*>(frame) - 2;
    fp[0] = state->context.fp;
    fp[1] = state->context.pc.x;
    CONTEXT enter = frame->context;
    enter.Sp = reinterpret_cast<uintptr_t>(fp);
    enter.Fp = enter.Sp;
    enter.Lr = 0;
    enter.Pc = reinterpret_cast<uintptr_t>(DispatchOnStack);
    enter.X0 = reinterpret_cast<uintptr_t>(frame);
    RtlRestoreContext(&enter, nullptr);
    abort();
}
