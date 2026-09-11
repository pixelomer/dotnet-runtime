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
static void Initialize(void)
{
    pthread_t reaper;
    initializationError = pthread_create(&reaper, NULL, Reap, NULL);
    // The reaper intentionally lives until process exit; it is not unloaded.
}
static void* Run(void* argument)
{
    Work* work = argument;
    void* result = work->entry(work->argument);
    if (pthread_mutex_lock(&lock)) abort();
    if (tail) tail->next = work;
    else head = work;
    tail = work;
    if (pthread_cond_signal(&ready)) abort();
    if (pthread_mutex_unlock(&lock)) abort();
    return result;
}
int LibnxCreateDetachedThread(size_t stackSize, void* (*entry)(void*), void* argument)
{
    if (!entry) return EINVAL;
    int error = pthread_once(&once, Initialize);
    if (error) return error;
    if (initializationError) return initializationError;
    Work* work = calloc(1, sizeof(*work));
    if (!work) return ENOMEM;
    work->entry = entry;
    work->argument = argument;
    pthread_attr_t attrs;
    error = pthread_attr_init(&attrs);
    if (error) { free(work); return error; }
    if (stackSize) error = pthread_attr_setstacksize(&attrs, stackSize);
    if (!error) {
        // Publish thread identity before a fast callback can enqueue itself.
        if (pthread_mutex_lock(&lock)) abort();
        error = pthread_create(&work->thread, &attrs, Run, work);
        if (pthread_mutex_unlock(&lock)) abort();
    }
    if (pthread_attr_destroy(&attrs)) abort();
    if (error) free(work);
    return error;
}
