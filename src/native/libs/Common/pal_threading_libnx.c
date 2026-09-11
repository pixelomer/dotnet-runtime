#include "pal_threading_libnx.h"
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>

// devkitPro's pthread_create ignores detachstate; libnx has no detach syscall.
// Join completed threads on a native-only reaper, after TLS destructors and the
// kernel exit have finished. Never free a thread's currently executing stack.
typedef struct Work {
    struct Work* next;
    pthread_t thread;
    void* (*entry)(void*);
    void* argument;
} Work;
static pthread_key_t completionKey;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static Work* head;
static Work* tail;
static int initializationError;

static void* Reap(void* unused)
{
    (void)unused;
    for (;;) {
        if (pthread_mutex_lock(&lock)) abort();
        while (!head) if (pthread_cond_wait(&ready, &lock)) abort();
        Work* work = head;
        head = work->next;
        if (!head) tail = NULL;
        if (pthread_mutex_unlock(&lock)) abort();
        if (pthread_join(work->thread, NULL)) abort();
        free(work);
    }
    return NULL;
}
static void Completed(void* argument)
{
    Work* work = argument;
    if (pthread_mutex_lock(&lock)) abort();
    if (tail) tail->next = work;
    else head = work;
    tail = work;
    if (pthread_cond_signal(&ready)) abort();
    if (pthread_mutex_unlock(&lock)) abort();
    // The reaper joins before freeing Work/native resources, even if other
    // TLS destructors are still running after this completion notification.
}
static void Initialize(void)
{
    initializationError = pthread_key_create(&completionKey, Completed);
    if (initializationError) return;
    pthread_t reaper;
    initializationError = pthread_create(&reaper, NULL, Reap, NULL);
    if (initializationError && pthread_key_delete(completionKey)) abort();
    // The reaper intentionally lives until process exit; it is not unloaded.
}
static void* Run(void* argument)
{
    Work* work = argument;
    if (pthread_setspecific(completionKey, work)) abort();
    return work->entry(work->argument);
}
int LibnxCreateDetachedThreadWithAttributes(pthread_t* nativeThread, const pthread_attr_t* attrs,
    void* (*entry)(void*), void* argument)
{
    if (!entry) return EINVAL;
    int error = pthread_once(&once, Initialize);
    if (error) return error;
    if (initializationError) return initializationError;
    Work* work = calloc(1, sizeof(*work));
    if (!work) return ENOMEM;
    work->entry = entry;
    work->argument = argument;
    // Publish both identities before a fast callback's TLS destructor can
    // enqueue its Work. Detached identity has the usual pthread lifetime.
    if (pthread_mutex_lock(&lock)) abort();
    error = pthread_create(&work->thread, attrs, Run, work);
    if (!error && nativeThread) *nativeThread = work->thread;
    if (pthread_mutex_unlock(&lock)) abort();
    if (error) free(work);
    return error;
}
int LibnxCreateDetachedThread(size_t stackSize, void* (*entry)(void*), void* argument)
{
    pthread_attr_t attrs;
    int error = pthread_attr_init(&attrs);
    if (error) return error;
    if (stackSize) error = pthread_attr_setstacksize(&attrs, stackSize);
    if (!error) error = LibnxCreateDetachedThreadWithAttributes(NULL, &attrs, entry, argument);
    if (pthread_attr_destroy(&attrs)) abort();
    return error;
}
