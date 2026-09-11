#include "LibnxThreads.h"
#include "LibnxPlatform.h"
#include <switch.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct LibnxThreadRegistration
{
    struct LibnxThreadRegistration* next;
    Handle handle; // Borrowed, never svcCloseHandle'd here.
    unsigned references;
    bool paused;
};
static Mutex registryLock;
static LibnxThreadRegistration* registrations;

LibnxThreadRegistration* LibnxRegisterCurrentThread(void)
{
    Handle handle = LibnxGetCurrentThreadHandle();
    if (handle == INVALID_HANDLE) return NULL;
    // Allocate outside the registry lock. While a target is paused we must
    // never enter malloc, stdio, runtime locks, or arbitrary callbacks.
    LibnxThreadRegistration* fresh = calloc(1, sizeof(*fresh));
    if (!fresh) return NULL;
    mutexLock(&registryLock);
    for (LibnxThreadRegistration* r = registrations; r; r = r->next)
    {
        if (r->handle == handle)
        {
            if (r->references == UINT_MAX) abort();
            ++r->references;
            mutexUnlock(&registryLock);
            free(fresh);
            return r;
        }
    }
    fresh->handle = handle;
    fresh->references = 1;
    fresh->next = registrations;
    registrations = fresh;
    mutexUnlock(&registryLock);
    return fresh;
}

bool LibnxUnregisterThread(LibnxThreadRegistration* registration)
{
    if (!registration) return false;
    bool success = false, destroy = false;
    mutexLock(&registryLock);
    LibnxThreadRegistration** cursor = &registrations;
    while (*cursor && *cursor != registration) cursor = &(*cursor)->next;
    if (*cursor && !registration->paused && registration->handle == LibnxGetCurrentThreadHandle())
    {
        success = true;
        if (--registration->references == 0)
        {
            *cursor = registration->next;
            destroy = true;
        }
    }
    mutexUnlock(&registryLock);
    if (destroy) free(registration);
    return success;
}

bool LibnxPauseRegisteredThread(LibnxThreadRegistration* r, ThreadContext* context)
{
    if (!r || !context) return false;
    mutexLock(&registryLock);
    bool success = false;
    if (!r->paused && r->handle != LibnxGetCurrentThreadHandle() &&
        R_SUCCEEDED(svcSetThreadActivity(r->handle, ThreadActivity_Paused)))
    {
        r->paused = true;
        memset(context, 0, sizeof(*context));
        success = R_SUCCEEDED(svcGetThreadContext3(context, r->handle));
        if (!success)
        {
            // Failure to resume is fatal; retain the paused state until exit.
            if (R_FAILED(svcSetThreadActivity(r->handle, ThreadActivity_Runnable))) abort();
            r->paused = false;
        }
    }
    mutexUnlock(&registryLock);
    return success;
}

bool LibnxResumeRegisteredThread(LibnxThreadRegistration* r)
{
    if (!r) return false;
    mutexLock(&registryLock);
    // Publish GC updates before making this thread runnable again.
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    bool success = r->paused && R_SUCCEEDED(svcSetThreadActivity(r->handle, ThreadActivity_Runnable));
    if (success) r->paused = false;
    mutexUnlock(&registryLock);
    return success;
}

void LibnxFlushProcessWriteBuffers(void)
{
    // Serializes lifecycle and every pause/resume, independently of ThreadStore
    // and GC locks. No path holds this lock while waiting for those locks.
    mutexLock(&registryLock);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    Handle current = LibnxGetCurrentThreadHandle();
    for (LibnxThreadRegistration* r = registrations; r; r = r->next)
    {
        if (r->handle == current || r->paused) continue;
        if (R_FAILED(svcSetThreadActivity(r->handle, ThreadActivity_Paused))) abort();
        // SetActivity waits for the kernel's atomically published switch-away.
        // Resume then publishes this caller's preceding writes through the
        // scheduler synchronization path. See THREAD_ORDERING.md for the exact
        // kernel version and limits; a local fence alone is not this protocol.
        if (R_FAILED(svcSetThreadActivity(r->handle, ThreadActivity_Runnable))) abort();
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    mutexUnlock(&registryLock);
}
