// Horizon context adapter. This deliberately uses libnx's declared layout,
// never a fabricated Linux ucontext_t layout.
#include "common.h"
#include "CommonTypes.h"
#include "CommonMacros.h"
#include "daccess.h"
#include "Pal.h"
#include "NativeContext.h"
#include <cstring>

static_assert(offsetof(NATIVE_CONTEXT, ctx) == 0);
static_assert(sizeof(ThreadContext::cpu_gprs[0].x) == sizeof(uint64_t));

uint64_t& NATIVE_CONTEXT::X0() { return ctx.cpu_gprs[0].x; }
uint64_t& NATIVE_CONTEXT::X1() { return ctx.cpu_gprs[1].x; }
uint64_t& NATIVE_CONTEXT::X2() { return ctx.cpu_gprs[2].x; }
uint64_t& NATIVE_CONTEXT::X3() { return ctx.cpu_gprs[3].x; }
uint64_t& NATIVE_CONTEXT::X4() { return ctx.cpu_gprs[4].x; }
uint64_t& NATIVE_CONTEXT::X5() { return ctx.cpu_gprs[5].x; }
uint64_t& NATIVE_CONTEXT::X6() { return ctx.cpu_gprs[6].x; }
uint64_t& NATIVE_CONTEXT::X7() { return ctx.cpu_gprs[7].x; }
uint64_t& NATIVE_CONTEXT::X8() { return ctx.cpu_gprs[8].x; }
uint64_t& NATIVE_CONTEXT::X9() { return ctx.cpu_gprs[9].x; }
uint64_t& NATIVE_CONTEXT::X10() { return ctx.cpu_gprs[10].x; }
uint64_t& NATIVE_CONTEXT::X11() { return ctx.cpu_gprs[11].x; }
uint64_t& NATIVE_CONTEXT::X12() { return ctx.cpu_gprs[12].x; }
uint64_t& NATIVE_CONTEXT::X13() { return ctx.cpu_gprs[13].x; }
uint64_t& NATIVE_CONTEXT::X14() { return ctx.cpu_gprs[14].x; }
uint64_t& NATIVE_CONTEXT::X15() { return ctx.cpu_gprs[15].x; }
uint64_t& NATIVE_CONTEXT::X16() { return ctx.cpu_gprs[16].x; }
uint64_t& NATIVE_CONTEXT::X17() { return ctx.cpu_gprs[17].x; }
uint64_t& NATIVE_CONTEXT::X18() { return ctx.cpu_gprs[18].x; }
uint64_t& NATIVE_CONTEXT::X19() { return ctx.cpu_gprs[19].x; }
uint64_t& NATIVE_CONTEXT::X20() { return ctx.cpu_gprs[20].x; }
uint64_t& NATIVE_CONTEXT::X21() { return ctx.cpu_gprs[21].x; }
uint64_t& NATIVE_CONTEXT::X22() { return ctx.cpu_gprs[22].x; }
uint64_t& NATIVE_CONTEXT::X23() { return ctx.cpu_gprs[23].x; }
uint64_t& NATIVE_CONTEXT::X24() { return ctx.cpu_gprs[24].x; }
uint64_t& NATIVE_CONTEXT::X25() { return ctx.cpu_gprs[25].x; }
uint64_t& NATIVE_CONTEXT::X26() { return ctx.cpu_gprs[26].x; }
uint64_t& NATIVE_CONTEXT::X27() { return ctx.cpu_gprs[27].x; }
uint64_t& NATIVE_CONTEXT::X28() { return ctx.cpu_gprs[28].x; }
uint64_t& NATIVE_CONTEXT::Fp() { return ctx.fp; }
uint64_t& NATIVE_CONTEXT::Lr() { return ctx.lr; }
uint64_t& NATIVE_CONTEXT::Sp() { return ctx.sp; }
uint64_t& NATIVE_CONTEXT::Pc() { return ctx.pc.x; }

void NativeContextToPalContext(const void* context, PAL_LIMITED_CONTEXT* pal)
{
    const auto& ctx = *static_cast<const ThreadContext*>(context);
    pal->FP = ctx.fp;
    pal->LR = ctx.lr;
    pal->SP = ctx.sp;
    pal->IP = ctx.pc.x;
    pal->X0 = ctx.cpu_gprs[0].x;
    pal->X1 = ctx.cpu_gprs[1].x;
    pal->X19 = ctx.cpu_gprs[19].x;
    pal->X20 = ctx.cpu_gprs[20].x;
    pal->X21 = ctx.cpu_gprs[21].x;
    pal->X22 = ctx.cpu_gprs[22].x;
    pal->X23 = ctx.cpu_gprs[23].x;
    pal->X24 = ctx.cpu_gprs[24].x;
    pal->X25 = ctx.cpu_gprs[25].x;
    pal->X26 = ctx.cpu_gprs[26].x;
    pal->X27 = ctx.cpu_gprs[27].x;
    pal->X28 = ctx.cpu_gprs[28].x;
    for (unsigned i = 0; i < 8; ++i)
        memcpy(&pal->D[i], &ctx.fpu_gprs[i + 8], sizeof(pal->D[i]));
}

void RedirectNativeContext(void* context, const PAL_LIMITED_CONTEXT* pal,
                           uintptr_t arg0Reg, uintptr_t arg1Reg)
{
    auto& ctx = *static_cast<ThreadContext*>(context);
    // Match the Unix adapter: redirect control state and the two arguments.
    // Other registers retain their interrupted values, including SIMD and TLS.
    ctx.fp = pal->FP;
    ctx.lr = pal->LR;
    ctx.sp = pal->SP;
    ctx.pc.x = pal->IP;
    ctx.cpu_gprs[0].x = arg0Reg;
    ctx.cpu_gprs[1].x = arg1Reg;
}
