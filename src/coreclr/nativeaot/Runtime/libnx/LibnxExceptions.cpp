#include "common.h"
#include "PalRedhawk.h"
#include "UnixContext.h"
#include "LibnxExceptions.h"
#include "LibnxPlatform.h"
#include <cstdlib>

extern "C" {
__thread LibnxExceptionState* tls_LibnxExceptionState;
}
static PHARDWARE_EXCEPTION_HANDLER hardwareHandler;
static_assert(sizeof(ThreadContext) == NXEX_CONTEXT_SIZE, "Update exception assembly layout");
static_assert(offsetof(ThreadContext, fp) == 232);
static_assert(offsetof(ThreadContext, lr) == 240);
static_assert(offsetof(ThreadContext, sp) == 248);
static_assert(offsetof(ThreadContext, pc) == 256);
static_assert(offsetof(ThreadContext, psr) == 264);
static_assert(offsetof(ThreadContext, fpu_gprs) == 272);
static_assert(offsetof(ThreadContext, fpcr) == 784);
static_assert(offsetof(ThreadContext, fpsr) == 788);
static_assert(offsetof(ThreadContext, tpidr) == 792);
static_assert(offsetof(LibnxExceptionState, far) == NXEX_FAR);
static_assert(offsetof(LibnxExceptionState, esr) == NXEX_ESR);
static_assert(offsetof(LibnxExceptionState, description) == NXEX_DESC);
static_assert(offsetof(LibnxExceptionState, stackLow) == NXEX_STACK_LOW);
static_assert(offsetof(LibnxExceptionState, stackHigh) == NXEX_STACK_HIGH);
static_assert(offsetof(LibnxExceptionState, stackTop) == NXEX_STACK_TOP);
static_assert(offsetof(LibnxExceptionState, active) == NXEX_ACTIVE);
static_assert(offsetof(LibnxExceptionState, stack) == NXEX_STACK);

bool InitializeHardwareExceptionHandling()
{
    // libnx crt0 routes user exceptions to our strong assembly entry symbol.
    // No POSIX signal installation or global exception stack is used.
    return true;
}
extern "C" void PalSetHardwareExceptionHandler(PHARDWARE_EXCEPTION_HANDLER handler)
{
    if (!handler || hardwareHandler) abort();
    hardwareHandler = handler;
}
extern "C" bool LibnxInitializeExceptionState()
{
    if (tls_LibnxExceptionState) return false;
    auto state = static_cast<LibnxExceptionState*>(calloc(1, sizeof(LibnxExceptionState)));
    if (!state) return false;
    void* low; void* high;
    if (!LibnxGetCurrentStackBounds(&low, &high)) { free(state); return false; }
    state->stackLow = reinterpret_cast<uintptr_t>(low);
    state->stackHigh = reinterpret_cast<uintptr_t>(high);
    state->stackTop = reinterpret_cast<uintptr_t>(state->stack + sizeof(state->stack));
    tls_LibnxExceptionState = state;
    return true;
}
extern "C" void LibnxDestroyExceptionState()
{
    auto state = tls_LibnxExceptionState;
    if (state && state->active) abort();
    tls_LibnxExceptionState = nullptr;
    free(state);
}
extern "C" void LibnxDispatchHardwareException(LibnxExceptionState* state)
{
    // We have returned from kernel exception mode onto a per-thread stack.
    // The original managed stack is untouched. Do not allocate or enter GC.
    if (state != tls_LibnxExceptionState || state->active != 1 || !hardwareHandler) abort();
    if (LibnxRuntimeHardwareFault)
        LibnxRuntimeHardwareFault(&state->context, state->far, state->esr);
    uint32_t ec = state->esr >> 26;
    if (ec != 0x24) abort(); // Only EL0 data aborts are currently translated.
    uintptr_t code = 0xc0000005u; // STATUS_ACCESS_VIOLATION
    if ((state->far >= state->stackLow - 4096 && state->far < state->stackLow) ||
        state->context.sp < state->stackLow + 1024 || state->context.sp >= state->stackHigh)
        code = 0xc00000fdu; // STATUS_STACK_OVERFLOW: runtime terminates fatally.
    PAL_LIMITED_CONTEXT limited;
    NativeContextToPalContext(&state->context, &limited);
    uintptr_t arg0 = 0, arg1 = 0;
    if (hardwareHandler(code, state->far, &limited, &arg0, &arg1) != -1) abort();
    RedirectNativeContext(&state->context, &limited, arg0, arg1);
    state->active = 0;
    // This restores the hardware-throw calling contract, not arbitrary user
    // continuation: x16/x17 are scratch branch registers. Callee-saved state,
    // original SP/LR, FP/SIMD state and the handler's arguments are restored.
    LibnxRestoreHardwareThrow(&state->context);
}
