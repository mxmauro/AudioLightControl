#include "Sha256.h"

#pragma comment(lib, "bcrypt.lib")

// -----------------------------------------------------------------------------

CSha256::~CSha256()
{
    if (m_hHash)
    {
        ::BCryptDestroyHash(m_hHash);
        m_hHash = nullptr;
    }
    memset(m_aHash, 0, sizeof(m_aHash));
}

VOID CSha256::Init()
{
    NTSTATUS nNtStatus;

    if (m_hHash)
    {
        ::BCryptDestroyHash(m_hHash);
        m_hHash = nullptr;
    }
    memset(m_aHash, 0, sizeof(m_aHash));

    nNtStatus = ::BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &m_hHash, nullptr, 0, nullptr, 0, 0);
    m_hRes = (nNtStatus >= 0) ? S_OK : HRESULT_FROM_NT(nNtStatus);
}

VOID CSha256::Update(_In_reads_bytes_(nDataLen) LPCVOID lpData, _In_ SIZE_T nDataLen)
{
    if (m_hHash && SUCCEEDED(m_hRes))
    {
        NTSTATUS nNtStatus;

        nNtStatus = ::BCryptHashData(m_hHash, reinterpret_cast<LPBYTE>(const_cast<LPVOID>(lpData)), static_cast<ULONG>(nDataLen), 0);
        m_hRes = (nNtStatus >= 0) ? S_OK : HRESULT_FROM_NT(nNtStatus);
    }
}

HRESULT CSha256::Finalize()
{
    if (m_hHash)
    {
        if (SUCCEEDED(m_hRes))
        {
            NTSTATUS nNtStatus;

            nNtStatus = ::BCryptFinishHash(m_hHash, m_aHash, HASH_BYTES, 0);
            m_hRes = (nNtStatus >= 0) ? S_OK : HRESULT_FROM_NT(nNtStatus);
        }

        ::BCryptDestroyHash(m_hHash);
        m_hHash = nullptr;
    }

    if (FAILED(m_hRes))
    {
        memset(m_aHash, 0, HASH_BYTES);
    }

    // Done
    return m_hRes;
}
