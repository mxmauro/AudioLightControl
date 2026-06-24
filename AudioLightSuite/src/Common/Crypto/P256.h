#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

// -----------------------------------------------------------------------------

class CP256KeyPair
{
public:
    static constexpr SIZE_T KEY_BYTES = 32;

    static constexpr SIZE_T RAW_PUBLIC_KEY_BYTES = 1 + 2 * KEY_BYTES;
    static constexpr SIZE_T RAW_PRIVATE_KEY_BYTES = KEY_BYTES;

protected:
    CP256KeyPair() = default;
    CP256KeyPair(_In_ const CP256KeyPair &) = delete;
    CP256KeyPair(_Inout_ CP256KeyPair &&) = delete;
public:
    ~CP256KeyPair();

    CP256KeyPair &operator=(_In_ const CP256KeyPair &) = delete;
    CP256KeyPair &operator=(_Inout_ CP256KeyPair &&) = delete;

    VOID Reset();

    HRESULT InitFromPublicKey(_In_reads_bytes_(RAW_PUBLIC_KEY_BYTES) const BYTE aRawPublicKey[RAW_PUBLIC_KEY_BYTES]);
    HRESULT InitFromPrivateKey(_In_reads_bytes_(RAW_PRIVATE_KEY_BYTES) const BYTE aRawPrivateKey[RAW_PRIVATE_KEY_BYTES]);

    HRESULT GenerateKeyPair(_Out_writes_bytes_(RAW_PUBLIC_KEY_BYTES) BYTE aRawPublicKey[RAW_PUBLIC_KEY_BYTES],
                            _Out_writes_bytes_(RAW_PRIVATE_KEY_BYTES) BYTE aRawPrivateKey[RAW_PRIVATE_KEY_BYTES]);

protected:
    virtual BCRYPT_ALG_HANDLE GetAlgHandle() const = 0;
    virtual DWORD GetAlgMagic(_In_ BOOL bPublicKey) const = 0;

protected:
#pragma pack(1)
    typedef struct PublicKeyBlob_s {
        BCRYPT_ECCKEY_BLOB sHeader;
        BYTE aKey[2 * KEY_BYTES]; // X || Y
    } PublicKeyBlob_t;

    typedef struct PrivateKeyBlob_s {
        BCRYPT_ECCKEY_BLOB sHeader;
        BYTE aKey[3 * KEY_BYTES]; // X || Y || d
    } PrivateKeyBlob_t;
#pragma pack()

protected:
    BCRYPT_KEY_HANDLE m_hKey{ nullptr };
};

class CECDHKeyPair : public CP256KeyPair
{
public:
    static constexpr SIZE_T SHARED_SECRET_SIZE = 32;

public:
    CECDHKeyPair() = default;
    CECDHKeyPair(_In_ const CECDHKeyPair &) = delete;
    CECDHKeyPair(_Inout_ CECDHKeyPair &&) = delete;
    ~CECDHKeyPair() = default;

    CECDHKeyPair &operator=(_In_ const CECDHKeyPair &) = delete;
    CECDHKeyPair &operator=(_Inout_ CECDHKeyPair &&) = delete;

    HRESULT ComputeSharedSecret(_In_reads_bytes_(RAW_PUBLIC_KEY_BYTES) BYTE aOtherPartyRawPublicKey[RAW_PUBLIC_KEY_BYTES],
                                _Out_writes_bytes_(SHARED_SECRET_SIZE) BYTE aSecret[SHARED_SECRET_SIZE]);

protected:
    BCRYPT_ALG_HANDLE GetAlgHandle() const;
    DWORD GetAlgMagic(_In_ BOOL bPublicKey) const;
};

class CECDsaKeyPair : public CP256KeyPair
{
public:
    static constexpr SIZE_T HASH_SIZE = 32;
    static constexpr SIZE_T SIGNATURE_SIZE = 64;

public:
    CECDsaKeyPair() = default;
    CECDsaKeyPair(_In_ const CECDsaKeyPair &) = delete;
    CECDsaKeyPair(_Inout_ CECDsaKeyPair &&) = delete;
    ~CECDsaKeyPair() = default;

    CECDsaKeyPair &operator=(_In_ const CECDsaKeyPair &) = delete;
    CECDsaKeyPair &operator=(_Inout_ CECDsaKeyPair &&) = delete;

    HRESULT Sign(_In_reads_bytes_(HASH_SIZE) const BYTE aHash[HASH_SIZE],
                 _Out_writes_bytes_(SIGNATURE_SIZE) BYTE aSignature[SIGNATURE_SIZE]);
    HRESULT Verify(_In_reads_bytes_(HASH_SIZE) const BYTE aHash[HASH_SIZE],
                   _In_reads_bytes_(SIGNATURE_SIZE) BYTE aSignature[SIGNATURE_SIZE]);

protected:
    BCRYPT_ALG_HANDLE GetAlgHandle() const;
    DWORD GetAlgMagic(_In_ BOOL bPublicKey) const;
};
