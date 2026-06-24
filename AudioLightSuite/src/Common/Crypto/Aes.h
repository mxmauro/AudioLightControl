#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <limits.h>

// -----------------------------------------------------------------------------

class CAes
{
public:
    static constexpr SIZE_T GCM_TAG_LEN = 16;

public:
    CAes() noexcept = default;
    CAes(_In_ const CAes &other);
    CAes(_Inout_ CAes &&other) noexcept;
    ~CAes() noexcept;

    CAes &operator=(_In_ const CAes &other);
    CAes &operator=(_Inout_ CAes &&other) noexcept;

    VOID Reset() noexcept;

    HRESULT SetKey(_In_reads_bytes_(nKeyLen) LPCVOID lpKey, _In_ SIZE_T nKeyLen) noexcept;

    // NOTE: The output buffer must be, at least, the size of the plain text plus 16 for the tag.
    HRESULT Encrypt(_In_reads_bytes_(nPlainTextLen) LPCVOID lpPlainText, _In_ SIZE_T nPlainTextLen,
                    _In_reads_bytes_(nIVLen) LPCVOID lpIV, _In_ SIZE_T nIVLen,
                    _In_reads_bytes_opt_(nAadLen) LPCVOID lpAad, _In_ SIZE_T nAadLen,
                    _Out_writes_bytes_(nPlainTextLen + GCM_TAG_LEN) LPBYTE lpCipherTextAndTag) noexcept;

    // NOTE: The input buffer contains the tag at the end so, the output buffer must be, at least,
    //       the size of the cipher text minus 16 for the tag.
    HRESULT Decrypt(_In_reads_bytes_(nCipherTextLen) LPCVOID lpCipherTextAndTag,
                    _In_ _In_range_(GCM_TAG_LEN, SIZE_MAX - 1) SIZE_T nCipherTextLen,
                    _In_reads_bytes_(nIVLen) LPCVOID lpIV, _In_ SIZE_T nIVLen,
                    _In_reads_bytes_opt_(nAadLen) LPCVOID lpAad, _In_ SIZE_T nAadLen,
                    _Out_writes_bytes_(nCipherTextLen - GCM_TAG_LEN) LPBYTE lpPlainText) noexcept;

private:
    BCRYPT_KEY_HANDLE m_hKey{ nullptr };
};
