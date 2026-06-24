#include "Hkdf.h"
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

// -----------------------------------------------------------------------------

namespace Hkdf {

HRESULT Sha256DeriveKey(_In_reads_bytes_(nKeyLen) LPCVOID lpKey, _In_ SIZE_T nKeyLen,
                        _In_reads_bytes_opt_(nSaltLen) LPCVOID lpSalt, _In_ SIZE_T nSaltLen,
                        _In_reads_bytes_(nInfoLen) LPCVOID lpInfo, _In_ SIZE_T nInfoLen,
                        _Out_writes_bytes_(nDerivedKeyLen) LPBYTE lpDerivedKey, _In_ SIZE_T nDerivedKeyLen)
{
    BCRYPT_KEY_HANDLE hHkdfKey;
    BCryptBuffer sBuf;
    BCryptBufferDesc sBufParams;
    ULONG cbKeyOut;
    NTSTATUS nNtStatus;

    // Verify parameters
    if ((!lpKey) || nKeyLen == 0 || (!lpDerivedKey) || nDerivedKeyLen == 0)
    {
        return E_INVALIDARG;
    }
    if (nSaltLen > 0 && (!lpSalt))
    {
        return E_INVALIDARG;
    }

    // Create a KDF key handle from the input keying material (IKM)
    nNtStatus = ::BCryptGenerateSymmetricKey(BCRYPT_HKDF_ALG_HANDLE, &hHkdfKey, nullptr, 0,
                                             reinterpret_cast<PUCHAR>(const_cast<LPVOID>(lpKey)),
                                             static_cast<ULONG>(nKeyLen), 0);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Set HKDF hash algorithm: SHA-256
    nNtStatus = ::BCryptSetProperty(hHkdfKey, BCRYPT_HKDF_HASH_ALGORITHM,
                                    reinterpret_cast<PUCHAR>(const_cast<LPWSTR>(BCRYPT_SHA256_ALGORITHM)),
                                    static_cast<ULONG>((wcslen(BCRYPT_SHA256_ALGORITHM) + 1) * sizeof(WCHAR)), 0);
    if (nNtStatus < 0)
    {
        ::BCryptDestroyKey(hHkdfKey);
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Provide optional salt and finalize the KDF key handle (treat input as IKM)
    nNtStatus = ::BCryptSetProperty(hHkdfKey, BCRYPT_HKDF_SALT_AND_FINALIZE,
                                    ((nSaltLen > 0) ? reinterpret_cast<PUCHAR>(const_cast<LPVOID>(lpSalt))
                                                    : reinterpret_cast<PUCHAR>(const_cast<LPSTR>(""))),
                                    static_cast<ULONG>(nSaltLen), 0);
    if (nNtStatus < 0)
    {
        ::BCryptDestroyKey(hHkdfKey);
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Derive output key using optional info
    cbKeyOut = 0;
    if (nInfoLen > 0)
    {
        sBuf.BufferType = KDF_HKDF_INFO;
        sBuf.cbBuffer = static_cast<ULONG>(nInfoLen);
        sBuf.pvBuffer = const_cast<LPVOID>(lpInfo);

        sBufParams.ulVersion = BCRYPTBUFFER_VERSION;
        sBufParams.cBuffers = 1;
        sBufParams.pBuffers = &sBuf;
    }
    nNtStatus = ::BCryptKeyDerivation(hHkdfKey, (nInfoLen > 0) ? &sBufParams : nullptr, lpDerivedKey, static_cast<ULONG>(nDerivedKeyLen),
                                      &cbKeyOut, 0);
    ::BCryptDestroyKey(hHkdfKey);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }
    if (static_cast<SIZE_T>(cbKeyOut) != nDerivedKeyLen)
    {
        return E_UNEXPECTED;
    }

    // Done
    return S_OK;
}

} // namespace Hkdf
