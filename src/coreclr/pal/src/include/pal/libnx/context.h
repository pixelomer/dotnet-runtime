
#ifndef _PAL_LIBNX_CONTEXT_H_
#define _PAL_LIBNX_CONTEXT_H_

#include <switch/arm/thread_context.h>

// This is libnx's actual context format, not a Linux ucontext/signal-frame ABI.
// The caller owns suspension and must supply a valid snapshot. Conversion does
// not suspend, resume, change TLS, or write registers back to a running thread.
namespace CorUnix
{
inline void LibnxContextToNative(const CONTEXT& source, ThreadContext& destination)
{
    if ((source.ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL)
    {
        destination.fp = source.Fp;
        destination.lr = source.Lr;
        destination.sp = source.Sp;
        destination.pc.x = source.Pc;
        destination.psr = source.Cpsr;
    }
    if ((source.ContextFlags & CONTEXT_INTEGER) == CONTEXT_INTEGER)
    {
        for (unsigned i = 0; i < 29; ++i)
            destination.cpu_gprs[i].x = source.X[i];
    }
    if ((source.ContextFlags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT)
    {
        static_assert(sizeof(destination.fpu_gprs) == sizeof(source.V), "SIMD register storage sizes must match");
        memcpy(destination.fpu_gprs, source.V, sizeof(source.V));
        destination.fpcr = source.Fpcr;
        destination.fpsr = source.Fpsr;
    }
    // tpidr belongs to libnx/pthread TLS and is deliberately never changed.
}

inline void LibnxContextFromNative(const ThreadContext& source, CONTEXT& destination, ULONG flags)
{
    // ThreadContext contains no debug registers or SVE state. Retain context
    // metadata flags, but only advertise register groups actually supplied.
    constexpr ULONG areaMask = 0xffff;
    constexpr ULONG supported = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT;
    destination.ContextFlags = (flags & ~areaMask) | (flags & supported & areaMask);
    if ((flags & CONTEXT_CONTROL) == CONTEXT_CONTROL)
    {
        destination.Fp = source.fp;
        destination.Lr = source.lr;
        destination.Sp = source.sp;
        destination.Pc = source.pc.x;
        destination.Cpsr = source.psr;
    }
    if ((flags & CONTEXT_INTEGER) == CONTEXT_INTEGER)
    {
        for (unsigned i = 0; i < 29; ++i)
            destination.X[i] = source.cpu_gprs[i].x;
    }
    if ((flags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT)
    {
        memcpy(destination.V, source.fpu_gprs, sizeof(destination.V));
        destination.Fpcr = source.fpcr;
        destination.Fpsr = source.fpsr;
    }
}
} // namespace CorUnix
#endif
