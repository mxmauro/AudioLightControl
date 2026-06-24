#include "Aes.h"
#include <system_error>

#define STATUS_AUTH_TAG_MISMATCH 0xC000A002L

// -----------------------------------------------------------------------------

CAes::CAes(_In_ const CAes &other)
{
    if (other.m_hKey)
    {
        NTSTATUS nNtStatus;

        nNtStatus = ::BCryptDuplicateKey(other.m_hKey, &m_hKey, nullptr, 0, 0);
        if (nNtStatus < 0)
        {
            m_hKey = nullptr;
            throw std::system_error(std::error_code(HRESULT_FROM_NT(nNtStatus), std::system_category()), "Win32 API call failed");
        }
    }
}

CAes::CAes(_Inout_ CAes &&other) noexcept
{
    if (this != &other)
    {
        m_hKey = other.m_hKey;
        other.m_hKey = nullptr;
    }
}

CAes::~CAes() noexcept
{
    Reset();
}

CAes& CAes::operator=(_In_ const CAes &other)
{
    Reset();
    if (other.m_hKey)
    {
        NTSTATUS nNtStatus;

        nNtStatus = ::BCryptDuplicateKey(other.m_hKey, &m_hKey, nullptr, 0, 0);
        if (nNtStatus < 0)
        {
            m_hKey = nullptr;
            throw std::system_error(std::error_code(HRESULT_FROM_NT(nNtStatus), std::system_category()), "Win32 API call failed");
        }
    }
    return *this;
}

CAes& CAes::operator=(_Inout_ CAes &&other) noexcept
{
    if (this != &other)
    {
        m_hKey = other.m_hKey;
        other.m_hKey = nullptr;
    }
    return *this;
}

VOID CAes::Reset() noexcept
{
    if (m_hKey)
    {
        ::BCryptDestroyKey(m_hKey);
        m_hKey = nullptr;
    }
}

HRESULT CAes::SetKey(_In_reads_bytes_(nKeyLen) LPCVOID lpKey, _In_ SIZE_T nKeyLen) noexcept
{
    NTSTATUS nNtStatus;

    if (m_hKey)
    {
        ::BCryptDestroyKey(m_hKey);
        m_hKey = nullptr;
    }

    // Create symmetric key
    nNtStatus = ::BCryptGenerateSymmetricKey(BCRYPT_AES_GCM_ALG_HANDLE, &m_hKey, nullptr, 0,
                                             reinterpret_cast<PUCHAR>(const_cast<LPVOID>(lpKey)), static_cast<ULONG>(nKeyLen), 0);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Done
    return S_OK;
}

HRESULT CAes::Encrypt(_In_reads_bytes_(nPlainTextLen) LPCVOID lpPlainText, _In_ SIZE_T nPlainTextLen,
                      _In_reads_bytes_(nIVLen) LPCVOID lpIV, _In_ SIZE_T nIVLen,
                      _In_reads_bytes_opt_(nAadLen) LPCVOID lpAad, _In_ SIZE_T nAadLen,
                      _Out_writes_bytes_(nPlainTextLen + GCM_TAG_LEN) LPBYTE lpCipherTextAndTag) noexcept
{
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO sAuthInfo;
    ULONG cbOutSize;
    NTSTATUS nNtStatus;

    // Verify parameters
    if ((!lpPlainText) || nPlainTextLen == 0 || (!lpIV) || nIVLen == 0 || (!lpCipherTextAndTag))
    {
        return E_INVALIDARG;
    }
    if (nAadLen > 0 && (!lpAad))
    {
        memset(lpCipherTextAndTag, 0, nPlainTextLen + GCM_TAG_LEN);
        return E_INVALIDARG;
    }

    // Check if we have a key to perform the encryption
    if (!m_hKey)
    {
        memset(lpCipherTextAndTag, 0, nPlainTextLen + GCM_TAG_LEN);
        return E_NOT_SET;
    }

    // Encrypt
    BCRYPT_INIT_AUTH_MODE_INFO(sAuthInfo);
    sAuthInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<LPVOID>(lpIV));
    sAuthInfo.cbNonce = static_cast<ULONG>(nIVLen);
    if (nAadLen > 0)
    {
        sAuthInfo.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<LPVOID>(lpAad));
        sAuthInfo.cbAuthData = static_cast<ULONG>(nAadLen);
    }
    sAuthInfo.pbTag = lpCipherTextAndTag + nPlainTextLen;
    sAuthInfo.cbTag = GCM_TAG_LEN;

    cbOutSize = 0;
    nNtStatus = ::BCryptEncrypt(m_hKey, reinterpret_cast<PUCHAR>(const_cast<LPVOID>(lpPlainText)), static_cast<ULONG>(nPlainTextLen),
                                &sAuthInfo, nullptr, 0, lpCipherTextAndTag, static_cast<ULONG>(nPlainTextLen), &cbOutSize, 0);
    if (nNtStatus < 0)
    {
        memset(lpCipherTextAndTag, 0, nPlainTextLen + GCM_TAG_LEN);
        return HRESULT_FROM_NT(nNtStatus);
    }
    if (static_cast<SIZE_T>(cbOutSize) != nPlainTextLen)
    {
        memset(lpCipherTextAndTag, 0, nPlainTextLen + GCM_TAG_LEN);
        return E_UNEXPECTED;
    }

    // Done
    return S_OK;
}

HRESULT CAes::Decrypt(_In_reads_bytes_(nCipherTextLen) LPCVOID lpCipherText,
                      _In_ _In_range_(GCM_TAG_LEN, SIZE_MAX - 1) SIZE_T nCipherTextLen,
                      _In_reads_bytes_(nIVLen) LPCVOID lpIV, _In_ SIZE_T nIVLen,
                      _In_reads_bytes_opt_(nAadLen) LPCVOID lpAad, _In_ SIZE_T nAadLen,
                      _Out_writes_bytes_(nCipherTextLen - GCM_TAG_LEN) LPBYTE lpPlainText) noexcept
{
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO sAuthInfo;
    ULONG cbOutSize;
    NTSTATUS nNtStatus;

    // Verify parameters
    if ((!lpCipherText) || nCipherTextLen <= GCM_TAG_LEN || (!lpIV) || nIVLen == 0 || (!lpPlainText))
    {
        return E_INVALIDARG;
    }
    if (nAadLen > 0 && (!lpAad))
    {
        memset(lpPlainText, 0, nCipherTextLen - GCM_TAG_LEN);
        return E_INVALIDARG;
    }

    // Check if we have a key to perform the encryption
    if (!m_hKey)
    {
        memset(lpPlainText, 0, nCipherTextLen - GCM_TAG_LEN);
        return E_NOT_SET;
    }

    // Decrypt
    BCRYPT_INIT_AUTH_MODE_INFO(sAuthInfo);
    sAuthInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<LPVOID>(lpIV));
    sAuthInfo.cbNonce = static_cast<ULONG>(nIVLen);
    if (nAadLen > 0)
    {
        sAuthInfo.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<LPVOID>(lpAad));
        sAuthInfo.cbAuthData = static_cast<ULONG>(nAadLen);
    }
    sAuthInfo.pbTag = reinterpret_cast<PUCHAR>(const_cast<LPVOID>(lpCipherText)) + nCipherTextLen - GCM_TAG_LEN;
    sAuthInfo.cbTag = GCM_TAG_LEN;

    cbOutSize = 0;
    nNtStatus = ::BCryptDecrypt(m_hKey, reinterpret_cast<PUCHAR>(const_cast<LPVOID>(lpCipherText)),
                                static_cast<ULONG>(nCipherTextLen - GCM_TAG_LEN), &sAuthInfo, nullptr, 0, lpPlainText,
                                static_cast<ULONG>(nCipherTextLen - GCM_TAG_LEN), &cbOutSize, 0);
    if (nNtStatus < 0)
    {
        memset(lpPlainText, 0, nCipherTextLen - GCM_TAG_LEN);
        return (nNtStatus == STATUS_AUTH_TAG_MISMATCH) ? S_FALSE : HRESULT_FROM_NT(nNtStatus);
    }
    if (static_cast<SIZE_T>(cbOutSize) != nCipherTextLen - GCM_TAG_LEN)
    {
        memset(lpPlainText, 0, nCipherTextLen - GCM_TAG_LEN);
        return E_UNEXPECTED;
    }

    // Done
    return S_OK;
}
