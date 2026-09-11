#include "pal/libnx/workerchannel.h"
#include <errno.h>
extern "C" {
#include <switch/arm/counter.h>
}
namespace {
struct Locked {
    Mutex* mutex;
    explicit Locked(Mutex* value) : mutex(value) { mutexLock(mutex); }
    ~Locked() { mutexUnlock(mutex); }
};
}
int LibnxWorkerChannel::Read(uint8_t* destination, int size, int timeoutMs) {
    if (size != 1 || !destination || timeoutMs < -1) { errno = EINVAL; return -1; }
    const uint64_t start = armGetSystemTick();
    const uint64_t timeout = timeoutMs == -1 ? UINT64_MAX : uint64_t(timeoutMs) * 1000000;
    Locked lock(&mutex);
    while (!count && !closed) {
        uint64_t remaining = timeout;
        if (timeout != UINT64_MAX) {
            uint64_t elapsed = armTicksToNs(armGetSystemTick() - start);
            if (elapsed >= timeout) return 0;
            remaining -= elapsed;
        }
        Result result = condvarWaitTimeout(&available, &mutex, remaining);
        if (result != 0 && result != 0xea01) { errno = EIO; return -1; }
        // Recheck the predicate even after timeout; a command may have arrived
        // before the kernel returned with the mutex reacquired.
    }
    if (!count) return 0;
    *destination = bytes[head];
    head = (head + 1) % sizeof(bytes);
    --count;
    return 1;
}
int LibnxWorkerChannel::Write(uint8_t command) {
    Locked lock(&mutex);
    if (closed) { errno = EPIPE; return -1; }
    if (count == sizeof(bytes)) { errno = EAGAIN; return -1; }
    bytes[(head + count) % sizeof(bytes)] = command;
    ++count;
    condvarWakeOne(&available);
    return 1;
}
void LibnxWorkerChannel::Close() {
    Locked lock(&mutex);
    closed = true;
    condvarWakeAll(&available);
}
