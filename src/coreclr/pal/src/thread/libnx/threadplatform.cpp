#include "pal/threadnative.h"
#include <stdint.h>
extern "C" {
#include <switch/kernel/thread.h>
#include <switch/kernel/svc.h>
#include <switch/runtime/pthread.h>
#include <switch/result.h>
}

bool NativeGetCurrentStackBounds(void** low, void** high)
{
    if (!low || !high) return false;
    *low = *high = nullptr;
    Thread* thread = threadGetSelf();
    if (!thread || !thread->stack_sz) return false;
    uintptr_t base = reinterpret_cast<uintptr_t>(thread->stack_mirror ? thread->stack_mirror : thread->stack_mem);
    if (!base || thread->stack_sz > UINTPTR_MAX - base) return false;
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    if (sp < base || sp >= base + thread->stack_sz) return false;
    *low = reinterpret_cast<void*>(base);
    *high = reinterpret_cast<void*>(base + thread->stack_sz);
    return true;
}

bool NativeSetThreadPriority(pthread_t thread, int relativePriority)
{
    if (relativePriority != -15 && relativePriority != 15 &&
        (relativePriority < -2 || relativePriority > 2)) return false;
    Handle handle = pthreadGetNativeHandle(thread);
    if (handle == INVALID_HANDLE) return false;
    u64 allowed = 0;
    if (R_FAILED(svcGetInfo(&allowed, InfoType_PriorityMask, CUR_PROCESS_HANDLE, 0)) || !allowed) return false;
    // libnx pthread workers start at 0x3b. Horizon numbers decrease as priority
    // increases. Map normal to that baseline and clamp to the actual process
    // capability mask, not a fabricated POSIX scheduling policy/range.
    int desired = 0x3b - relativePriority;
    int selected = -1, distance = 128;
    for (int priority = 0; priority < 64; ++priority) {
        if (!(allowed & (UINT64_C(1) << priority))) continue;
        int delta = priority > desired ? priority - desired : desired - priority;
        if (delta < distance) { selected = priority; distance = delta; }
    }
    return selected >= 0 && R_SUCCEEDED(svcSetThreadPriority(handle, selected));
}
