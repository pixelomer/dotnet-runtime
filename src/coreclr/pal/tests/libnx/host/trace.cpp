// Link-time observation only: preserve the real PAL operation and last error.
#include <pal.h>
extern "C" void HostProtectionTrace(void*, size_t, unsigned, int, unsigned);
extern "C" BOOL PALAPI __real_VirtualProtect(LPVOID, SIZE_T, DWORD, PDWORD);
extern "C" BOOL PALAPI __wrap_VirtualProtect(LPVOID address, SIZE_T size, DWORD protection, PDWORD old)
{
    BOOL result = __real_VirtualProtect(address, size, protection, old);
    DWORD error = GetLastError();
    HostProtectionTrace(address, size, protection, result, error);
    SetLastError(error);
    return result;
}
