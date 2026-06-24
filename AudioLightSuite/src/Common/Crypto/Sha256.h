#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

// -----------------------------------------------------------------------------

class CSha256
{
public:
    static constexpr SIZE_T HASH_BYTES = 32;

public:
    CSha256() = default;
    CSha256(_In_ const CSha256 &) = delete;
    CSha256(_Inout_ CSha256 &&) = delete;
    ~CSha256();

    CSha256 &operator=(_In_ const CSha256 &) = delete;
    CSha256 &operator=(_Inout_ CSha256 &&) = delete;

    VOID Init();
    VOID Update(_In_reads_bytes_(nDataLen) LPCVOID lpData, _In_ SIZE_T nDataLen);
    HRESULT Finalize();

    LPBYTE GetHash() const
    {
        return const_cast<LPBYTE>(m_aHash);
    };

private:
    BCRYPT_HASH_HANDLE m_hHash{ nullptr };
    BYTE m_aHash[HASH_BYTES]{ 0 };
    HRESULT m_hRes{ E_NOT_SET };
};
