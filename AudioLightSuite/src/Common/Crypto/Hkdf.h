#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

namespace Hkdf {

HRESULT Sha256DeriveKey(_In_reads_bytes_(nKeyLen) LPCVOID lpKey, _In_ SIZE_T nKeyLen,
                        _In_reads_bytes_opt_(nSaltLen) LPCVOID lpSalt, _In_ SIZE_T nSaltLen,
                        _In_reads_bytes_(nInfoLen) LPCVOID lpInfo, _In_ SIZE_T nInfoLen,
                        _Out_writes_bytes_(nDerivedKeyLen) LPBYTE lpDerivedKey, _In_ SIZE_T nDerivedKeyLen);

} // namespace Hkdf
