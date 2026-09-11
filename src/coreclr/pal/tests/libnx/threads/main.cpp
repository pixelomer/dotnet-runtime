#include "pal/threadnative.h"
#include <libs/Common/pal_threading_libnx.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/kernel/thread.h>
#include <switch/runtime/pthread.h>
#include <switch/result.h>
unsigned __nx_applet_exit_mode = 1;
}
static FILE* output;
static std::atomic<unsigned> checks{0};
static pthread_key_t lateKey;
struct State {
    std::atomic<bool> ready{false}, finish{false}, destructor{false}, release{false}, done{false};
    bool explicitExit;
    void* low; void* high;
};
static void check(bool yes, const char* name) {
    unsigned n = ++checks;
    if (!yes) { fprintf(output, "FAIL %s check=%u\n", name, n); abort(); }
}
static void wait(std::atomic<bool>& flag) {
    for (unsigned i=0; i<10000 && !flag.load(); ++i) svcSleepThread(1000000);
    check(flag.load(), "bounded worker wait");
}
static void destructor(void* value) {
    auto s = static_cast<State*>(value);
    s->destructor = true;
    wait(s->release);
    s->done = true;
}
static void* worker(void* value) {
    auto s = static_cast<State*>(value);
    check(NativeGetCurrentStackBounds(&s->low, &s->high), "actual worker stack bounds");
    check(reinterpret_cast<uintptr_t>(&value) >= reinterpret_cast<uintptr_t>(s->low) &&
          reinterpret_cast<uintptr_t>(&value) < reinterpret_cast<uintptr_t>(s->high), "worker local inside reported stack");
    check(pthreadGetNativeHandle(pthread_self()) == threadGetSelf()->handle, "pthread native identity matches libnx");
    check(pthread_setspecific(lateKey, s) == 0, "register late TLS cleanup");
    s->ready = true;
    wait(s->finish);
    if (s->explicitExit) pthread_exit(nullptr);
    return nullptr;
}
static int kernelPriority(pthread_t thread) {
    s32 priority;
    check(R_SUCCEEDED(svcGetThreadPriority(&priority, pthreadGetNativeHandle(thread))), "read actual kernel priority");
    return priority;
}
static size_t settle() {
    size_t previous = mallinfo().uordblks;
    unsigned stable = 0;
    for (unsigned i=0; i<5000 && stable<20; ++i) {
        svcSleepThread(1000000);
        size_t current = mallinfo().uordblks;
        stable = current == previous ? stable + 1 : 0;
        previous = current;
    }
    check(stable == 20, "reaper settles after kernel exits");
    return previous;
}
static void batch(bool legacy, unsigned round) {
    State states[4]; pthread_t identities[4];
    pthread_attr_t attr;
    check(pthread_attr_init(&attr) == 0 && pthread_attr_setstacksize(&attr, 256*1024) == 0, "requested stack attributes");
    check(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) == 0, "CoreCLR detached attribute");
    for (unsigned i=0; i<4; ++i) {
        states[i].explicitExit = (i & 1) != 0;
        size_t requested = (128 + i * 64) * 1024;
        check(pthread_attr_setstacksize(&attr, requested) == 0, "vary requested stack allocation");
        int error = legacy ? LibnxCreateDetachedThread(requested, worker, &states[i]) :
            LibnxCreateDetachedThreadWithAttributes(&identities[i], &attr, worker, &states[i]);
        check(error == 0, "create reaped worker");
    }
    for (unsigned i=0; i<4; ++i) {
        wait(states[i].ready);
        size_t usable = reinterpret_cast<uintptr_t>(states[i].high) - reinterpret_cast<uintptr_t>(states[i].low);
        size_t firstUsable = reinterpret_cast<uintptr_t>(states[0].high) - reinterpret_cast<uintptr_t>(states[0].low);
        // libnx reserves its bootstrap arguments above the usable stack. Verify
        // exact changes in requested capacity without inventing those private
        // bytes as stack space or hard-coding the bootstrap structure's layout.
        check(usable > 64*1024 && usable <= (128 + i*64)*1024 && usable == firstUsable + i*64*1024,
            "usable stack capacity follows exact requested increments");
        if (round == 0) fprintf(output, "STACK requested=%u usable=%zu\n", (128+i*64)*1024, usable);
        if (!legacy) {
            int original = kernelPriority(identities[i]);
            check(!NativeSetThreadPriority(identities[i], 3) && kernelPriority(identities[i]) == original, "invalid priority leaves kernel unchanged");
            const int choices[] = {-15,-2,-1,0,1,2,15};
            int prior = 64;
            for (int p: choices) {
                check(NativeSetThreadPriority(identities[i], p), "set supported priority through Horizon");
                int current = kernelPriority(identities[i]);
                check(current <= prior, "increasing PAL priority never lowers native priority");
                prior = current;
            }
            check(NativeSetThreadPriority(identities[i], 0), "restore worker baseline");
            check(kernelPriority(identities[i]) == original, "baseline equals original libnx worker priority");
        }
        states[i].finish = true;
    }
    for (unsigned i=0; i<4; ++i) {
        wait(states[i].destructor);
        // Completion TLS may already have enqueued this worker, but it must
        // retain its native handle and stack until the later destructor exits.
        if (!legacy) {
            MemoryInfo info{}; u32 page;
            check(R_SUCCEEDED(svcQueryMemory(&info, &page, reinterpret_cast<u64>(states[i].low))) && info.perm == Perm_Rw, "late destructor retains its executing stack");
            check(kernelPriority(identities[i]) == 0x3b, "late destructor retains live native handle");
        }
        states[i].release = true;
    }
    for (auto& s: states) wait(s.done);
    check(pthread_attr_destroy(&attr) == 0, "destroy caller attributes");
    size_t used = settle();
    fprintf(output, "BATCH %u legacy=%u checks=%u native_used=%zu\n", round, legacy, checks.load(), used);
}
static void* warmup(void* value) { static_cast<std::atomic<bool>*>(value)->store(true); return nullptr; }
int main() {
    output = fopen("sdmc:/switch/coreclr-thread-probe.txt", "w");
    if (!output) return 1;
    setvbuf(output, nullptr, _IONBF, 0);
    fprintf(output, "BEGIN CoreCLR native thread boundary; no managed runtime\n");
    void *low, *high;
    check(NativeGetCurrentStackBounds(&low, &high), "actual main stack bounds");
    check(pthreadGetNativeHandle(pthread_self()) == threadGetSelf()->handle, "main pthread identity");
    check(!NativeSetThreadPriority(nullptr, 0), "null thread rejected");
    std::atomic<bool> warmed{false};
    check(LibnxCreateDetachedThread(0, warmup, &warmed) == 0, "initialize shared reaper");
    wait(warmed); settle();
    // Allocate after completionKey so this destructor executes after queuing.
    check(pthread_key_create(&lateKey, destructor) == 0, "late cleanup key");
    for (unsigned round=0; round<64; ++round) batch((round & 1) != 0, round);
    check(pthread_key_delete(lateKey) == 0, "release test TLS key");
    fprintf(output, "PASS checks=%u workers=256 explicit_exits=128 rounds=64\n", checks.load());
    fclose(output); return 0;
}
