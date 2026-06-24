#include "P256.h"

#pragma comment(lib, "bcrypt.lib")

#define STATUS_INVALID_SIGNATURE 0xC000A000

// -----------------------------------------------------------------------------

static VOID SwapBytes(_In_ LPBYTE lpData, _In_ SIZE_T nDataLen);

// -----------------------------------------------------------------------------

CP256KeyPair::~CP256KeyPair()
{
    Reset();
}

VOID CP256KeyPair::Reset()
{
    if (m_hKey)
    {
        ::BCryptDestroyKey(m_hKey);
        m_hKey = nullptr;
    }
}

HRESULT CP256KeyPair::InitFromPublicKey(_In_reads_bytes_(RAW_PUBLIC_KEY_BYTES) const BYTE aRawPublicKey[RAW_PUBLIC_KEY_BYTES])
{
    PublicKeyBlob_t sPK;
    NTSTATUS nNtStatus;

    // Verify parameters
    if (aRawPublicKey[0] != 0x04)
    {
        return E_INVALIDARG;
    }

    Reset();

    // Import the public key
    sPK.sHeader.dwMagic = GetAlgMagic(TRUE);
    sPK.sHeader.cbKey = KEY_BYTES;
    memcpy(sPK.aKey, aRawPublicKey + 1, RAW_PUBLIC_KEY_BYTES - 1);
    nNtStatus = ::BCryptImportKeyPair(GetAlgHandle(), 0, BCRYPT_ECCPUBLIC_BLOB, &m_hKey, reinterpret_cast<PUCHAR>(&sPK),
                                      static_cast<ULONG>(sizeof(sPK)), 0);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Done
    return S_OK;
}

HRESULT CP256KeyPair::InitFromPrivateKey(_In_reads_bytes_(RAW_PRIVATE_KEY_BYTES) const BYTE aRawPrivateKey[RAW_PRIVATE_KEY_BYTES])
{
    PrivateKeyBlob_t sSK;
    NTSTATUS nNtStatus;

    Reset();

    // Import the private key but do not verify for the public component (since we don't have it)
    sSK.sHeader.dwMagic = GetAlgMagic(FALSE);
    sSK.sHeader.cbKey = RAW_PRIVATE_KEY_BYTES;
    memset(sSK.aKey, 0, 2 * KEY_BYTES);
    memcpy(sSK.aKey + 2 * KEY_BYTES, aRawPrivateKey, RAW_PRIVATE_KEY_BYTES);
    nNtStatus = ::BCryptImportKeyPair(GetAlgHandle(), 0, BCRYPT_ECCPRIVATE_BLOB, &m_hKey, reinterpret_cast<PUCHAR>(&sSK),
                                      static_cast<ULONG>(sizeof(sSK)), BCRYPT_NO_KEY_VALIDATION);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Done
    return S_OK;
}

HRESULT CP256KeyPair::GenerateKeyPair(_Out_writes_bytes_(RAW_PUBLIC_KEY_BYTES) BYTE aRawPublicKey[RAW_PUBLIC_KEY_BYTES],
                                      _Out_writes_bytes_(RAW_PRIVATE_KEY_BYTES) BYTE aRawPrivateKey[RAW_PRIVATE_KEY_BYTES])
{
    PrivateKeyBlob_t sSK;
    ULONG cbBlob;
    NTSTATUS nNtStatus;

    memset(aRawPublicKey, 0, RAW_PUBLIC_KEY_BYTES);
    memset(aRawPrivateKey, 0, RAW_PRIVATE_KEY_BYTES);

    Reset();

    // Generate a new key pair
    nNtStatus = ::BCryptGenerateKeyPair(GetAlgHandle(), &m_hKey, static_cast<ULONG>(KEY_BYTES * 8), 0);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Finalize the key pair to make it usable for export
    nNtStatus = ::BCryptFinalizeKeyPair(m_hKey, 0);
    if (nNtStatus < 0)
    {
        Reset();
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Export the key pair to get the public and private components together (Windows exports them together in a single blob)
    cbBlob = 0;
    nNtStatus = ::BCryptExportKey(m_hKey, nullptr, BCRYPT_ECCPRIVATE_BLOB, reinterpret_cast<PUCHAR>(&sSK),
                                  static_cast<ULONG>(sizeof(sSK)), &cbBlob, 0);
    if (nNtStatus < 0)
    {
        Reset();
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Split the blob into raw public and private key formats
    aRawPublicKey[0] = 0x04; // Uncompressed
    memcpy(aRawPublicKey + 1, sSK.aKey, RAW_PUBLIC_KEY_BYTES - 1);
    memcpy(aRawPrivateKey, sSK.aKey + 2 * KEY_BYTES, RAW_PRIVATE_KEY_BYTES);

    // Done
    return S_OK;
}

// -----------------------------------------------------------------------------

HRESULT CECDHKeyPair::ComputeSharedSecret(_In_reads_bytes_(RAW_PUBLIC_KEY_BYTES) BYTE aOtherPartyRawPublicKey[RAW_PUBLIC_KEY_BYTES],
                                          _Out_writes_bytes_(SHARED_SECRET_SIZE) BYTE aSecret[SHARED_SECRET_SIZE])
{
    BCRYPT_KEY_HANDLE hPeerKey;
    BCRYPT_SECRET_HANDLE hSecret;
    PublicKeyBlob_t sPK;
    ULONG cbResult;
    NTSTATUS nNtStatus;

    memset(aSecret, 0, SHARED_SECRET_SIZE);

    // Verify parameters
    if (aOtherPartyRawPublicKey[0] != 0x04)
    {
        return E_INVALIDARG;
    }

    // Check if we have a private key to perform the agreement
    if (!m_hKey)
    {
        return E_NOT_SET;
    }

    // Import the other party's public key
    sPK.sHeader.dwMagic = GetAlgMagic(TRUE);
    sPK.sHeader.cbKey = KEY_BYTES;
    memcpy(sPK.aKey, aOtherPartyRawPublicKey + 1, RAW_PUBLIC_KEY_BYTES - 1);
    nNtStatus = ::BCryptImportKeyPair(GetAlgHandle(), 0, BCRYPT_ECCPUBLIC_BLOB, &hPeerKey, reinterpret_cast<PUCHAR>(&sPK),
                                      static_cast<ULONG>(sizeof(sPK)), 0);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Perform the secret agreement
    nNtStatus = ::BCryptSecretAgreement(m_hKey, hPeerKey, &hSecret, 0);
    ::BCryptDestroyKey(hPeerKey);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Derive the shared secret in raw format (i.e., the X coordinate of the ECDH result)
    cbResult = 0;
    nNtStatus = ::BCryptDeriveKey(hSecret, BCRYPT_KDF_RAW_SECRET, nullptr, reinterpret_cast<PUCHAR>(aSecret), SHARED_SECRET_SIZE,
                                  &cbResult, 0);
    ::BCryptDestroySecret(hSecret);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }
    if (cbResult != SHARED_SECRET_SIZE)
    {
        return E_UNEXPECTED;
    }

    // Convert to big endian (Windows returns little-endian)
    SwapBytes(aSecret, SHARED_SECRET_SIZE);

    // Done
    return S_OK;
}

BCRYPT_ALG_HANDLE CECDHKeyPair::GetAlgHandle() const
{
    return BCRYPT_ECDH_P256_ALG_HANDLE;
}

DWORD CECDHKeyPair::GetAlgMagic(_In_ BOOL bPublicKey) const
{
    return (bPublicKey) ? BCRYPT_ECDH_PUBLIC_P256_MAGIC : BCRYPT_ECDH_PRIVATE_P256_MAGIC;
}

// -----------------------------------------------------------------------------

HRESULT CECDsaKeyPair::Sign(_In_reads_bytes_(HASH_SIZE) const BYTE aHash[HASH_SIZE],
                            _Out_writes_bytes_(SIGNATURE_SIZE) BYTE aSignature[SIGNATURE_SIZE])
{
    ULONG cbSig;
    NTSTATUS nNtStatus;

    // Check if we have a private key to perform the signature
    if (!m_hKey)
    {
        return E_NOT_SET;
    }

    cbSig = 0;
    nNtStatus = ::BCryptSignHash(m_hKey, nullptr, const_cast<PUCHAR>(aHash), HASH_SIZE, aSignature, SIGNATURE_SIZE, &cbSig, 0);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }
    if (cbSig != SIGNATURE_SIZE)
    {
        return E_UNEXPECTED;
    }

    // Done
    return S_OK;
}

HRESULT CECDsaKeyPair::Verify(_In_reads_bytes_(HASH_SIZE) const BYTE aHash[HASH_SIZE],
                              _In_reads_bytes_(SIGNATURE_SIZE) BYTE aSignature[SIGNATURE_SIZE])
{
    NTSTATUS nNtStatus;

    // Check if we have a public key to perform the signature verification
    if (!m_hKey)
    {
        return E_NOT_SET;
    }

    nNtStatus = ::BCryptVerifySignature(m_hKey, nullptr, const_cast<PUCHAR>(aHash), HASH_SIZE, aSignature, SIGNATURE_SIZE, 0);
    if (nNtStatus == STATUS_INVALID_SIGNATURE)
    {
        return S_FALSE; // Signature did not match
    }
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }

    // Done
    return S_OK;
}

BCRYPT_ALG_HANDLE CECDsaKeyPair::GetAlgHandle() const
{
    return BCRYPT_ECDSA_P256_ALG_HANDLE;
}

DWORD CECDsaKeyPair::GetAlgMagic(_In_ BOOL bPublicKey) const
{
    return (bPublicKey) ? BCRYPT_ECDSA_PUBLIC_P256_MAGIC : BCRYPT_ECDSA_PRIVATE_P256_MAGIC;
}

// -----------------------------------------------------------------------------

static VOID SwapBytes(_In_ LPBYTE lpData, _In_ SIZE_T nDataLen)
{
    BYTE temp;
    SIZE_T i;

    for (i = 0; i < nDataLen / 2; i++)
    {
        temp = lpData[i];
        lpData[i] = lpData[nDataLen - i - 1];
        lpData[nDataLen - i - 1] = temp;
    }
}
