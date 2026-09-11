#pragma once
#include <stddef.h>
#include <stdint.h>
extern "C" {
#include <switch/kernel/mutex.h>
#include <switch/kernel/condvar.h>
}
// In-process transport for PAL's existing byte-sized worker commands. Horizon
// has native process-wide keys, but no pipe descriptors. Queue order, bounded
// capacity, timeout, and drain-before-EOF behavior match the PAL's requirements.
class LibnxWorkerChannel {
    Mutex mutex = 0;
    CondVar available = 0;
    uint8_t bytes[4096]{};
    size_t head = 0, count = 0;
    bool closed = false;
public:
    int Read(uint8_t* destination, int size, int timeoutMs);
    int Write(uint8_t command);
    void Close();
};
