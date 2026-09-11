#include "pal/mapnative.h"
#include <assert.h>
#include <unistd.h>
int main() {
    size_t size = sysconf(_SC_PAGESIZE);
    void* memory = NativeMap(nullptr, size, MapRead | MapWrite, MapPrivate | MapAnonymous, -1, 0);
    assert(memory != MapFailed);
    *static_cast<int*>(memory) = 123;
    assert(NativeProtect(memory, size, MapRead) == 0);
    assert(*static_cast<int*>(memory) == 123);
    assert(NativeUnmap(memory, size) == 0);
}
