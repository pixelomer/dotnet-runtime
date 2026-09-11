#include <poll.h>
#include <cstdint>
static uint64_t calls, begin;
extern "C" int __real_poll(struct pollfd*, nfds_t, int);
extern "C" int __wrap_poll(struct pollfd* events, nfds_t count, int timeout) {
    __atomic_add_fetch(&calls, 1, __ATOMIC_RELAXED);
    return __real_poll(events, count, timeout);
}
extern "C" void HostSocketPollBegin() { begin = __atomic_load_n(&calls, __ATOMIC_RELAXED); }
extern "C" uint64_t HostSocketPollEnd() { return __atomic_load_n(&calls, __ATOMIC_RELAXED) - begin; }
