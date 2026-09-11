#include <unistd.h>
#include <switch/runtime/devices/fs_dev.h>

// This devkitPro newlib declares pread but supplies no implementation. The
// libnx driver performs an offset read without touching descriptor position.
ssize_t pread(int fd, void* buffer, size_t size, off_t offset)
{
    return fsdevPread(fd, buffer, size, offset);
}
