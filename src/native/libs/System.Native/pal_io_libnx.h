#pragma once
#include <pthread.h>
#include <sys/iosupport.h>
#include "../Common/nxvm.h"

static bool LibnxPageLength(uint64_t length, size_t* rounded)
{
    if (!length || length > SIZE_MAX - 4095) { errno = EINVAL; return false; }
    *rounded = ((size_t)length + 4095) & ~(size_t)4095;
    return true;
}

// libnx/newlib has no positional I/O syscall. Serialize regular-file position
// operations within System.Native, including duplicated descriptors. Descriptor
// ownership remains with SafeFileHandle; foreign native users of the same open
// file description must not bypass this serialization. Pipes/terminals do not
// hold the lock across blocking read/write, so a pipe reader cannot deadlock its
// writer. This is a BCL adapter, not a replacement global libc pread/pwrite.
static pthread_mutex_t libnxFilePositionLock = PTHREAD_MUTEX_INITIALIZER;
static bool LibnxIsRegularFile(int fd)
{
    struct stat info;
    return fstat(fd, &info) == 0 && S_ISREG(info.st_mode);
}
static ssize_t LibnxRead(int fd, void* buffer, size_t size)
{
    bool regular = LibnxIsRegularFile(fd);
    if (regular && pthread_mutex_lock(&libnxFilePositionLock)) abort();
    ssize_t result = read(fd, buffer, size);
    int error = errno;
    if (regular && pthread_mutex_unlock(&libnxFilePositionLock)) abort();
    errno = error;
    return result;
}
static ssize_t LibnxWrite(int fd, const void* buffer, size_t size)
{
    bool regular = LibnxIsRegularFile(fd);
    if (regular && pthread_mutex_lock(&libnxFilePositionLock)) abort();
    ssize_t result = write(fd, buffer, size);
    int error = errno;
    if (regular && pthread_mutex_unlock(&libnxFilePositionLock)) abort();
    errno = error;
    return result;
}
static off_t LibnxSeek(int fd, off_t offset, int whence)
{
    if (pthread_mutex_lock(&libnxFilePositionLock)) abort();
    off_t result = lseek(fd, offset, whence);
    int error = errno;
    if (pthread_mutex_unlock(&libnxFilePositionLock)) abort();
    errno = error;
    return result;
}
static int LibnxClose(int fd)
{
    if (pthread_mutex_lock(&libnxFilePositionLock)) abort();
    int result = close(fd);
    int error = errno;
    if (pthread_mutex_unlock(&libnxFilePositionLock)) abort();
    errno = error;
    return result;
}
static int LibnxDup(int fd)
{
    if (pthread_mutex_lock(&libnxFilePositionLock)) abort();
    // libnx fcntl supports socket flags, not F_DUPFD[_CLOEXEC]. Its unsupported
    // command return can be positive ENOTSUP, which is not a valid duplicate.
    // libsysbase dup maintains real shared handle ownership. Validate through
    // its public handle lookup before dup (which lacks its own range check).
    int result;
    if (!__get_handle(fd)) { errno=EBADF; result=-1; }
    else result=dup(fd);
    int error=errno;
    if (pthread_mutex_unlock(&libnxFilePositionLock)) abort();
    errno=error;
    return result;
}
static ssize_t LibnxPositionedIO(int fd, void* buffer, size_t size, off_t offset, bool writing)
{
    if (offset < 0) { errno = EINVAL; return -1; }
    if (pthread_mutex_lock(&libnxFilePositionLock)) abort();
    off_t previous = lseek(fd, 0, SEEK_CUR);
    ssize_t result = -1;
    // fsdev rejects reads whose starting offset exceeds the file size. Read
    // from exact EOF instead, preserving access checks in the underlying read.
    // A regular-file read there returns zero without exposing unrelated bytes.
    if (!writing && previous != (off_t)-1) {
        struct stat info;
        if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && offset > info.st_size)
            offset = info.st_size;
    }
    if (previous != (off_t)-1 && lseek(fd, offset, SEEK_SET) != (off_t)-1) {
        result = writing ? write(fd, buffer, size) : read(fd, buffer, size);
        int error = errno;
        if (lseek(fd, previous, SEEK_SET) == (off_t)-1) {
            result = -1;
            error = errno;
        }
        errno = error;
    }
    int error = errno;
    if (pthread_mutex_unlock(&libnxFilePositionLock)) abort();
    errno = error;
    return result;
}
#define pread(fd, buffer, size, offset) LibnxPositionedIO(fd, buffer, size, offset, false)
#define pwrite(fd, buffer, size, offset) LibnxPositionedIO(fd, (void*)(buffer), size, offset, true)
#define read LibnxRead
#define write LibnxWrite
#define lseek LibnxSeek
#define close LibnxClose
