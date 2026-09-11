// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

//
// Implementation of the Redhawk Platform Abstraction Layer (PAL) library for Horizon using devkitPro newlib/libnx.
// Derived from PalRedhawkUnix.cpp; no glibc ABI shims are used.
//

#include <stdio.h>
#include <errno.h>
#include <cwchar>
#include <sal.h>
#include "config.h"
#include "UnixHandle.h"
#include <pthread.h>
#include "gcenv.h"
#include "gcenv.ee.h"
#include "gcconfig.h"
#include "holder.h"
#include "UnixContext.h"
#include "HardwareExceptions.h"
#include "threadstore.h"
#include "thread.h"
#include "threadstore.inl"

#define _T(s) s
#include "RhConfig.h"

#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <cstdarg>
#include <signal.h>

#if HAVE_PTHREAD_GETTHREADID_NP
#include <pthread_np.h>
#endif

#if HAVE_LWP_SELF
#include <lwp.h>
#endif

#if HAVE_CLOCK_GETTIME_NSEC_NP
#include <time.h>
#endif

#ifdef TARGET_APPLE
#include <mach/mach.h>
#endif

#include "LibnxPlatform.h"
#include "LibnxThreads.h"
extern "C" {
#include "nxvm.h"
}
using std::nullptr_t;

#define PalRaiseFailFastException RaiseFailFastException

#define INVALID_HANDLE_VALUE    ((HANDLE)(intptr_t)-1)

#define PAGE_NOACCESS           0x01
#define PAGE_READWRITE          0x04
#define PAGE_EXECUTE_READ       0x20
#define PAGE_EXECUTE_READWRITE  0x40

#define WAIT_OBJECT_0           0
#define WAIT_TIMEOUT            258
#define WAIT_FAILED             0xFFFFFFFF

static const int tccSecondsToMilliSeconds = 1000;
static const int tccSecondsToMicroSeconds = 1000000;
static const int tccSecondsToNanoSeconds = 1000000000;
static const int tccMilliSecondsToMicroSeconds = 1000;
static const int tccMilliSecondsToNanoSeconds = 1000000;
static const int tccMicroSecondsToNanoSeconds = 1000;

extern "C" void RaiseFailFastException(PEXCEPTION_RECORD arg1, PCONTEXT arg2, uint32_t arg3)
{
    // Subprocess crash dumps are unavailable. Preserve fatal termination.

    // Aborts the process
    abort();
}

static void TimeSpecAdd(timespec* time, uint32_t milliseconds)
{
    uint64_t nsec = time->tv_nsec + (uint64_t)milliseconds * tccMilliSecondsToNanoSeconds;
    if (nsec >= tccSecondsToNanoSeconds)
    {
        time->tv_sec += nsec / tccSecondsToNanoSeconds;
        nsec %= tccSecondsToNanoSeconds;
    }

    time->tv_nsec = nsec;
}

// Convert nanoseconds to the timespec structure
// Parameters:
//  nanoseconds - time in nanoseconds to convert
//  t           - the target timespec structure
static void NanosecondsToTimeSpec(uint64_t nanoseconds, timespec* t)
{
    t->tv_sec = nanoseconds / tccSecondsToNanoSeconds;
    t->tv_nsec = nanoseconds % tccSecondsToNanoSeconds;
}

void ReleaseCondAttr(pthread_condattr_t* condAttr)
{
    int st = pthread_condattr_destroy(condAttr);
    ASSERT_MSG(st == 0, "Failed to destroy pthread_condattr_t object");
}

class PthreadCondAttrHolder : public Wrapper<pthread_condattr_t*, DoNothing, ReleaseCondAttr, nullptr>
{
public:
    PthreadCondAttrHolder(pthread_condattr_t* attrs)
    : Wrapper<pthread_condattr_t*, DoNothing, ReleaseCondAttr, nullptr>(attrs)
    {
    }
};

class UnixEvent
{
    pthread_cond_t m_condition;
    pthread_mutex_t m_mutex;
    bool m_manualReset;
    bool m_state;
    bool m_isValid;

public:

    UnixEvent(bool manualReset, bool initialState)
    : m_manualReset(manualReset),
      m_state(initialState),
      m_isValid(false)
    {
    }

    bool Initialize()
    {
        pthread_condattr_t attrs;
        int st = pthread_condattr_init(&attrs);
        if (st != 0)
        {
            ASSERT_UNCONDITIONALLY("Failed to initialize UnixEvent condition attribute");
            return false;
        }

        PthreadCondAttrHolder attrsHolder(&attrs);

#if HAVE_PTHREAD_CONDATTR_SETCLOCK && !HAVE_CLOCK_GETTIME_NSEC_NP
        // Ensure that the pthread_cond_timedwait will use CLOCK_MONOTONIC
        st = pthread_condattr_setclock(&attrs, CLOCK_MONOTONIC);
        if (st != 0)
        {
            ASSERT_UNCONDITIONALLY("Failed to set UnixEvent condition variable wait clock");
            return false;
        }
#endif // HAVE_PTHREAD_CONDATTR_SETCLOCK && !HAVE_CLOCK_GETTIME_NSEC_NP

        st = pthread_mutex_init(&m_mutex, NULL);
        if (st != 0)
        {
            ASSERT_UNCONDITIONALLY("Failed to initialize UnixEvent mutex");
            return false;
        }

        st = pthread_cond_init(&m_condition, &attrs);
        if (st != 0)
        {
            ASSERT_UNCONDITIONALLY("Failed to initialize UnixEvent condition variable");

            st = pthread_mutex_destroy(&m_mutex);
            ASSERT_MSG(st == 0, "Failed to destroy UnixEvent mutex");
            return false;
        }

        m_isValid = true;

        return true;
    }

    bool Destroy()
    {
        bool success = true;

        if (m_isValid)
        {
            int st = pthread_mutex_destroy(&m_mutex);
            ASSERT_MSG(st == 0, "Failed to destroy UnixEvent mutex");
            success = success && (st == 0);

            st = pthread_cond_destroy(&m_condition);
            ASSERT_MSG(st == 0, "Failed to destroy UnixEvent condition variable");
            success = success && (st == 0);
        }

        return success;
    }

    uint32_t Wait(uint32_t milliseconds)
    {
        timespec endTime;
#if HAVE_CLOCK_GETTIME_NSEC_NP
        uint64_t endNanoseconds;
        if (milliseconds != INFINITE)
        {
            uint64_t nanoseconds = (uint64_t)milliseconds * tccMilliSecondsToNanoSeconds;
            NanosecondsToTimeSpec(nanoseconds, &endTime);
            endNanoseconds = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + nanoseconds;
        }
#elif HAVE_PTHREAD_CONDATTR_SETCLOCK
        if (milliseconds != INFINITE)
        {
            clock_gettime(CLOCK_MONOTONIC, &endTime);
            TimeSpecAdd(&endTime, milliseconds);
        }
#else
#error "Don't know how to perform timed wait on this platform"
#endif

        int st = 0;

        pthread_mutex_lock(&m_mutex);
        while (!m_state)
        {
            if (milliseconds == INFINITE)
            {
                st = pthread_cond_wait(&m_condition, &m_mutex);
            }
            else
            {
#if HAVE_CLOCK_GETTIME_NSEC_NP
                // Since OSX doesn't support CLOCK_MONOTONIC, we use relative variant of the
                // timed wait and we need to handle spurious wakeups properly.
                st = pthread_cond_timedwait_relative_np(&m_condition, &m_mutex, &endTime);
                if ((st == 0) && !m_state)
                {
                    uint64_t currentNanoseconds = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                    if (currentNanoseconds < endNanoseconds)
                    {
                        // The wake up was spurious, recalculate the relative endTime
                        uint64_t remainingNanoseconds = (endNanoseconds - currentNanoseconds);
                        NanosecondsToTimeSpec(remainingNanoseconds, &endTime);
                    }
                    else
                    {
                        // Although the timed wait didn't report a timeout, time calculated from the
                        // mach time shows we have already reached the end time. It can happen if
                        // the wait was spuriously woken up right before the timeout.
                        st = ETIMEDOUT;
                    }
                }
#else // HAVE_CLOCK_GETTIME_NSEC_NP
                st = pthread_cond_timedwait(&m_condition, &m_mutex, &endTime);
#endif // HAVE_CLOCK_GETTIME_NSEC_NP
            }

            if (st != 0)
            {
                // wait failed or timed out
                break;
            }
        }

        if ((st == 0) && !m_manualReset)
        {
            // Clear the state for auto-reset events so that only one waiter gets released
            m_state = false;
        }

        pthread_mutex_unlock(&m_mutex);

        uint32_t waitStatus;

        if (st == 0)
        {
            waitStatus = WAIT_OBJECT_0;
        }
        else if (st == ETIMEDOUT)
        {
            waitStatus = WAIT_TIMEOUT;
        }
        else
        {
            waitStatus = WAIT_FAILED;
        }

        return waitStatus;
    }

    void Set()
    {
        pthread_mutex_lock(&m_mutex);
        m_state = true;
        // Unblock all threads waiting for the condition variable
        pthread_cond_broadcast(&m_condition);
        pthread_mutex_unlock(&m_mutex);
    }

    void Reset()
    {
        pthread_mutex_lock(&m_mutex);
        m_state = false;
        pthread_mutex_unlock(&m_mutex);
    }
};

// Construct synchronization objects in their final storage: copying initialized
// pthread mutex/condition objects is not a supported POSIX operation.
class EventUnixHandle : public UnixHandleBase
{
    UnixEvent m_event;
public:
    EventUnixHandle(bool manualReset, bool initialState)
        : UnixHandleBase(UnixHandleType::Event), m_event(manualReset, initialState) {}
    UnixEvent* GetObject() { return &m_event; }
    bool Initialize() { return m_event.Initialize(); }
    virtual bool Destroy() { return m_event.Destroy(); }
};

// This wrapper borrows libnx's actual handle. The owning pthread must remain
// alive until RuntimeThreadShutdown detaches it; closing this wrapper must not
// close the libnx-owned kernel handle.
class ThreadUnixHandle : public UnixHandleBase
{
    LibnxThreadRegistration* registration;
public:
    ThreadUnixHandle(LibnxThreadRegistration* value)
        : UnixHandleBase(UnixHandleType::Thread), registration(value) {}
    LibnxThreadRegistration* GetRegistration() { return registration; }
    virtual bool Destroy() { return LibnxUnregisterThread(registration); }
};

extern "C" bool LibnxPausePalThread(void* handle, ThreadContext* context)
{
    auto thread = static_cast<ThreadUnixHandle*>(handle);
    return thread && thread->GetType() == UnixHandleType::Thread &&
        LibnxPauseRegisteredThread(thread->GetRegistration(), context);
}
extern "C" bool LibnxResumePalThread(void* handle)
{
    auto thread = static_cast<ThreadUnixHandle*>(handle);
    return thread && thread->GetType() == UnixHandleType::Thread &&
        LibnxResumeRegisteredThread(thread->GetRegistration());
}

void InitializeCurrentProcessCpuCount()
{
    uint32_t count;

    // If the configuration value has been set, it takes precedence. Otherwise, take into account
    // process affinity and CPU quota limit.

    const unsigned int MAX_PROCESSOR_COUNT = 0xffff;
    uint64_t configValue;

    if (g_pRhConfig->ReadConfigValue("PROCESSOR_COUNT", &configValue, true /* decimal */) &&
        0 < configValue && configValue <= MAX_PROCESSOR_COUNT)
    {
        count = configValue;
    }
    else
    {
#if HAVE_SCHED_GETAFFINITY

        cpu_set_t cpuSet;
        int st = sched_getaffinity(getpid(), sizeof(cpu_set_t), &cpuSet);
        if (st != 0)
        {
            _ASSERTE(!"sched_getaffinity failed");
        }

        count = CPU_COUNT(&cpuSet);
#else // HAVE_SCHED_GETAFFINITY
        count = GCToOSInterface::GetTotalProcessorCount();
#endif // HAVE_SCHED_GETAFFINITY

    }

    _ASSERTE(count > 0);
    g_RhNumberOfProcessors = count;
}

static uint32_t g_RhPageSize;

void InitializeOsPageSize()
{
    g_RhPageSize = 4096;

#if defined(HOST_AMD64)
    ASSERT(g_RhPageSize == 0x1000);
#elif defined(HOST_APPLE)
    ASSERT(g_RhPageSize == 0x4000);
#endif
}

REDHAWK_PALEXPORT uint32_t REDHAWK_PALAPI PalGetOsPageSize()
{
    return g_RhPageSize;
}

#if defined(TARGET_LIBNX)
static pthread_key_t key;
static __thread void* threadInDestructor;
static void ThreadKeyDestructor(void* thread)
{
    // pthread has already cleared the key. Retain the identity separately so
    // PalDetachThread can authorize exactly this teardown, once.
    if (threadInDestructor != nullptr) RhFailFast();
    threadInDestructor = thread;
    RuntimeThreadShutdown(thread);
    threadInDestructor = nullptr;
}
#endif

// The Redhawk PAL must be initialized before any of its exports can be called. Returns true for a successful
// initialization and false on failure.
REDHAWK_PALEXPORT bool REDHAWK_PALAPI PalInit()
{
#ifndef USE_PORTABLE_HELPERS
    if (!InitializeHardwareExceptionHandling())
    {
        return false;
    }
#endif // !USE_PORTABLE_HELPERS

    GCConfig::Initialize();

    if (!GCToOSInterface::Initialize())
    {
        return false;
    }


    InitializeCurrentProcessCpuCount();

    InitializeOsPageSize();

#if defined(TARGET_LIBNX)
    if (pthread_key_create(&key, ThreadKeyDestructor) != 0)
    {
        GCToOSInterface::Shutdown();
        return false;
    }
#endif

    return true;
}

// This thread local variable is used for delegate marshalling
DECLSPEC_THREAD intptr_t tls_thunkData;

#ifdef FEATURE_EMULATED_TLS
EXTERN_C intptr_t* RhpGetThunkData()
{
    return &tls_thunkData;
}
#endif //FEATURE_EMULATED_TLS

FCIMPL0(intptr_t, RhGetCurrentThunkContext)
{
    return tls_thunkData;
}
FCIMPLEND

// Register the thread with OS to be notified when thread is about to be destroyed
// It fails fast if a different thread was already registered.
// Parameters:
//  thread        - thread to attach
extern "C" void PalAttachThread(void* thread)
{
    if (thread == nullptr || pthread_getspecific(key) != nullptr || pthread_setspecific(key, thread) != 0)
        RhFailFast();
}

extern "C" bool PalDetachThread(void* thread)
{
    // POSIX clears the key before invoking its destructor, so no value is also
    // expected when RuntimeThreadShutdown was entered by pthread teardown.
    void* current = pthread_getspecific(key);
    if (current == nullptr)
    {
        if (thread == nullptr || threadInDestructor != thread) return false;
        threadInDestructor = nullptr;
        return true;
    }
    if (current != thread || pthread_setspecific(key, nullptr) != 0) RhFailFast();
    return true;
}

#if !defined(USE_PORTABLE_HELPERS) && !defined(FEATURE_RX_THUNKS)
REDHAWK_PALEXPORT UInt32_BOOL REDHAWK_PALAPI PalAllocateThunksFromTemplate(HANDLE, uint32_t, size_t, void** newThunksOut)
{
    *newThunksOut = nullptr;
    return UInt32_FALSE; // Executable aliases require a separate code allocator.
}
REDHAWK_PALEXPORT UInt32_BOOL REDHAWK_PALAPI PalFreeThunksFromTemplate(void*, size_t)
{
    return UInt32_FALSE;
}
#endif

REDHAWK_PALEXPORT UInt32_BOOL REDHAWK_PALAPI PalMarkThunksAsValidCallTargets(void*, int, int, int, int)
{
    return UInt32_FALSE; // No unvalidated executable thunk mappings.
}

REDHAWK_PALEXPORT void REDHAWK_PALAPI PalSleep(uint32_t milliseconds)
{
    GCToOSInterface::Sleep(milliseconds);
}
REDHAWK_PALEXPORT UInt32_BOOL REDHAWK_PALAPI __stdcall PalSwitchToThread()
{
    GCToOSInterface::YieldThread(0);
    return UInt32_FALSE; // Yield success does not indicate a context switch.
}

REDHAWK_PALEXPORT UInt32_BOOL REDHAWK_PALAPI PalAreShadowStacksEnabled()
{
    return false;
}

extern "C" UInt32_BOOL CloseHandle(HANDLE handle)
{
    if ((handle == NULL) || (handle == INVALID_HANDLE_VALUE))
    {
        return UInt32_FALSE;
    }

    UnixHandleBase* handleBase = (UnixHandleBase*)handle;

    bool success = handleBase->Destroy();

    delete handleBase;

    return success ? UInt32_TRUE : UInt32_FALSE;
}

REDHAWK_PALEXPORT HANDLE REDHAWK_PALAPI PalCreateEventW(_In_opt_ LPSECURITY_ATTRIBUTES pEventAttributes, UInt32_BOOL manualReset, UInt32_BOOL initialState, _In_opt_z_ const WCHAR* pName)
{
    EventUnixHandle* handle = new (nothrow) EventUnixHandle(manualReset, initialState);
    if (handle == nullptr) return INVALID_HANDLE_VALUE;
    if (!handle->Initialize())
    {
        delete handle;
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

typedef uint32_t(__stdcall *BackgroundCallback)(_In_opt_ void* pCallbackContext);

struct BackgroundWork
{
    BackgroundCallback callback;
    void* context;
};
static void* BackgroundWorkEntry(void* state)
{
    BackgroundWork work = *static_cast<BackgroundWork*>(state);
    delete static_cast<BackgroundWork*>(state);
    work.callback(work.context);
    return nullptr;
}
REDHAWK_PALEXPORT bool REDHAWK_PALAPI PalStartBackgroundWork(BackgroundCallback callback, void* context, UInt32_BOOL highPriority)
{
    // Scheduling policy tuning is pending. Preserve the pthread default priority.
    BackgroundWork* work = new (nothrow) BackgroundWork{callback, context};
    if (work == nullptr) return false;
    pthread_attr_t attrs;
    if (pthread_attr_init(&attrs) != 0) { delete work; return false; }
    int st = pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    if (st == 0) st = pthread_create(&thread, &attrs, BackgroundWorkEntry, work);
    pthread_attr_destroy(&attrs);
    if (st != 0) delete work;
    return st == 0;
}

REDHAWK_PALEXPORT bool REDHAWK_PALAPI PalStartBackgroundGCThread(_In_ BackgroundCallback callback, _In_opt_ void* pCallbackContext)
{
    return PalStartBackgroundWork(callback, pCallbackContext, UInt32_FALSE);
}

REDHAWK_PALEXPORT bool REDHAWK_PALAPI PalStartFinalizerThread(_In_ BackgroundCallback callback, _In_opt_ void* pCallbackContext)
{
    return PalStartBackgroundWork(callback, pCallbackContext, UInt32_TRUE);
}

REDHAWK_PALEXPORT bool REDHAWK_PALAPI PalStartEventPipeHelperThread(_In_ BackgroundCallback callback, _In_opt_ void* pCallbackContext)
{
    return PalStartBackgroundWork(callback, pCallbackContext, UInt32_FALSE);
}

// Returns a 64-bit tick count with a millisecond resolution. It tries its best
// to return monotonically increasing counts and avoid being affected by changes
// to the system clock (either due to drift or due to explicit changes to system
// time).
REDHAWK_PALEXPORT uint64_t REDHAWK_PALAPI PalGetTickCount64()
{
    return GCToOSInterface::GetLowPrecisionTimeStamp();
}

REDHAWK_PALEXPORT HANDLE REDHAWK_PALAPI PalGetModuleHandleFromPointer(_In_ void* pointer)
{
    return LibnxGetModuleBase(pointer);
}

REDHAWK_PALEXPORT void PalPrintFatalError(const char* message)
{
    // Write the message using lowest-level OS API available. This is used to print the stack overflow
    // message, so there is not much that can be done here.
    // write() has __attribute__((warn_unused_result)) in glibc, for which gcc 11+ issue `-Wunused-result` even with `(void)write(..)`,
    // so we use additional NOT(!) operator to force unused-result suppression.
    (void)!write(STDERR_FILENO, message, strlen(message));
}

REDHAWK_PALEXPORT char* PalCopyTCharAsChar(const TCHAR* toCopy)
{
    NewArrayHolder<char> copy {new (nothrow) char[strlen(toCopy) + 1]};
    if (copy == nullptr) return nullptr;
    strcpy(copy, toCopy);
    return copy.Extract();
}

REDHAWK_PALEXPORT HANDLE PalLoadLibrary(const char*)
{
    errno = ENOTSUP;
    return nullptr; // Only statically linked direct imports are supported.
}
REDHAWK_PALEXPORT void* PalGetProcAddress(HANDLE, const char*)
{
    errno = ENOTSUP;
    return nullptr;
}

static size_t PageBytes(size_t size)
{
    return size > SIZE_MAX - 4095 ? 0 : (size + 4095) & ~size_t(4095);
}
static int DataPermission(uint32_t protect)
{
    switch (protect)
    {
        case PAGE_NOACCESS: return 0;
        case PAGE_READONLY: return 1;
        case PAGE_READWRITE: return 3;
        default: return -1; // Never acknowledge executable permissions.
    }
}
REDHAWK_PALEXPORT UInt32_BOOL REDHAWK_PALAPI PalVirtualProtect(void* address, size_t size, uint32_t protect)
{
    int permission = DataPermission(protect);
    uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (permission < 0 || size == 0 || start > UINTPTR_MAX - size) return UInt32_FALSE;
    uintptr_t pageStart = start & ~uintptr_t(4095);
    size_t pageBytes = PageBytes(size + (start - pageStart));
    return pageBytes && LibnxSetDataPermission(reinterpret_cast<void*>(pageStart), pageBytes, permission);
}
REDHAWK_PALEXPORT void* REDHAWK_PALAPI PalVirtualAlloc(size_t size, uint32_t protect)
{
    size = PageBytes(size);
    if (!size || DataPermission(protect) < 0) return nullptr;
    void* memory = nxvm_reserve(size, 4096);
    if (!memory) return nullptr;
    if (!nxvm_commit(memory, size) || !PalVirtualProtect(memory, size, protect))
    {
        if (!nxvm_release(memory, size)) RhFailFast();
        return nullptr;
    }
    return memory;
}
REDHAWK_PALEXPORT void REDHAWK_PALAPI PalVirtualFree(void* address, size_t size)
{
    if (!nxvm_release(address, PageBytes(size))) RhFailFast();
}
REDHAWK_PALEXPORT void PalFlushInstructionCache(void* address, size_t size)
{
    LibnxFlushInstructionCache(address, size);
}

extern "C" HANDLE GetCurrentProcess()
{
    return (HANDLE)-1;
}

extern "C" uint32_t GetCurrentProcessId()
{
    return GCToOSInterface::GetCurrentProcessId();
}

extern "C" HANDLE GetCurrentThread()
{
    return (HANDLE)-2;
}

extern "C" UInt32_BOOL DuplicateHandle(
    HANDLE hSourceProcessHandle,
    HANDLE hSourceHandle,
    HANDLE hTargetProcessHandle,
    HANDLE * lpTargetHandle,
    uint32_t dwDesiredAccess,
    UInt32_BOOL bInheritHandle,
    uint32_t dwOptions)
{
    // We can only duplicate the current thread handle. That is all that the MRT uses.
    ASSERT(hSourceProcessHandle == GetCurrentProcess());
    ASSERT(hTargetProcessHandle == GetCurrentProcess());
    ASSERT(hSourceHandle == GetCurrentThread());
    if (lpTargetHandle == nullptr) return UInt32_FALSE;
    *lpTargetHandle = nullptr;
    if (hSourceProcessHandle != GetCurrentProcess() || hTargetProcessHandle != GetCurrentProcess() ||
        hSourceHandle != GetCurrentThread()) return UInt32_FALSE;
    LibnxThreadRegistration* registration = LibnxRegisterCurrentThread();
    if (!registration) return UInt32_FALSE;
    *lpTargetHandle = new (nothrow) ThreadUnixHandle(registration);
    if (*lpTargetHandle == nullptr && !LibnxUnregisterThread(registration)) RhFailFast();
    return *lpTargetHandle != nullptr;
}

extern "C" UInt32_BOOL InitializeCriticalSection(CRITICAL_SECTION * lpCriticalSection)
{
    pthread_mutexattr_t mutexAttributes;
    int st = pthread_mutexattr_init(&mutexAttributes);
    if (st != 0)
    {
        return false;
    }

    st = pthread_mutexattr_settype(&mutexAttributes, PTHREAD_MUTEX_RECURSIVE);
    if (st == 0)
    {
        st = pthread_mutex_init(&lpCriticalSection->mutex, &mutexAttributes);
    }

    pthread_mutexattr_destroy(&mutexAttributes);

    return (st == 0);
}

extern "C" UInt32_BOOL InitializeCriticalSectionEx(CRITICAL_SECTION * lpCriticalSection, uint32_t arg2, uint32_t arg3)
{
    return InitializeCriticalSection(lpCriticalSection);
}


extern "C" void DeleteCriticalSection(CRITICAL_SECTION * lpCriticalSection)
{
    pthread_mutex_destroy(&lpCriticalSection->mutex);
}

extern "C" void EnterCriticalSection(CRITICAL_SECTION * lpCriticalSection)
{
    pthread_mutex_lock(&lpCriticalSection->mutex);;
}

extern "C" void LeaveCriticalSection(CRITICAL_SECTION * lpCriticalSection)
{
    pthread_mutex_unlock(&lpCriticalSection->mutex);
}

extern "C" UInt32_BOOL IsDebuggerPresent()
{
#ifdef HOST_WASM
    // For now always true since the browser will handle it in case of WASM.
    return UInt32_TRUE;
#else
    // UNIXTODO: Implement this function
    return UInt32_FALSE;
#endif
}

extern "C" UInt32_BOOL SetEvent(HANDLE event)
{
    EventUnixHandle* unixHandle = (EventUnixHandle*)event;
    unixHandle->GetObject()->Set();

    return UInt32_TRUE;
}

extern "C" UInt32_BOOL ResetEvent(HANDLE event)
{
    EventUnixHandle* unixHandle = (EventUnixHandle*)event;
    unixHandle->GetObject()->Reset();

    return UInt32_TRUE;
}

extern "C" uint32_t GetEnvironmentVariableA(const char * name, char * buffer, uint32_t size)
{
    const char* value = getenv(name);
    if (value == NULL)
    {
        return 0;
    }

    size_t valueLen = strlen(value);
    if (valueLen < size)
    {
        strcpy(buffer, value);
        return valueLen;
    }

    // return required size including the null character or 0 if the size doesn't fit into uint32_t
    return (valueLen < UINT32_MAX) ? (valueLen + 1) : 0;
}

extern "C" uint16_t RtlCaptureStackBackTrace(uint32_t arg1, uint32_t arg2, void* arg3, uint32_t* arg4)
{
    // UNIXTODO: Implement this function
    return 0;
}

// Horizon Thread::Hijack uses synchronous suspension directly. There is no
// signal-based callback registration or POSIX pthread_kill emulation.
REDHAWK_PALIMPORT HijackFunc* REDHAWK_PALAPI PalGetHijackTarget(HijackFunc* defaultTarget)
{
    return defaultTarget;
}

extern "C" uint32_t WaitForSingleObjectEx(HANDLE handle, uint32_t milliseconds, UInt32_BOOL alertable)
{
    // The handle can only represent an event here
    // TODO: encapsulate this stuff
    UnixHandleBase* handleBase = (UnixHandleBase*)handle;
    ASSERT(handleBase->GetType() == UnixHandleType::Event);
    EventUnixHandle* unixHandle = (EventUnixHandle*)handleBase;

    return unixHandle->GetObject()->Wait(milliseconds);
}

REDHAWK_PALEXPORT uint32_t REDHAWK_PALAPI PalCompatibleWaitAny(UInt32_BOOL alertable, uint32_t timeout, uint32_t handleCount, HANDLE* pHandles, UInt32_BOOL allowReentrantWait)
{
    // Only a single handle wait for event is supported
    if (handleCount != 1 || pHandles == nullptr) return WAIT_FAILED;

    return WaitForSingleObjectEx(pHandles[0], timeout, alertable);
}

REDHAWK_PALEXPORT HANDLE PalCreateLowMemoryResourceNotification()
{
    return NULL;
}

#if !__has_builtin(_mm_pause)
extern "C" void _mm_pause()
// Defined for implementing PalYieldProcessor in PalRedhawk.h
{
#if defined(HOST_AMD64) || defined(HOST_X86)
  __asm__ volatile ("pause");
#endif
}
#endif

extern "C" int32_t _stricmp(const char *string1, const char *string2)
{
    return strcasecmp(string1, string2);
}

REDHAWK_PALIMPORT void REDHAWK_PALAPI PopulateControlSegmentRegisters(CONTEXT* pContext)
{
#if defined(TARGET_X86) || defined(TARGET_AMD64)
    // Currently the CONTEXT is only used on Windows for RaiseFailFastException.
    // So we punt on filling in SegCs and SegSs for now.
#endif
}

uint32_t g_RhNumberOfProcessors;

REDHAWK_PALEXPORT int32_t PalGetProcessCpuCount()
{
    ASSERT(g_RhNumberOfProcessors > 0);
    return g_RhNumberOfProcessors;
}

REDHAWK_PALEXPORT bool PalGetMaximumStackBounds(void** low, void** high)
{
    return LibnxGetCurrentStackBounds(low, high);
}
REDHAWK_PALEXPORT int32_t PalGetModuleFileName(const TCHAR** name, HANDLE moduleBase)
{
    // The loader supplies no stable module pathname here. Return no name rather
    // than inventing a path or exposing a pointer with the wrong lifetime.
    *name = nullptr;
    return 0;
}

extern "C" void FlushProcessWriteBuffers()
{
    GCToOSInterface::FlushProcessWriteBuffers();
}

static const int64_t SECS_BETWEEN_1601_AND_1970_EPOCHS = 11644473600LL;
static const int64_t SECS_TO_100NS = 10000000; /* 10^7 */

extern "C" void GetSystemTimeAsFileTime(FILETIME *lpSystemTimeAsFileTime)
{
    struct timeval time = { 0 };
    gettimeofday(&time, NULL);

    int64_t result = ((int64_t)time.tv_sec + SECS_BETWEEN_1601_AND_1970_EPOCHS) * SECS_TO_100NS +
        (time.tv_usec * 10);

    lpSystemTimeAsFileTime->dwLowDateTime = (uint32_t)result;
    lpSystemTimeAsFileTime->dwHighDateTime = (uint32_t)(result >> 32);
}

extern "C" uint64_t PalQueryPerformanceCounter()
{
    return GCToOSInterface::QueryPerformanceCounter();
}

extern "C" uint64_t PalQueryPerformanceFrequency()
{
    return GCToOSInterface::QueryPerformanceFrequency();
}

extern "C" uint64_t PalGetCurrentOSThreadId()
{
    return GCToOSInterface::GetCurrentThreadIdForLogging();
}
