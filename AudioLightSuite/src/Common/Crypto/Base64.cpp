#include "Base64.h"
#include "..\Utils\Exception2HR.h"
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")

#define HRESULT_FROM_LASTERROR() HRESULT_FROM_WIN32(::GetLastError())

// -----------------------------------------------------------------------------

namespace Base64 {

HRESULT Encode(_In_reads_bytes_(nDataLen) LPCVOID lpData, _In_ SIZE_T nDataLen, _In_ BOOL bIsUrl, _Out_ std::string &encoded)
{
    DWORD dwOutSize;
    DWORD dwFlags = CRYPT_STRING_NOCRLF;
    HRESULT hRes;

    encoded.clear();

    if (nDataLen == 0)
    {
        return S_OK;
    }
    if (!lpData)
    {
        return E_INVALIDARG;
    }

    // Calculate output size
    dwFlags |= (bIsUrl != FALSE) ? CRYPT_STRING_BASE64URI : CRYPT_STRING_BASE64;
    dwOutSize = 0;
    if (!::CryptBinaryToStringA(reinterpret_cast<const BYTE *>(lpData), static_cast<DWORD>(nDataLen), dwFlags, nullptr, &dwOutSize))
    {
        return HRESULT_FROM_LASTERROR();
    }

    // Allocate output buffer
    hRes = TranslateToHresult([&]()
    {
        encoded.resize((size_t)dwOutSize);
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Encode
    if (!::CryptBinaryToStringA(reinterpret_cast<const BYTE *>(lpData), static_cast<DWORD>(nDataLen), dwFlags, encoded.data(), &dwOutSize))
    {
        hRes = HRESULT_FROM_LASTERROR();
        encoded.clear();
        return hRes;
    }

    // Remove trailing null terminator if present
    if (!encoded.empty() && encoded.back() == L'\0')
    {
        encoded.pop_back();
    }

    // Done
    return S_OK;
}

HRESULT Decode(_In_reads_(nDataLen) LPCSTR szDataA, _In_ SIZE_T nDataLen, _In_ BOOL bIsUrl, _Out_ std::vector<BYTE> &decoded)
{
    DWORD dwFlags = 0;
    DWORD dwOutSize;
    HRESULT hRes;

    decoded.clear();

    if (nDataLen == 0)
    {
        return S_OK;
    }
    if (!szDataA)
    {
        return E_INVALIDARG;
    }
    while (*szDataA == ' ' || *szDataA == '\t' || *szDataA == '\r' || *szDataA == '\n')
    {
        ++szDataA;
        --nDataLen;
    }

    while (nDataLen > 0 &&
           (szDataA[nDataLen - 1] == ' ' || szDataA[nDataLen - 1] == '\t' ||
            szDataA[nDataLen - 1] == '\r' || szDataA[nDataLen - 1] == '\n'))
    {
        ++szDataA;
        --nDataLen;
    }
    if (nDataLen == 0)
    {
        return S_OK;
    }

    // Calculate output size
    dwFlags |= (bIsUrl != FALSE) ? CRYPT_STRING_BASE64URI : CRYPT_STRING_BASE64;
    dwOutSize = 0;
    if (!::CryptStringToBinaryA(szDataA, static_cast<DWORD>(nDataLen), CRYPT_STRING_BASE64, nullptr, &dwOutSize, nullptr, nullptr))
    {
        throw HRESULT_FROM_LASTERROR();
    }

    // Allocate output buffer
    hRes = TranslateToHresult([&]()
    {
        decoded.resize((size_t)dwOutSize);
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Decode
    if (!::CryptStringToBinaryA(szDataA, static_cast<DWORD>(nDataLen), CRYPT_STRING_BASE64, decoded.data(), &dwOutSize, nullptr, nullptr))
    {
        hRes = HRESULT_FROM_LASTERROR();
        decoded.clear();
        return hRes;
    }

    // Done
    return S_OK;
}

} // namespace Base64
