#include "Random.h"
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

// -----------------------------------------------------------------------------

HRESULT RandomBytes(_Out_writes_bytes_(nLen) LPBYTE lpBytes, _In_ SIZE_T nLen)
{
    NTSTATUS nNtStatus;

    nNtStatus = ::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(lpBytes), static_cast<ULONG>(nLen), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (nNtStatus < 0)
    {
        memset(lpBytes, 0, nLen);
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Done
    return S_OK;
}
