#include "IotCommClient.h"
#include "..\Crypto\Base64.h"
#include "..\Crypto\Hkdf.h"
#include "..\Crypto\Random.h"
#include "..\Crypto\Sha256.h"
#pragma warning(disable : 26819)
#include "..\ThirdParty\nlohmann_json\json.hpp"
#pragma warning(default : 26819)
#include "..\Utils\Exception2HR.h"
#include "..\Utils\RundownProtection.h"
#include "..\Utils\WindowsEvent.h"
#include <windns.h>
#include <In6addr.h>

#pragma comment(lib, "Dnsapi.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Synchronization.lib")

#define HRESULT_FROM_LASTERROR() HRESULT_FROM_WIN32(::GetLastError())
#define THROW_IF_FAILED_HR(hr) if (FAILED(hr)) { throw (HRESULT)(hr); }

#define _InterlockedRead(destination) _InterlockedOr(destination, 0)

#define STATE_CONNECTED                        0x0001
#define STATE_CONNECTING                       0x0002
#define STATE_CLEANING_UP                      0x0003
#define STATE_CONNECTION_MASK                  0x0007
#define STATE_FLAG_WS_CLOSE_OR_ERROR_CB_CALLED 0x0100
#define STATE_FLAG_MUST_CHANGE_CREDS           0x0200

// -----------------------------------------------------------------------------

static const BYTE aWsLoginV1[11] = { // "ws-login-v1"
    0x77, 0x73, 0x2D, 0x6C, 0x6F, 0x67, 0x69, 0x6E,
    0x2D, 0x76, 0x31
};

static const BYTE aWsServerAuthV1[23] = { // "ws-login-v1/server-auth"
    0x77, 0x73, 0x2D, 0x6C, 0x6F, 0x67, 0x69, 0x6E,
    0x2D, 0x76, 0x31, 0x2F, 0x73, 0x65, 0x72, 0x76,
    0x65, 0x72, 0x2D, 0x61, 0x75, 0x74, 0x68
};

static const BYTE aWsUserAuthV1[21] = { // "ws-login-v1/user-auth"
    0x77, 0x73, 0x2D, 0x6C, 0x6F, 0x67, 0x69, 0x6E,
    0x2D, 0x76, 0x31, 0x2F, 0x75, 0x73, 0x65, 0x72,
    0x2D, 0x61, 0x75, 0x74, 0x68
};

static const BYTE aAuthEnvelopeInfo[14] = { // "mx-iot-auth-v1"
    0x6D, 0x78, 0x2D, 0x69, 0x6F, 0x74, 0x2D, 0x61,
    0x75, 0x74, 0x68, 0x2D, 0x76, 0x31
};

static const BYTE aSessionMasterInfo[24] = { // "mx-iot-session-master-v1"
    0x6D, 0x78, 0x2D, 0x69, 0x6F, 0x74, 0x2D, 0x73,
    0x65, 0x73, 0x73, 0x69, 0x6F, 0x6E, 0x2D, 0x6D,
    0x61, 0x73, 0x74, 0x65, 0x72, 0x2D, 0x76, 0x31
};

static const BYTE aWsTransportInfo[12] = { // "mx-iot-ws-v1"
    0x6D, 0x78, 0x2D, 0x69, 0x6F, 0x74, 0x2D, 0x77,
    0x73, 0x2D, 0x76, 0x31
};

static const BYTE aUdpOpenV1[11] = { // "udp-open-v1"
    0x75, 0x64, 0x70, 0x2D, 0x6F, 0x70, 0x65, 0x6E,
    0x2D, 0x76, 0x31
};

static const BYTE aUdpTransportInfo[13] = { // "mx-iot-udp-v1"
    0x6D, 0x78, 0x2D, 0x69, 0x6F, 0x74, 0x2D, 0x75,
    0x64, 0x70, 0x2D, 0x76, 0x31
};

static const BYTE aWsChangeCredsV1[14] = { // "ws-chgcreds-v1"
    0x77, 0x73, 0x2D, 0x63, 0x68, 0x67, 0x63, 0x72,
    0x65, 0x64, 0x73, 0x2D, 0x76, 0x31
};

// -----------------------------------------------------------------------------

static SIZE_T ValidateUserName(_In_z_ LPCSTR szUserNameA);

static BOOL IsValidHostname(_In_z_ LPCWSTR szHostNameW);
static BOOL IsValidHostnameAndPort(_In_z_ LPCWSTR szHostNameAndPortW);
static HRESULT ExtractHostname(_In_z_ LPCWSTR szHostNameAndPortW, _Out_ std::wstring &strHostname) noexcept;

static __forceinline USHORT readUint16BE(_In_reads_bytes_(2) const BYTE *p);
static __forceinline VOID writeUint16BE(_Out_writes_bytes_(2) LPBYTE p, _In_ USHORT v);
static __forceinline DWORD readUint32BE(_In_reads_bytes_(4) const BYTE *p);
static __forceinline void writeUint32BE(_Out_writes_bytes_(4) LPBYTE p, _In_ DWORD v);

static HRESULT Utf8BytesToWstring(_In_reads_bytes_(nStrLen) LPBYTE lpStr, _In_ SIZE_T nStrLen, _Out_ std::wstring &strOut);

// -----------------------------------------------------------------------------

CIotCommClient::~CIotCommClient() noexcept
{
    Cleanup();
}

VOID CIotCommClient::OnMessageReceived(_In_ MessageReceivedCallback_t cMessageReceivedCallback) noexcept
{
    CReaderWriterMutex::CAutoExclusiveLock cLock(m_cCbRwMutex);

    m_cMessageReceivedCallback = cMessageReceivedCallback;
}

VOID CIotCommClient::OnClose(_In_ CloseCallback_t cCloseCallback) noexcept
{
    CReaderWriterMutex::CAutoExclusiveLock cLock(m_cCbRwMutex);

    m_cCloseCallback = cCloseCallback;
}

HRESULT CIotCommClient::Connect(_In_z_ LPCWSTR szHostNameW, _In_z_ LPCSTR szUserNameA,
                                _In_ const BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES],
                                _In_opt_ DWORD dwTimeoutMs, _In_opt_ HANDLE hAbortEvent,
                                _In_opt_ VerifyDeviceFingerprintCallback_t cVerifyDeviceFingerprintCallback) noexcept
{
    std::unique_ptr<CHttpClient> cHttpClient;
    CECDHKeyPair cECDH;
    CECDsaKeyPair cECDsa;
    CECDsaKeyPair cDeviceIdentity;
    CAes cAuthAes;
    CSha256 cSha256;
    CHttpClient::RequestParams_t sHttpClientRequest;
    CHttpClient::CResponseBody cRespBody;
    LPCWSTR lpszJsonHeadersW[1] = { L"Content-Type: application/json" };
    BYTE aClientNonce[NONCE_SIZE];
    BYTE aEcdhClientPublicKey[CECDHKeyPair::RAW_PUBLIC_KEY_BYTES];
    BYTE aEcdhClientPrivateKey[CECDHKeyPair::RAW_PRIVATE_KEY_BYTES];
    std::vector<BYTE> vecCookie;
    std::vector<BYTE> vecEcdhServerPublicKey;
    std::vector<BYTE> vecServerNonce;
    std::vector<BYTE> vecDevicePublicKey;
    std::vector<BYTE> vecDeviceSignature;
    DeviceFingerprintInfo_t sFingerprintInfo;
    BYTE aAuthKey[AES_KEY_LEN];
    BYTE aAuthNonce[NONCE_SIZE];
    BYTE aAuthIv[SESSION_IV_LEN];
    BYTE aSignature[CECDsaKeyPair::SIGNATURE_SIZE];
    BYTE aHash[CSha256::HASH_BYTES];
    std::vector<BYTE> vecEncryptedAuth;
    std::vector<BYTE> vecWsNonce;
    BOOL bMustChangeCredentials;
    BYTE aSharedSecret[CECDHKeyPair::SHARED_SECRET_SIZE];
    BYTE aSessionMasterKeyTemp[AES_KEY_LEN];
    BYTE aSalt[CSha256::HASH_BYTES];
    BYTE aDerivedKey[2 * AES_KEY_LEN + 2 * SESSION_IV_LEN];
    std::wstring strUrl;
    std::wstring strAuthorizationHeaderW;
    LPCWSTR lpszWsHeadersW[1]{};
    std::string strBody, strTemp;
    std::string strWsTicket;
    nlohmann::json j;
    CThread cTimedAbortThread;
    LONG nThisConnectionID;
    HRESULT hRes;

    // Validate parameters
    if (!(szHostNameW && IsValidHostnameAndPort(szHostNameW)))
    {
        return E_INVALIDARG;
    }
    if ((!szUserNameA) || !ValidateUserName(szUserNameA))
    {
        return E_INVALIDARG;
    }

    // Load private key
    hRes = cECDsa.InitFromPrivateKey(aRawPrivateKey);
    if (FAILED(hRes))
    {
        return hRes;
    }

    {
        CReaderWriterMutex::CAutoSharedLock cLock(m_cCbRwMutex);

        if (!(m_cMessageReceivedCallback && m_cCloseCallback))
        {
            return E_NOT_SET;
        }
    }

    // Close previous session if any
    Cleanup();
    RundownProt_Initialize(&m_nRundownProt);

    {
        CMutex::CAutoLock cLock(m_cMutex);

        if ((_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) != 0)
        {
            return HRESULT_FROM_WIN32(ERROR_OPERATION_IN_PROGRESS);
        }

        if (!m_cLocalAbortEvent)
        {
            hRes = m_cLocalAbortEvent.Create(TRUE);
            if (FAILED(hRes))
            {
                return hRes;
            }
        }
        else
        {
            m_cLocalAbortEvent.Reset();
        }

        do
        {
            nThisConnectionID = _InterlockedIncrement(&m_nConnectionID);
        }
        while (nThisConnectionID == 0);

        SetConnectionState(STATE_CONNECTING);
    }

    // Generate client nonce
    hRes = RandomBytes(aClientNonce, sizeof(aClientNonce));
    if (FAILED(hRes))
    {
        goto done;
    }

    // Generate client ECDH keypair
    hRes = cECDH.GenerateKeyPair(aEcdhClientPublicKey, aEcdhClientPrivateKey);
    if (FAILED(hRes))
    {
        goto done;
    }

    // Create HTTP client
    hRes = TranslateToHresult([&]()
    {
        cHttpClient = std::make_unique<CHttpClient>();
    });
    if (FAILED(hRes))
    {
        goto done;
    }

    // Set super timeout
    if (dwTimeoutMs != 0 && dwTimeoutMs != INFINITE)
    {
        hRes = cTimedAbortThread.Start([&](_In_ CThread *)-> DWORD
        {
            if (hAbortEvent)
            {
                HANDLE hEvents[2];

                hEvents[0] = cTimedAbortThread.GetExitEvent();
                hEvents[1] = hAbortEvent;
                if (::WaitForMultipleObjects(2, hEvents, FALSE, dwTimeoutMs) != WAIT_OBJECT_0)
                {
                    m_cLocalAbortEvent.Set();
                }
            }
            else
            {
                if (::WaitForSingleObject(cTimedAbortThread.GetExitEvent(), dwTimeoutMs) != WAIT_OBJECT_0)
                {
                    m_cLocalAbortEvent.Set();
                }
            }
            return 0;
        });
        if (FAILED(hRes))
        {
            goto done;
        }
    }

    // Call INIT endpoint
    hRes = TranslateToHresult([&]()
    {
        strUrl = L"http://";
        strUrl += szHostNameW;
        strUrl += L"/ws/init";

        hRes = Base64::Encode(aClientNonce, sizeof(aClientNonce), FALSE, strTemp);
        THROW_IF_FAILED_HR(hRes);
        j["clientNonce"] = strTemp;

        hRes = Base64::Encode(aEcdhClientPublicKey, sizeof(aEcdhClientPublicKey), FALSE, strTemp);
        THROW_IF_FAILED_HR(hRes);
        j["clientPublicKey"] = strTemp;

        strBody = j.dump();
    });
    if (FAILED(hRes))
    {
        goto done;
    }

    // Send request
    sHttpClientRequest = {};
    sHttpClientRequest.szUrlW = strUrl.c_str();
    sHttpClientRequest.szMethodW = L"POST";
    sHttpClientRequest.lpszHeadersW = lpszJsonHeadersW;
    sHttpClientRequest.nHeadersCount = _countof(lpszJsonHeadersW);
    sHttpClientRequest.lpBody = strBody.c_str();
    sHttpClientRequest.nBodyLen = strBody.length();
    sHttpClientRequest.hAbortEvent = m_cLocalAbortEvent.GetEvent();
    hRes = cHttpClient->DoRequest(sHttpClientRequest);
    if (SUCCEEDED(hRes))
    {
        if (cHttpClient->GetStatusCode() == 200)
        {
            hRes = cHttpClient->GetResponseBody(cRespBody);
            if (SUCCEEDED(hRes))
            {
                hRes = TranslateToHresult([&]()
                {
                    std::string strTemp;
                    HRESULT hRes;

                    j = nlohmann::json::parse(
                        reinterpret_cast<const char *>(cRespBody->data()),
                        reinterpret_cast<const char *>(cRespBody->data()) + cRespBody->size()
                    );

                    if (!(j.contains("token") && j["token"].is_string() &&
                          j.contains("serverNonce") && j["serverNonce"].is_string() &&
                          j.contains("serverPublicKey") && j["serverPublicKey"].is_string() &&
                          j.contains("devicePublicKey") && j["devicePublicKey"].is_string() &&
                          j.contains("deviceSignature") && j["deviceSignature"].is_string()))
                    {
                        throw IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
                    }

                    strTemp = j["token"].get<std::string>();
                    hRes = Base64::Decode(strTemp.data(), strTemp.length(), FALSE, vecCookie);
                    THROW_IF_FAILED_HR(hRes);
                    if (vecCookie.size() != COOKIE_SIZE)
                    {
                        throw IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
                    }

                    strTemp = j["serverNonce"].get<std::string>();
                    hRes = Base64::Decode(strTemp.data(), strTemp.length(), FALSE, vecServerNonce);
                    THROW_IF_FAILED_HR(hRes);
                    if (vecServerNonce.size() != NONCE_SIZE)
                    {
                        throw IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
                    }

                    strTemp = j["serverPublicKey"].get<std::string>();
                    hRes = Base64::Decode(strTemp.data(), strTemp.length(), FALSE, vecEcdhServerPublicKey);
                    THROW_IF_FAILED_HR(hRes);
                    if (vecEcdhServerPublicKey.size() != CECDHKeyPair::RAW_PUBLIC_KEY_BYTES)
                    {
                        throw IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
                    }

                    strTemp = j["devicePublicKey"].get<std::string>();
                    hRes = Base64::Decode(strTemp.data(), strTemp.length(), FALSE, vecDevicePublicKey);
                    THROW_IF_FAILED_HR(hRes);
                    if (vecDevicePublicKey.size() != CECDsaKeyPair::RAW_PUBLIC_KEY_BYTES)
                    {
                        throw IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
                    }

                    strTemp = j["deviceSignature"].get<std::string>();
                    hRes = Base64::Decode(strTemp.data(), strTemp.length(), FALSE, vecDeviceSignature);
                    THROW_IF_FAILED_HR(hRes);
                    if (vecDeviceSignature.size() != CECDsaKeyPair::SIGNATURE_SIZE)
                    {
                        throw IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
                    }
                });
            }
        }
        else
        {
            hRes = IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
        }
    }
    if (FAILED(hRes))
    {
        goto done;
    }
    if ((_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) != STATE_CONNECTING ||
        nThisConnectionID != _InterlockedRead(&m_nConnectionID))
    {
        hRes = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
        goto done;
    }

    // Verify the server-auth transcript using the device identity key.
    hRes = cDeviceIdentity.InitFromPublicKey(vecDevicePublicKey.data());
    if (FAILED(hRes))
    {
        hRes = IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
        goto done;
    }

    cSha256.Init();
    cSha256.Update(aWsServerAuthV1, sizeof(aWsServerAuthV1));
    cSha256.Update(aEcdhClientPublicKey, sizeof(aEcdhClientPublicKey));
    cSha256.Update(vecEcdhServerPublicKey.data(), vecEcdhServerPublicKey.size());
    cSha256.Update(aClientNonce, sizeof(aClientNonce));
    cSha256.Update(vecServerNonce.data(), vecServerNonce.size());
    cSha256.Update(vecCookie.data(), vecCookie.size());
    hRes = cSha256.Finalize();
    if (SUCCEEDED(hRes))
    {
        memcpy(aHash, cSha256.GetHash(), sizeof(aHash));
        hRes = cDeviceIdentity.Verify(aHash, vecDeviceSignature.data());
        if (hRes == S_FALSE)
        {
            hRes = IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
        }
    }
    if (FAILED(hRes))
    {
        goto done;
    }

    memcpy(sFingerprintInfo.aDevicePublicKey, vecDevicePublicKey.data(), sizeof(sFingerprintInfo.aDevicePublicKey));
    cSha256.Init();
    cSha256.Update(vecDevicePublicKey.data(), vecDevicePublicKey.size());
    hRes = cSha256.Finalize();
    if (FAILED(hRes))
    {
        goto done;
    }
    memcpy(aHash, cSha256.GetHash(), sizeof(aHash));
    for (SIZE_T i = 0; i < sizeof(aHash); i++)
    {
        static constexpr WCHAR szHexCharsW[] = L"0123456789ABCDEF";

        sFingerprintInfo.szFingerprintW[2 * i] = szHexCharsW[(aHash[i] >> 4) & 0x0F];
        sFingerprintInfo.szFingerprintW[2 * i + 1] = szHexCharsW[aHash[i] & 0x0F];
    }
    sFingerprintInfo.szFingerprintW[_countof(sFingerprintInfo.szFingerprintW) - 1] = 0;

    if (cVerifyDeviceFingerprintCallback)
    {
        hRes = cVerifyDeviceFingerprintCallback(sFingerprintInfo, m_cLocalAbortEvent.GetEvent());
        if (FAILED(hRes))
        {
            goto done;
        }
    }

    // Compute shared secret and derive the auth envelope key.
    hRes = cECDH.ComputeSharedSecret(vecEcdhServerPublicKey.data(), aSharedSecret);
    if (FAILED(hRes))
    {
        goto done;
    }

    // Build session salt = SHA256("ws-login-v1" || s_nonce || c_nonce || cookie)
    cSha256.Init();
    cSha256.Update(aWsLoginV1, sizeof(aWsLoginV1));
    cSha256.Update(vecServerNonce.data(), vecServerNonce.size());
    cSha256.Update(aClientNonce, sizeof(aClientNonce));
    cSha256.Update(vecCookie.data(), vecCookie.size());
    hRes = cSha256.Finalize();
    if (FAILED(hRes))
    {
        goto done;
    }
    memcpy(aSalt, cSha256.GetHash(), sizeof(aSalt));

    hRes = Hkdf::Sha256DeriveKey(aSharedSecret, sizeof(aSharedSecret), aSalt, sizeof(aSalt), aAuthEnvelopeInfo,
                                 sizeof(aAuthEnvelopeInfo), aAuthKey, sizeof(aAuthKey));
    if (FAILED(hRes))
    {
        goto done;
    }

    hRes = cAuthAes.SetKey(aAuthKey, sizeof(aAuthKey));
    if (FAILED(hRes))
    {
        goto done;
    }

    // Generate auth nonce
    hRes = RandomBytes(aAuthNonce, sizeof(aAuthNonce));
    if (FAILED(hRes))
    {
        goto done;
    }

    hRes = RandomBytes(aAuthIv, sizeof(aAuthIv));
    if (FAILED(hRes))
    {
        goto done;
    }

    // t = "ws-login-v1/user-auth" || c_pk || s_pk || c_nonce || s_nonce || cookie || auth_nonce || user_name
    cSha256.Init();
    cSha256.Update(aWsUserAuthV1, sizeof(aWsUserAuthV1));
    cSha256.Update(aEcdhClientPublicKey, sizeof(aEcdhClientPublicKey));
    cSha256.Update(vecEcdhServerPublicKey.data(), vecEcdhServerPublicKey.size());
    cSha256.Update(aClientNonce, sizeof(aClientNonce));
    cSha256.Update(vecServerNonce.data(), vecServerNonce.size());
    cSha256.Update(vecCookie.data(), vecCookie.size());
    cSha256.Update(aAuthNonce, sizeof(aAuthNonce));
    cSha256.Update(szUserNameA, strlen(szUserNameA));
    hRes = cSha256.Finalize();
    // Sign T (SHA-256 is applied internally)
    if (SUCCEEDED(hRes))
    {
        memcpy(aHash, cSha256.GetHash(), CSha256::HASH_BYTES);
        hRes = cECDsa.Sign(aHash, aSignature);
    }
    if (FAILED(hRes))
    {
        goto done;
    }

    // Call AUTH endpoint
    hRes = TranslateToHresult([&]()
    {
        strUrl = L"http://";
        strUrl += szHostNameW;
        strUrl += L"/ws/auth";

        j = {};
        hRes = Base64::Encode(aSignature, sizeof(aSignature), FALSE, strTemp);
        THROW_IF_FAILED_HR(hRes);
        j["signature"] = strTemp;

        hRes = Base64::Encode(aAuthNonce, sizeof(aAuthNonce), FALSE, strTemp);
        THROW_IF_FAILED_HR(hRes);
        j["authNonce"] = strTemp;

        j["userName"] = szUserNameA;

        strBody = j.dump();

        hRes = TranslateToHresult([&]()
        {
            vecEncryptedAuth.resize(strBody.length() + CAes::GCM_TAG_LEN);
        });
        THROW_IF_FAILED_HR(hRes);

        hRes = cAuthAes.Encrypt(strBody.data(), strBody.length(), aAuthIv, sizeof(aAuthIv), nullptr, 0, vecEncryptedAuth.data());
        THROW_IF_FAILED_HR(hRes);

        j = {};
        hRes = Base64::Encode(aAuthIv, sizeof(aAuthIv), FALSE, strTemp);
        THROW_IF_FAILED_HR(hRes);
        j["authIv"] = strTemp;

        hRes = Base64::Encode(vecEncryptedAuth.data(), vecEncryptedAuth.size(), FALSE, strTemp);
        THROW_IF_FAILED_HR(hRes);
        j["encryptedAuth"] = strTemp;

        hRes = Base64::Encode(vecCookie.data(), vecCookie.size(), FALSE, strTemp);
        THROW_IF_FAILED_HR(hRes);
        j["token"] = strTemp;

        strBody = j.dump();
    });
    if (FAILED(hRes))
    {
        goto done;
    }

    sHttpClientRequest = {};
    sHttpClientRequest.szUrlW = strUrl.c_str();
    sHttpClientRequest.szMethodW = L"POST";
    sHttpClientRequest.lpszHeadersW = lpszJsonHeadersW;
    sHttpClientRequest.nHeadersCount = _countof(lpszJsonHeadersW);
    sHttpClientRequest.lpBody = strBody.c_str();
    sHttpClientRequest.nBodyLen = strBody.length();
    sHttpClientRequest.hAbortEvent = m_cLocalAbortEvent.GetEvent();
    hRes = cHttpClient->DoRequest(sHttpClientRequest);
    if (SUCCEEDED(hRes))
    {
        if (cHttpClient->GetStatusCode() == 200)
        {
            hRes = cHttpClient->GetResponseBody(cRespBody);
            if (SUCCEEDED(hRes))
            {
                hRes = TranslateToHresult([&]()
                {
                    std::string strTemp;
                    HRESULT hRes;

                    j = nlohmann::json::parse(
                        reinterpret_cast<const char *>(cRespBody->data()),
                        reinterpret_cast<const char *>(cRespBody->data()) + cRespBody->size()
                    );

                    if (!(j.contains("wsNonce") && j["wsNonce"].is_string() &&
                          j.contains("mustChangeCredentials") && j["mustChangeCredentials"].is_boolean() &&
                          j.contains("wsTicket") && j["wsTicket"].is_string()))
                    {
                        throw IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
                    }

                    strTemp = j["wsNonce"].get<std::string>();
                    hRes = Base64::Decode(strTemp.data(), strTemp.length(), FALSE, vecWsNonce);
                    THROW_IF_FAILED_HR(hRes);
                    if (vecWsNonce.size() != NONCE_SIZE)
                    {
                        throw IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
                    }

                    strWsTicket = j["wsTicket"].get<std::string>();
                    if (strWsTicket.empty())
                    {
                        throw IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
                    }

                    bMustChangeCredentials = j["mustChangeCredentials"].get<bool>() ? TRUE : FALSE;
                });
            }
        }
        else if (cHttpClient->GetStatusCode() == 401)
        {
            hRes = IOTCOMM_E_AUTH_FAILED;
        }
        else
        {
            hRes = IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
        }
    }
    if (FAILED(hRes))
    {
        goto done;
    }
    if ((_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) != STATE_CONNECTING ||
        nThisConnectionID != _InterlockedRead(&m_nConnectionID))
    {
        hRes = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
        goto done;
    }

    // Derive retained session master key
    hRes = Hkdf::Sha256DeriveKey(aSharedSecret, sizeof(aSharedSecret), aSalt, sizeof(aSalt), aSessionMasterInfo,
                                 sizeof(aSessionMasterInfo), aSessionMasterKeyTemp, sizeof(aSessionMasterKeyTemp));
    if (FAILED(hRes))
    {
        goto done;
    }

    // Build WebSocket salt = SHA256("ws-login-v1" || s_nonce || c_nonce || cookie || ws_nonce)
    cSha256.Init();
    cSha256.Update(aWsLoginV1, sizeof(aWsLoginV1));
    cSha256.Update(vecServerNonce.data(), vecServerNonce.size());
    cSha256.Update(aClientNonce, sizeof(aClientNonce));
    cSha256.Update(vecCookie.data(), vecCookie.size());
    cSha256.Update(vecWsNonce.data(), vecWsNonce.size());
    hRes = cSha256.Finalize();
    if (FAILED(hRes))
    {
        goto done;
    }
    memcpy(aSalt, cSha256.GetHash(), CSha256::HASH_BYTES);

    // Derive WebSocket transport keys
    hRes = Hkdf::Sha256DeriveKey(aSessionMasterKeyTemp, sizeof(aSessionMasterKeyTemp), aSalt, sizeof(aSalt), aWsTransportInfo,
                                 sizeof(aWsTransportInfo), aDerivedKey, sizeof(aDerivedKey));
    if (FAILED(hRes))
    {
        goto done;
    }

    // Create websocket and connect
    hRes = TranslateToHresult([&]()
    {
        strUrl = L"ws://";
        strUrl += szHostNameW;
        strUrl += L"/ws";

        strAuthorizationHeaderW = L"Authorization: Bearer ";
        strAuthorizationHeaderW += std::wstring(strWsTicket.begin(), strWsTicket.end());
        lpszWsHeadersW[0] = strAuthorizationHeaderW.c_str();
    });
    if (FAILED(hRes))
    {
        goto done;
    }

    sHttpClientRequest = {};
    sHttpClientRequest.szUrlW = strUrl.c_str();
    sHttpClientRequest.lpszHeadersW = lpszWsHeadersW;
    sHttpClientRequest.nHeadersCount = _countof(lpszWsHeadersW);
    sHttpClientRequest.cWsDataReceivedCallback = [this](_In_ const BYTE *lpData, _In_ SIZE_T nDataLen, _In_ BOOL bIsText)
    {
        this->OnWsMessage(lpData, nDataLen, bIsText);
    };
    sHttpClientRequest.cWsClosedCallback = [this](_In_ USHORT nCode, _In_z_ LPCSTR szReasonA)
    {
        this->OnWsClose(nCode, szReasonA);
    };
    sHttpClientRequest.cWsErrorCallback = [this](_In_ HRESULT hRes, _In_ BOOL bIsRead)
    {
        this->OnWsError(hRes, bIsRead);
    };
    sHttpClientRequest.hAbortEvent = m_cLocalAbortEvent ? m_cLocalAbortEvent.GetEvent() : hAbortEvent;
    hRes = cHttpClient->DoRequest(sHttpClientRequest);
    if (hRes == S_FALSE)
    {
        hRes = IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE;
    }
    if (FAILED(hRes))
    {
        goto done;
    }

    {
        CMutex::CAutoLock cLock(m_cMutex);

        if ((_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) == STATE_CONNECTING &&
            nThisConnectionID == _InterlockedRead(&m_nConnectionID))
        {
            // Init internals
            hRes = m_cClientAes.SetKey(aDerivedKey, AES_KEY_LEN);
            if (SUCCEEDED(hRes))
            {
                hRes = m_cServerAes.SetKey(aDerivedKey + AES_KEY_LEN, AES_KEY_LEN);
            }
            if (SUCCEEDED(hRes))
            {
                memcpy(m_aClientBaseIV, aDerivedKey + 2 * AES_KEY_LEN, SESSION_IV_LEN);
                memcpy(m_aServerBaseIV, aDerivedKey + 2 * AES_KEY_LEN + SESSION_IV_LEN, SESSION_IV_LEN);
                memcpy(this->m_aSessionMasterKey, aSessionMasterKeyTemp, sizeof(this->m_aSessionMasterKey));
                memcpy(m_aWsNonce, vecWsNonce.data(), NONCE_SIZE);
                hRes = TranslateToHresult([&]()
                {
                    m_strHostNameW = szHostNameW;
                });
                if (FAILED(hRes))
                {
                    cHttpClient->WsForceClose();
                    goto done;
                }
                m_dwNextRxCounter = 1;
                m_dwNextTxCounter = 1;

                // Complete websocket link
                m_cWsClient = std::move(cHttpClient);

                if (bMustChangeCredentials)
                {
                    _InterlockedOr(&m_nState, STATE_FLAG_MUST_CHANGE_CREDS);
                }
                SetConnectionState(STATE_CONNECTED);

                hRes = m_cWsClient->WsInitiate();
            }
            else
            {
                cHttpClient->WsForceClose();
            }

        }
        else
        {
            hRes = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
        }
    }

done:
    if (FAILED(hRes))
    {
        Cleanup();
    }

    // Cleanup
    sHttpClientRequest = {};
    if (cRespBody && (!cRespBody->empty()))
    {
        memset(cRespBody->data(), 0, cRespBody->size());
        cRespBody->clear();
    }
    memset(aClientNonce, 0, sizeof(aClientNonce));
    memset(aEcdhClientPublicKey, 0, sizeof(aEcdhClientPublicKey));
    memset(aEcdhClientPrivateKey, 0, sizeof(aEcdhClientPrivateKey));
    if (!vecCookie.empty())
    {
        memset(vecCookie.data(), 0, vecCookie.size());
        vecCookie.clear();
    }
    if (!vecEcdhServerPublicKey.empty())
    {
        memset(vecEcdhServerPublicKey.data(), 0, vecEcdhServerPublicKey.size());
        vecEcdhServerPublicKey.clear();
    }
    if (!vecServerNonce.empty())
    {
        memset(vecServerNonce.data(), 0, vecServerNonce.size());
        vecServerNonce.clear();
    }
    if (!vecDevicePublicKey.empty())
    {
        memset(vecDevicePublicKey.data(), 0, vecDevicePublicKey.size());
        vecDevicePublicKey.clear();
    }
    if (!vecDeviceSignature.empty())
    {
        memset(vecDeviceSignature.data(), 0, vecDeviceSignature.size());
        vecDeviceSignature.clear();
    }
    memset(&sFingerprintInfo, 0, sizeof(sFingerprintInfo));
    memset(aAuthKey, 0, sizeof(aAuthKey));
    memset(aAuthNonce, 0, sizeof(aAuthNonce));
    memset(aAuthIv, 0, sizeof(aAuthIv));
    memset(aSignature, 0, sizeof(aSignature));
    memset(aHash, 0, sizeof(aHash));
    if (!vecEncryptedAuth.empty())
    {
        memset(vecEncryptedAuth.data(), 0, vecEncryptedAuth.size());
        vecEncryptedAuth.clear();
    }
    if (!vecWsNonce.empty())
    {
        memset(vecWsNonce.data(), 0, vecWsNonce.size());
        vecWsNonce.clear();
    }
    memset(aSharedSecret, 0, sizeof(aSharedSecret));
    memset(aSessionMasterKeyTemp, 0, sizeof(aSessionMasterKeyTemp));
    memset(aSalt, 0, sizeof(aSalt));
    memset(aDerivedKey, 0, sizeof(aDerivedKey));
    if (!strUrl.empty())
    {
        memset(strUrl.data(), 0, strUrl.length() * sizeof(wchar_t));
    }
    if (!strBody.empty())
    {
        memset(strBody.data(), 0, strBody.length());
    }
    if (!strTemp.empty())
    {
        memset(strTemp.data(), 0, strTemp.length());
    }
    if (!strWsTicket.empty())
    {
        memset(strWsTicket.data(), 0, strWsTicket.length());
    }
    if (!strAuthorizationHeaderW.empty())
    {
        memset(strAuthorizationHeaderW.data(), 0, strAuthorizationHeaderW.length() * sizeof(wchar_t));
    }
    return hRes;
}

VOID CIotCommClient::Close(_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA) noexcept
{
    {
        CAutoRundownProtection cAutoRundownProt(&m_nRundownProt);

        if (cAutoRundownProt.IsAcquired() && (_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) == STATE_CONNECTED)
        {
            m_cWsClient->WsClose(nCode, szReasonA);
        }
    }

    Cleanup();
}

VOID CIotCommClient::ForceClose() noexcept
{
    Cleanup();
}

CIotCommClient::LinkState_t CIotCommClient::GetLinkState() const noexcept
{
    switch (_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK)
    {
        case STATE_CONNECTING:
            return LinkStateConnecting;
        case STATE_CONNECTED:
            return LinkStateConnected;
        case STATE_CLEANING_UP:
            return LinkStateDisconnecting;
    }
    return LinkStateDisconnected;
}

BOOL CIotCommClient::MustChangeCredentials() const noexcept
{
    return ((_InterlockedRead(&m_nState) & (STATE_CONNECTION_MASK | STATE_FLAG_MUST_CHANGE_CREDS)) ==
            (STATE_CONNECTED | STATE_FLAG_MUST_CHANGE_CREDS))
           ? TRUE : FALSE;
}

HRESULT CIotCommClient::SendCommand(_In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload, _In_ SIZE_T nPayloadLen)
{
    // Verify parameters
    if ((!lpPayload) || nPayloadLen == 0)
    {
        return E_INVALIDARG;
    }

    {
        CAutoRundownProtection cAutoRundownProt(&m_nRundownProt);

        if (cAutoRundownProt.IsAcquired() && (_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) == STATE_CONNECTED)
        {
            CMutex::CAutoLock cLock(m_cMutex);
            HRESULT hRes;

            // Try to send
            hRes = EncryptAndSend(nCmd, lpPayload, nPayloadLen);
            if (FAILED(hRes))
            {
                return hRes;
            }
        }
        else
        {
            return E_NOT_SET;
        }
    }

    // Done
    return S_OK;
}

HRESULT CIotCommClient::SendCommandAndWaitReply(_In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload,
                                                _In_ SIZE_T nPayloadLen, _Out_ std::vector<BYTE> &reply) noexcept
{
    PendingReply_t sPendingReply = {};
    DWORD dwTxCounter;
    HRESULT hRes;

    reply.clear();

    // Verify parameters
    if (nPayloadLen > 0 && (!lpPayload))
    {
        return E_INVALIDARG;
    }

    {
        CAutoRundownProtection cAutoRundownProt(&m_nRundownProt);

        if (cAutoRundownProt.IsAcquired() && (_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) == STATE_CONNECTED)
        {
            CMutex::CAutoLock cLock(m_cMutex);

            dwTxCounter = GetNextTxCounter();
            hRes = TranslateToHresult([&]
            {
                m_mapPendingReplies.insert_or_assign(dwTxCounter, &sPendingReply);
            });

            // Try to send
            hRes = EncryptAndSend(nCmd, lpPayload, nPayloadLen);
            if (FAILED(hRes))
            {
                m_mapPendingReplies.erase(dwTxCounter);
                return hRes;
            }
        }
        else
        {
            return E_NOT_SET;
        }
    }

    // Wait until the pending reply is fulfilled
    for (;;)
    {
        HRESULT hrExpected = S_FALSE;

        hRes = _InterlockedOr(&sPendingReply.hRes, 0);
        if (hRes != hrExpected)
        {
            break;
        }
        ::WaitOnAddress(&sPendingReply.hRes, &hrExpected, sizeof(hrExpected), INFINITE);
    }
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Extract reply
    reply = std::move(sPendingReply.data);

    // Done
    return S_OK;
}

CIotCommClient::CCommandResult CIotCommClient::OpenUdpChannel()
{
    BYTE aClientUdpNonce[NONCE_SIZE];
    BYTE aUdpSalt[CSha256::HASH_BYTES];
    BYTE aDerivedKey[2 * AES_KEY_LEN + 2 * SESSION_IV_LEN];
    BYTE aServerUdpNonce[NONCE_SIZE];
    std::vector<BYTE> vecReply;
    std::wstring strHostOnlyW;
    addrinfoW sHints = {};
    PADDRINFOW lpAddrInfoW = nullptr;
    SOCKET hUdpSocketLocal = INVALID_SOCKET;
    USHORT nUdpPort;
    DWORD dwUdpConnectionIdLocal;
    INT nAddrInfoRes;
    WCHAR szUdpPortW[6];
    CSha256 cSha256;
    HRESULT hRes;

    if (FAILED(RandomBytes(aClientUdpNonce, sizeof(aClientUdpNonce))))
    {
        return CCommandResult(E_FAIL);
    }

    hRes = SendCommandAndWaitReply(CMD_UDP_OPEN, aClientUdpNonce, sizeof(aClientUdpNonce), vecReply);
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }
    if (vecReply.size() < sizeof(DWORD) || readUint32BE(vecReply.data()) != 0)
    {
        return BuildCommandResultFromStandardReply(vecReply);
    }
    if (vecReply.size() != sizeof(DWORD) + 2 + sizeof(DWORD) + NONCE_SIZE)
    {
        return CCommandResult(E_INVALID_PROTOCOL_FORMAT);
    }

    nUdpPort = readUint16BE(vecReply.data() + sizeof(DWORD));
    dwUdpConnectionIdLocal = readUint32BE(vecReply.data() + sizeof(DWORD) + 2);
    memcpy(aServerUdpNonce, vecReply.data() + sizeof(DWORD) + 2 + sizeof(DWORD), sizeof(aServerUdpNonce));
    if (nUdpPort == 0 || dwUdpConnectionIdLocal == 0)
    {
        return CCommandResult(E_INVALID_PROTOCOL_FORMAT);
    }

    {
        CMutex::CAutoLock cLock(m_cMutex);

        if ((_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) != STATE_CONNECTED || m_strHostNameW.empty())
        {
            return CCommandResult(E_NOT_SET);
        }

        hRes = ExtractHostname(m_strHostNameW.c_str(), strHostOnlyW);
        if (FAILED(hRes))
        {
            return CCommandResult(hRes);
        }

        cSha256.Init();
        cSha256.Update(aUdpOpenV1, sizeof(aUdpOpenV1));
        cSha256.Update(aClientUdpNonce, sizeof(aClientUdpNonce));
        cSha256.Update(aServerUdpNonce, sizeof(aServerUdpNonce));
        cSha256.Update(vecReply.data() + sizeof(DWORD) + 2, sizeof(DWORD));
        hRes = cSha256.Finalize();
        if (FAILED(hRes))
        {
            return CCommandResult(hRes);
        }
        memcpy(aUdpSalt, cSha256.GetHash(), sizeof(aUdpSalt));

        hRes = Hkdf::Sha256DeriveKey(m_aSessionMasterKey, sizeof(m_aSessionMasterKey), aUdpSalt, sizeof(aUdpSalt), aUdpTransportInfo,
                                     sizeof(aUdpTransportInfo), aDerivedKey, sizeof(aDerivedKey));
        if (FAILED(hRes))
        {
            return CCommandResult(hRes);
        }

        swprintf_s(szUdpPortW, L"%u", nUdpPort);
        sHints.ai_family = AF_UNSPEC;
        sHints.ai_socktype = SOCK_DGRAM;
        sHints.ai_protocol = IPPROTO_UDP;
        nAddrInfoRes = ::GetAddrInfoW(strHostOnlyW.c_str(), szUdpPortW, &sHints, &lpAddrInfoW);
        if (nAddrInfoRes != 0 || !lpAddrInfoW)
        {
            return CCommandResult(HRESULT_FROM_WIN32(nAddrInfoRes));
        }

        hUdpSocketLocal = ::socket(lpAddrInfoW->ai_family, lpAddrInfoW->ai_socktype, lpAddrInfoW->ai_protocol);
        if (hUdpSocketLocal == INVALID_SOCKET)
        {
            hRes = HRESULT_FROM_WIN32(WSAGetLastError());
            goto done;
        }
        if (::connect(hUdpSocketLocal, lpAddrInfoW->ai_addr, static_cast<int>(lpAddrInfoW->ai_addrlen)) == SOCKET_ERROR)
        {
            hRes = HRESULT_FROM_WIN32(WSAGetLastError());
            goto done;
        }

        ResetUdpState();

        hRes = m_cUdpClientAes.SetKey(aDerivedKey, AES_KEY_LEN);
        if (FAILED(hRes))
        {
            goto done;
        }

        m_hUdpSocket = hUdpSocketLocal;
        hUdpSocketLocal = INVALID_SOCKET;
        m_dwUdpConnectionId = dwUdpConnectionIdLocal;
        m_dwNextUdpTxCounter = 1;
        memcpy(m_aUdpClientBaseIV, aDerivedKey + 2 * AES_KEY_LEN, SESSION_IV_LEN);
    }

    hRes = S_OK;

done:
    if (lpAddrInfoW)
    {
        ::FreeAddrInfoW(lpAddrInfoW);
    }
    if (hUdpSocketLocal != INVALID_SOCKET)
    {
        ::closesocket(hUdpSocketLocal);
    }
    memset(aClientUdpNonce, 0, sizeof(aClientUdpNonce));
    memset(aUdpSalt, 0, sizeof(aUdpSalt));
    memset(aDerivedKey, 0, sizeof(aDerivedKey));
    memset(aServerUdpNonce, 0, sizeof(aServerUdpNonce));
    return CCommandResult(hRes);
}

HRESULT CIotCommClient::SendUdpCommand(_In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload, _In_ SIZE_T nPayloadLen)
{
    if ((!lpPayload) || nPayloadLen == 0)
    {
        return E_INVALIDARG;
    }

    {
        CAutoRundownProtection cAutoRundownProt(&m_nRundownProt);

        if (cAutoRundownProt.IsAcquired() && (_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) == STATE_CONNECTED)
        {
            CMutex::CAutoLock cLock(m_cMutex);

            if (m_hUdpSocket == INVALID_SOCKET || m_dwUdpConnectionId == 0 || m_dwNextUdpTxCounter == 0)
            {
                return E_NOT_SET;
            }
            return EncryptAndSendUdp(nCmd, lpPayload, nPayloadLen);
        }
    }

    return E_NOT_SET;
}

CIotCommClient::CCommandResult CIotCommClient::CreateUserCommand(_In_z_ LPCSTR szUserNameA,
                                                                 _In_reads_bytes_(nPublicKeyLen) LPCVOID lpPublicKey,
                                                                 _In_ SIZE_T nPublicKeyLen,
                                                                 _In_opt_ BYTE uFlags)
{
    CECDsaKeyPair cECDsa;
    SIZE_T nUserNameLen;
    std::vector<BYTE> vecPayload;
    std::vector<BYTE> vecReply;
    HRESULT hRes;

    // Verify parameters
    if ((!szUserNameA) || (!lpPublicKey) || nPublicKeyLen != CP256KeyPair::RAW_PUBLIC_KEY_BYTES)
    {
        return CCommandResult(E_INVALIDARG);
    }
    if ((uFlags & (~USER_CREATE_FLAGS_MASK)) != 0)
    {
        return CCommandResult(E_INVALIDARG);
    }
    nUserNameLen = ValidateUserName(szUserNameA);
    if (nUserNameLen == 0)
    {
        return CCommandResult(E_INVALIDARG);
    }
    if (FAILED(cECDsa.InitFromPublicKey(reinterpret_cast<const BYTE *>(lpPublicKey))))
    {
        return CCommandResult(E_INVALIDARG);
    }

    // Create payload
    hRes = TranslateToHresult([&]()
    {
        vecPayload.push_back(uFlags);
        vecPayload.insert(vecPayload.end(), szUserNameA, szUserNameA + nUserNameLen);
        vecPayload.push_back(0x00);
        vecPayload.insert(vecPayload.end(), reinterpret_cast<const BYTE *>(lpPublicKey),
                          reinterpret_cast<const BYTE *>(lpPublicKey) + nPublicKeyLen);
    });
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }

    // Run command
    hRes = SendCommandAndWaitReply(CMD_CREATE_USER, vecPayload.data(), vecPayload.size(), vecReply);
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }

    // Done
    return BuildCommandResultFromStandardReply(vecReply);
}

CIotCommClient::CCommandResult CIotCommClient::DeleteUserCommand(_In_z_ LPCSTR szUserNameA)
{
    SIZE_T nUserNameLen;
    std::vector<BYTE> vecPayload;
    std::vector<BYTE> vecReply;
    HRESULT hRes;

    // Verify parameters
    if (!szUserNameA)
    {
        return CCommandResult(E_INVALIDARG);
    }
    nUserNameLen = ValidateUserName(szUserNameA);
    if (nUserNameLen == 0)
    {
        return CCommandResult(E_INVALIDARG);
    }

    // Create payload
    hRes = TranslateToHresult([&]()
    {
        vecPayload.insert(vecPayload.end(), szUserNameA, szUserNameA + nUserNameLen);
        vecPayload.push_back(0x00);
    });
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }

    // Run command
    hRes = SendCommandAndWaitReply(CMD_DELETE_USER, vecPayload.data(), vecPayload.size(), vecReply);
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }

    // Done
    return BuildCommandResultFromStandardReply(vecReply);
}

CIotCommClient::CCommandResult CIotCommClient::ResetUserCredentialsCommand(_In_z_ LPCSTR szUserNameA,
                                                                           _In_reads_bytes_(nPublicKeyLen) LPCVOID lpPublicKey,
                                                                           _In_ SIZE_T nPublicKeyLen)
{
    CECDsaKeyPair cECDsa;
    SIZE_T nUserNameLen;
    std::vector<BYTE> vecPayload;
    std::vector<BYTE> vecReply;
    HRESULT hRes;

    // Verify parameters
    if ((!szUserNameA) || (!lpPublicKey) || nPublicKeyLen != CP256KeyPair::RAW_PUBLIC_KEY_BYTES)
    {
        return CCommandResult(E_INVALIDARG);
    }
    nUserNameLen = ValidateUserName(szUserNameA);
    if (nUserNameLen == 0)
    {
        return CCommandResult(E_INVALIDARG);
    }
    if (FAILED(cECDsa.InitFromPublicKey(reinterpret_cast<const BYTE *>(lpPublicKey))))
    {
        return CCommandResult(E_INVALIDARG);
    }

    // Create payload
    hRes = TranslateToHresult([&]()
    {
        vecPayload.insert(vecPayload.end(), szUserNameA, szUserNameA + nUserNameLen);
        vecPayload.push_back(0x00);
        vecPayload.insert(vecPayload.end(), reinterpret_cast<const BYTE *>(lpPublicKey),
                          reinterpret_cast<const BYTE *>(lpPublicKey) + nPublicKeyLen);
    });
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }

    // Run command
    hRes = SendCommandAndWaitReply(CMD_RESET_USER_CREDENTIALS, vecPayload.data(), vecPayload.size(), vecReply);
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }

    // Done
    return BuildCommandResultFromStandardReply(vecReply);
}

CIotCommClient::CCommandResult CIotCommClient::ChangeUserCredentialsCommand(_In_reads_bytes_(nOldPrivateKeyLen) LPCVOID lpOldPrivateKey,
                                                                            _In_ SIZE_T nOldPrivateKeyLen,
                                                                            _In_reads_bytes_(nNewPublicKeyLen) LPCVOID lpNewPublicKey,
                                                                            _In_ SIZE_T nNewPublicKeyLen)
{
    CECDsaKeyPair cECDsa;
    CSha256 cSha256;
    BYTE aSignature[CECDsaKeyPair::SIGNATURE_SIZE];
    BYTE aHash[CSha256::HASH_BYTES];
    LONG nOrigConnectionID;
    std::vector<BYTE> vecPayload;
    std::vector<BYTE> vecReply;
    CCommandResult cCmdResult;
    HRESULT hRes;

    // Verify parameters
    if ((!lpOldPrivateKey) || nOldPrivateKeyLen != CP256KeyPair::RAW_PRIVATE_KEY_BYTES ||
        (!lpNewPublicKey) || nNewPublicKeyLen != CP256KeyPair::RAW_PUBLIC_KEY_BYTES)
    {
        return CCommandResult(E_INVALIDARG);
    }
    if (FAILED(cECDsa.InitFromPublicKey(reinterpret_cast<const BYTE *>(lpNewPublicKey))))
    {
        return CCommandResult(E_INVALIDARG);
    }
    if (FAILED(cECDsa.InitFromPrivateKey(reinterpret_cast<const BYTE *>(lpOldPrivateKey))))
    {
        return CCommandResult(E_INVALIDARG);
    }

    // Store connection info for later use
    if ((_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) != STATE_CONNECTED)
    {
        return CCommandResult(E_NOT_SET);
    }
    nOrigConnectionID = _InterlockedOr(&m_nConnectionID, 0);

    // t = "ws-chgcreds-v1" || publicKey || ws_nonce
    cSha256.Init();
    cSha256.Update(aWsChangeCredsV1, sizeof(aWsChangeCredsV1));
    cSha256.Update(lpNewPublicKey, nNewPublicKeyLen);
    cSha256.Update(m_aWsNonce, sizeof(m_aWsNonce));
    hRes = cSha256.Finalize();
    // Sign T (SHA-256 is applied internally)
    if (SUCCEEDED(hRes))
    {
        memcpy(aHash, cSha256.GetHash(), CSha256::HASH_BYTES);
        hRes = cECDsa.Sign(aHash, aSignature);
    }

    // Create payload
    hRes = TranslateToHresult([&]()
    {
        vecPayload.insert(vecPayload.end(), aSignature, aSignature + sizeof(aSignature));
        vecPayload.insert(vecPayload.end(), reinterpret_cast<const BYTE *>(lpNewPublicKey),
                          reinterpret_cast<const BYTE *>(lpNewPublicKey) + nNewPublicKeyLen);
    });
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }

    // Run command
    hRes = SendCommandAndWaitReply(CMD_CHANGE_USER_CREDENTIALS, vecPayload.data(), vecPayload.size(), vecReply);
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }

    // On success, mark the credentials change as completed
    cCmdResult = BuildCommandResultFromStandardReply(vecReply);
    if (cCmdResult.Succeeded() && cCmdResult.Code() == 0)
    {
        if ((_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) == STATE_CONNECTED &&
            nOrigConnectionID == _InterlockedRead(&m_nConnectionID))
        {
            _InterlockedAnd(&m_nState, ~STATE_FLAG_MUST_CHANGE_CREDS);
        }
    }

    // Done
    return cCmdResult;
}

CIotCommClient::CCommandResult CIotCommClient::SetHostNameCommand(_In_z_ LPCSTR szHostNameA)
{
    SIZE_T nHostNameLen;
    std::vector<BYTE> vecPayload;
    std::vector<BYTE> vecReply;
    HRESULT hRes;

    if (!szHostNameA)
    {
        return CCommandResult(E_INVALIDARG);
    }
    nHostNameLen = strlen(szHostNameA);
    if (nHostNameLen < 1 || nHostNameLen > 64)
    {
        return CCommandResult(E_INVALIDARG);
    }

    // Create payload
    hRes = TranslateToHresult([&]()
    {
        vecPayload.insert(vecPayload.end(), szHostNameA, szHostNameA + nHostNameLen);
        vecPayload.push_back(0x00);
    });
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }

    // Run command
    hRes = SendCommandAndWaitReply(CMD_SET_MDNS_HOSTNAME, vecPayload.data(), vecPayload.size(), vecReply);
    if (FAILED(hRes))
    {
        return CCommandResult(hRes);
    }

    // Done
    return BuildCommandResultFromStandardReply(vecReply);
}

CIotCommClient::CCommandResult CIotCommClient::BuildCommandResultFromStandardReply(_In_ std::vector<BYTE> &reply)
{
    LPBYTE lpData;
    SIZE_T nDataLen, nMsgLen;
    DWORD dwCode;
    std::wstring strMessage;
    HRESULT hRes;

    nDataLen = reply.size();
    if (nDataLen < sizeof(DWORD) + 1)
    {
        return CCommandResult(E_INVALID_PROTOCOL_FORMAT);
    }
    lpData = reply.data();

    // Extract code
    dwCode = readUint32BE(reply.data());
    if (dwCode == 0)
    {
        return CCommandResult(S_OK);
    }

    // Extract message
    for (nMsgLen = 4; nMsgLen < nDataLen && lpData[nMsgLen] != 0; nMsgLen++);
    hRes = Utf8BytesToWstring(lpData + 4, nMsgLen - 4, strMessage);
    if (FAILED(hRes))
    {
        return CCommandResult(E_INVALID_PROTOCOL_FORMAT);
    }

    // Done
    return CCommandResult(S_OK, dwCode, strMessage.c_str());
}

LONG CIotCommClient::SetConnectionState(_In_ LONG nConnState) noexcept
{
    LONG initVal, newVal;

    newVal = _InterlockedRead(&m_nState);
    do
    {
        initVal = newVal;
        newVal = _InterlockedCompareExchange(&m_nState, (initVal & (~STATE_CONNECTION_MASK)) | nConnState, initVal);
    }
    while (newVal != initVal);
    return initVal & STATE_CONNECTION_MASK;
}

VOID CIotCommClient::OnWsMessage(_In_ const BYTE *lpData, _In_ SIZE_T nDataLen, _In_ BOOL bIsText)
{
    BYTE nVersion;
    USHORT nCmd;
    DWORD dwReplyCounter = 0, dwRxCounter = 0;
    BYTE aIV[SESSION_IV_LEN];
    USHORT nIntErrCode = 0;
    LPCSTR szIntErrReasonA = nullptr;
    std::vector<BYTE> vecInput;
    HRESULT hRes = S_OK;

    {
        CAutoRundownProtection cAutoRundownProt(&m_nRundownProt);

        // Ignore messages if we are not connected
        if (cAutoRundownProt.IsAcquired() && (_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) == STATE_CONNECTED)
        {
            // Only binary packets are allowed
            if (bIsText)
            {
                nIntErrCode = WINHTTP_WEB_SOCKET_INVALID_PAYLOAD_CLOSE_STATUS;
                szIntErrReasonA = "Protocol error: non-binary packet";
                goto done;
            }

            // Check message size
            if (nDataLen < PACKET_HEADER_LEN)
            {
                // Packet too short to contain header + tag
                nIntErrCode = WINHTTP_WEB_SOCKET_INVALID_PAYLOAD_CLOSE_STATUS;
                szIntErrReasonA = "Protocol error: packet too short";
                goto done;
            }
            if (nDataLen > PACKET_HEADER_LEN + MAX_MSG_SIZE + CAes::GCM_TAG_LEN)
            {
                // Packet too short to contain header + tag
                nIntErrCode = WINHTTP_WEB_SOCKET_INVALID_PAYLOAD_CLOSE_STATUS;
                szIntErrReasonA = "Protocol error: packet too long";
                goto done;
            }

            // Extract header and validate version and RX counter (a.k.a. nonce)
            // Header { v(1) | cmd(2) | filler(1) | replyCounter(4) | counter(4) | filler(4) }
            nVersion = lpData[0];
            nCmd = readUint16BE(lpData + 1);
            dwReplyCounter = readUint32BE(lpData + 4);
            dwRxCounter = readUint32BE(lpData + 8);

            if (nVersion == VERSION)
            {
                CMutex::CAutoLock cLock(m_cMutex);

                if (dwRxCounter != m_dwNextRxCounter)
                {
                    // Invalid RX counter
                    nIntErrCode = WINHTTP_WEB_SOCKET_INVALID_PAYLOAD_CLOSE_STATUS;
                    szIntErrReasonA = "Protocol error: RX counter mismatch";
                    goto done_in_lock;
                }
                m_dwNextRxCounter += 1;

                // Build IV
                memcpy(aIV, m_aServerBaseIV, SESSION_IV_LEN);
                for (SIZE_T i = 0; i < 4; i++)
                {
                    aIV[SESSION_IV_LEN - i - 1] ^= (BYTE)((dwRxCounter >> (i << 3)) & 0xFF);
                }

                // Prepare buffer
                hRes = TranslateToHresult([&]()
                {
                    vecInput.resize(nDataLen - PACKET_HEADER_LEN - CAes::GCM_TAG_LEN);
                });
                if (FAILED(hRes))
                {
                    nIntErrCode = WINHTTP_WEB_SOCKET_SERVER_ERROR_CLOSE_STATUS;
                    szIntErrReasonA = "Out of memory";
                    goto done_in_lock;
                }

                // Try to decrypt
                hRes = m_cServerAes.Decrypt(lpData + PACKET_HEADER_LEN, nDataLen - PACKET_HEADER_LEN, aIV, SESSION_IV_LEN, nullptr, 0,
                                          vecInput.data());
                if (hRes == S_FALSE)
                {
                    nIntErrCode = WINHTTP_WEB_SOCKET_INVALID_PAYLOAD_CLOSE_STATUS;
                    szIntErrReasonA = "Protocol error: decryption/authentication failed";
                    hRes = S_OK;
                    goto done_in_lock;
                }
                if (FAILED(hRes))
                {
                    // Unable to decrypt message
                    nIntErrCode = WINHTTP_WEB_SOCKET_INVALID_PAYLOAD_CLOSE_STATUS;
                    szIntErrReasonA = "Protocol error: Internal error while decrypting";
                    goto done_in_lock;
                }

                // If it is a reply, then try to match it with a pending reply
                if (dwReplyCounter != 0)
                {
                    OnCommandReply(nCmd, vecInput, dwReplyCounter);
                }
done_in_lock:;
            }
            else
            {
                // Unsupported version
                nIntErrCode = WINHTTP_WEB_SOCKET_INVALID_PAYLOAD_CLOSE_STATUS;
                szIntErrReasonA = "Protocol error: unsupported version";
            }

done:
            // If an error happened, try to close gracefully
            if (nIntErrCode != 0)
            {
                m_cWsClient->WsClose(nIntErrCode, szIntErrReasonA);
            }
        }

        // Delivery of server commands is done outside of the mutex to avoid potential deadlocks with user callbacks
        if (nIntErrCode == 0 && dwReplyCounter == 0)
        {
            OnServerCommand(nCmd, vecInput.data(), vecInput.size());
        }
    }

    // If an error happened, notify the user
    if (nIntErrCode != 0)
    {
        CallCloseCallbackAndCleanup(nIntErrCode, szIntErrReasonA, hRes);
    }

    // Cleanup
    if (!vecInput.empty())
    {
        memset(vecInput.data(), 0, vecInput.size());
        vecInput.clear();
    }
}

VOID CIotCommClient::OnWsClose(_In_ USHORT nCode, _In_z_ LPCSTR szReasonA)
{
    CallCloseCallbackAndCleanup(nCode, szReasonA, S_OK);
}

VOID CIotCommClient::OnWsError(_In_ HRESULT hRes, _In_ BOOL bIsRead)
{
    CallCloseCallbackAndCleanup(WINHTTP_WEB_SOCKET_SERVER_ERROR_CLOSE_STATUS,
                                bIsRead ? "Comm error: Read failed" : "Comm error: Write failed",
                                hRes);
}

DWORD CIotCommClient::GetNextTxCounter() const
{
    return m_dwNextTxCounter;
}

HRESULT CIotCommClient::EncryptAndSend(_In_ USHORT nCmd, _In_reads_bytes_(nDataLen) _In_ LPCVOID lpData, _In_ SIZE_T nDataLen)
{
    LPBYTE lpHeader;
    BYTE aIV[SESSION_IV_LEN];
    HRESULT hRes;

    _ASSERT(lpData);
    _ASSERT(nDataLen > 0);

    // Check data size
    if (nDataLen > MAX_MSG_SIZE)
    {
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    }

    // Prepare output buffer
    hRes = TranslateToHresult([&]()
    {
        m_vecOutput.resize(PACKET_HEADER_LEN + nDataLen + CAes::GCM_TAG_LEN);
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Header { v(1) | cmd(2) | filler(1) | replyCounter(4) | counter(4) | filler(4) }
    lpHeader = m_vecOutput.data();
    lpHeader[0] = VERSION; // v
    writeUint16BE(lpHeader + 1, nCmd); //cmd
    lpHeader[3] = 0; //filler1
    writeUint32BE(lpHeader + 4, 0); //reply counter
    writeUint32BE(lpHeader + 8, m_dwNextTxCounter); // counter
    writeUint32BE(lpHeader + 12, 0); // filler2

    // Build IV
    memcpy(aIV, m_aClientBaseIV, SESSION_IV_LEN);
    for (SIZE_T i = 0; i < 4; i++)
    {
        aIV[SESSION_IV_LEN - i - 1] ^= (BYTE)((m_dwNextTxCounter >> (i << 3)) & 0xFF);
    }

    // Encrypt data
    hRes = m_cClientAes.Encrypt(lpData, nDataLen, aIV, SESSION_IV_LEN, nullptr, 0, m_vecOutput.data() + PACKET_HEADER_LEN);
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Send it
    hRes = m_cWsClient->WsSend(m_vecOutput.data(), PACKET_HEADER_LEN + nDataLen + CAes::GCM_TAG_LEN, FALSE);
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Increment write counter
    m_dwNextTxCounter += 1;

    // Done
    return S_OK;
}

HRESULT CIotCommClient::EncryptAndSendUdp(_In_ USHORT nCmd, _In_reads_bytes_(nDataLen) _In_ LPCVOID lpData, _In_ SIZE_T nDataLen)
{
    LPBYTE lpHeader;
    BYTE aIV[SESSION_IV_LEN];
    int nSentLen;
    HRESULT hRes;

    _ASSERT(lpData);
    _ASSERT(nDataLen > 0);

    if (nDataLen > MAX_MSG_SIZE)
    {
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    }
    if (m_hUdpSocket == INVALID_SOCKET || m_dwUdpConnectionId == 0 || m_dwNextUdpTxCounter == 0)
    {
        return E_NOT_SET;
    }

    hRes = TranslateToHresult([&]()
    {
        m_vecUdpOutput.resize(PACKET_HEADER_LEN + nDataLen + CAes::GCM_TAG_LEN);
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    lpHeader = m_vecUdpOutput.data();
    lpHeader[0] = VERSION;
    writeUint16BE(lpHeader + 1, nCmd);
    lpHeader[3] = 0;
    writeUint32BE(lpHeader + 4, m_dwUdpConnectionId);
    writeUint32BE(lpHeader + 8, m_dwNextUdpTxCounter);
    writeUint32BE(lpHeader + 12, 0);

    memcpy(aIV, m_aUdpClientBaseIV, SESSION_IV_LEN);
    for (SIZE_T i = 0; i < 4; i++)
    {
        aIV[SESSION_IV_LEN - i - 1] ^= (BYTE)((m_dwNextUdpTxCounter >> (i << 3)) & 0xFF);
    }

    hRes = m_cUdpClientAes.Encrypt(lpData, nDataLen, aIV, SESSION_IV_LEN, nullptr, 0, m_vecUdpOutput.data() + PACKET_HEADER_LEN);
    if (FAILED(hRes))
    {
        return hRes;
    }

    nSentLen = ::send(m_hUdpSocket, reinterpret_cast<const char *>(m_vecUdpOutput.data()), static_cast<int>(m_vecUdpOutput.size()), 0);
    if (nSentLen == SOCKET_ERROR)
    {
        return HRESULT_FROM_WIN32(WSAGetLastError());
    }
    if (static_cast<SIZE_T>(nSentLen) != m_vecUdpOutput.size())
    {
        return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }

    m_dwNextUdpTxCounter += 1;
    return S_OK;
}

VOID CIotCommClient::OnCommandReply(_In_ USHORT nCmd, _Inout_ std::vector<BYTE> &vecInput, _In_ DWORD dwReplyCounter)
{
    CMutex::CAutoLock cPendingRepliesLock(m_cPendingRepliesMutex);

    if (auto it = m_mapPendingReplies.find(dwReplyCounter); it != m_mapPendingReplies.end())
    {
        PendingReply_t *lpPendingReply = it->second;

        m_mapPendingReplies.erase(it);

        lpPendingReply->data = std::move(vecInput);

        _InterlockedExchange(&lpPendingReply->hRes, S_OK);
        ::WakeByAddressSingle(const_cast<LONG *>(&lpPendingReply->hRes));
    }
}

VOID CIotCommClient::OnServerCommand(_In_ USHORT nCmd, _In_ LPBYTE lpData, _In_ SIZE_T nDataLen)
{
    MessageReceivedCallback_t cMessageReceivedCallbackCopy;

    {
        CReaderWriterMutex::CAutoSharedLock cLock(m_cCbRwMutex);

        cMessageReceivedCallbackCopy = m_cMessageReceivedCallback;
    }

    if (cMessageReceivedCallbackCopy)
    {
        cMessageReceivedCallbackCopy(nCmd, lpData, nDataLen);
    }
}

VOID CIotCommClient::FulfillAllWaitingCloses(_In_ USHORT nCode, _In_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason)
{
    WaitingClose_t *lpWaitingClose;

    for (;;)
    {
        lpWaitingClose = nullptr;
        {
            CMutex::CAutoLock cWaitingCloseLock(m_cWaitingCloseMutex);

            if (!m_queueWaitingClose.empty())
            {
                lpWaitingClose = m_queueWaitingClose.front();
                m_queueWaitingClose.pop();
            }
        }

        if (!lpWaitingClose)
        {
            break;
        }
        lpWaitingClose->nCode = nCode;
        strncpy_s(lpWaitingClose->szReasonA, 124, szReasonA, _TRUNCATE);

        _InterlockedExchange(&lpWaitingClose->hRes, hrInternalReason);
        ::WakeByAddressSingle(const_cast<LONG *>(&lpWaitingClose->hRes));
    }
}

VOID CIotCommClient::CancelAllWaitingReplies()
{
    CMutex::CAutoLock cPendingRepliesLock(m_cPendingRepliesMutex);

    for (auto it = m_mapPendingReplies.begin(); it != m_mapPendingReplies.end(); ++it)
    {
        PendingReply_t *lpPendingReply = it->second;

        _InterlockedExchange(&lpPendingReply->hRes, HRESULT_FROM_WIN32(ERROR_CANCELLED));
        ::WakeByAddressSingle(const_cast<LONG *>(&lpPendingReply->hRes));
    }

    m_mapPendingReplies.clear();
}

VOID CIotCommClient::ResetUdpState() noexcept
{
    if (m_hUdpSocket != INVALID_SOCKET)
    {
        ::closesocket(m_hUdpSocket);
        m_hUdpSocket = INVALID_SOCKET;
    }
    m_dwNextUdpTxCounter = 0;
    m_dwUdpConnectionId = 0;
    m_cUdpClientAes.Reset();
    memset(m_aUdpClientBaseIV, 0, sizeof(m_aUdpClientBaseIV));
    if (!m_vecUdpOutput.empty())
    {
        memset(m_vecUdpOutput.data(), 0, m_vecUdpOutput.size());
        m_vecUdpOutput.clear();
    }
}

BOOL CIotCommClient::Cleanup()
{
    if (SetConnectionState(STATE_CLEANING_UP) == STATE_CLEANING_UP)
    {
        return FALSE; // Cleanup already in progress
    }

    RundownProt_WaitForRelease(&m_nRundownProt);

    m_cLocalAbortEvent.Reset();
    if (m_cWsClient)
    {
        m_cWsClient->WsForceClose();
        m_cWsClient.reset();
    }
    m_dwNextRxCounter = 0;
    m_dwNextTxCounter = 0;
    m_cClientAes.Reset();
    m_cServerAes.Reset();
    ResetUdpState();
    m_strHostNameW.clear();
    memset(m_aSessionMasterKey, 0, sizeof(m_aSessionMasterKey));
    memset(m_aClientBaseIV, 0, sizeof(m_aClientBaseIV));
    memset(m_aServerBaseIV, 0, sizeof(m_aServerBaseIV));
    memset(m_aWsNonce, 0, sizeof(m_aWsNonce));
    if (!m_vecOutput.empty())
    {
        memset(m_vecOutput.data(), 0, m_vecOutput.size());
        m_vecOutput.clear();
    }
    CancelAllWaitingReplies();
    FulfillAllWaitingCloses(0, "Cancelled", HRESULT_FROM_WIN32(ERROR_CANCELLED));

    _InterlockedExchange(&m_nState, 0);

    // Done
    return TRUE;
}

VOID CIotCommClient::WaitForCleanupCompletion()
{
    while ((_InterlockedRead(&m_nState) & STATE_CONNECTION_MASK) == STATE_CLEANING_UP)
    {
        ::Sleep(20);
    }
}

VOID CIotCommClient::CallCloseCallbackAndCleanup(_In_ USHORT nCode, _In_z_ LPCSTR szReasonA, _In_ HRESULT hRes)
{
    if ((_InterlockedOr(&m_nState, STATE_FLAG_WS_CLOSE_OR_ERROR_CB_CALLED) & STATE_FLAG_WS_CLOSE_OR_ERROR_CB_CALLED) == 0)
    {
        CReaderWriterMutex::CAutoSharedLock cLock(m_cCbRwMutex);

        if (m_cCloseCallback)
        {
            m_cCloseCallback(nCode, szReasonA, hRes);
        }
    }

    Cleanup();
}

// -----------------------------------------------------------------------------

static SIZE_T ValidateUserName(_In_z_ LPCSTR szUserNameA)
{
    SIZE_T nLen;

    for (nLen = 0; nLen < 256; nLen++)
    {
        if (szUserNameA[nLen] == 0)
        {
            break;
        }
        if ((BYTE)szUserNameA[nLen] > 127)
        {
            return FALSE;
        }
    }
    return (nLen > 0 && nLen < 256) ? TRUE : FALSE;
}

static BOOL IsValidHostname(_In_z_ LPCWSTR szHostNameW)
{
    union {
        IN_ADDR sAddr4;
        IN6_ADDR sAddr6;
    } u;

    return (szHostNameW[0] != 0 && wcslen(szHostNameW) <= 255 &&
            (::InetPtonW(AF_INET, szHostNameW, &u.sAddr4) == 1 ||
             ::InetPtonW(AF_INET6, szHostNameW, &u.sAddr6) == 1 ||
             ::DnsValidateName_W(szHostNameW, DnsNameDomain) == ERROR_SUCCESS)) ? TRUE : FALSE;
}

static BOOL IsValidHostnameAndPort(_In_z_ LPCWSTR szHostNameAndPortW)
{
    WCHAR szTempBufW[256];
    LPCWSTR sW;
    SIZE_T nLen;
    DWORD dwPort;

    // Check host
    if (*szHostNameAndPortW == L'[')
    {
        // Extract IPv6 between square brackets
        for (sW = szHostNameAndPortW + 1; *sW != 0 && *sW != L']'; sW++);
        if (*sW == 0)
        {
            return FALSE;
        }

        nLen = (SIZE_T)(sW - szHostNameAndPortW) - 1;
        if (nLen > 255)
        {
            return FALSE;
        }

        sW++; // Skip closing ]

        memcpy(szTempBufW, szHostNameAndPortW + 1, nLen * sizeof(WCHAR));
        szTempBufW[nLen] = 0;

        if (!IsValidHostname(szTempBufW))
        {
            return FALSE;
        }

        // End or port?
        if (*sW == 0)
        {
            return TRUE;
        }
        if (*sW != L':')
        {
            return FALSE;
        }
        sW++; // Skip colon
    }
    else
    {
        sW = wcschr(szHostNameAndPortW, L':');
        if (!sW)
        {
            return IsValidHostname(szHostNameAndPortW);
        }

        nLen = (SIZE_T)(sW - szHostNameAndPortW);
        if (nLen > 255)
        {
            return FALSE;
        }

        memcpy(szTempBufW, szHostNameAndPortW, nLen * sizeof(WCHAR));
        szTempBufW[nLen] = 0;

        if (!IsValidHostname(szTempBufW))
        {
            return FALSE;
        }

        sW++; // Skip colon
    }

    // At this point, we are after the colon ready to verify the port number
    if (*sW == 0)
    {
        return FALSE;
    }
    dwPort = 0;
    while (*sW != 0)
    {
        if (*sW < L'0' || *sW > L'9')
        {
            return FALSE;
        }
        dwPort = dwPort * 10 + (DWORD)(*sW - L'0');
        if (dwPort > 65535)
        {
            return FALSE;
        }
        sW++;
    }
    return TRUE;
}

static HRESULT ExtractHostname(_In_z_ LPCWSTR szHostNameAndPortW, _Out_ std::wstring &strHostname) noexcept
{
    LPCWSTR sW;

    if (!szHostNameAndPortW)
    {
        return E_INVALIDARG;
    }

    try
    {
        if (*szHostNameAndPortW == L'[')
        {
            sW = wcschr(szHostNameAndPortW + 1, L']');
            if (!sW || sW == szHostNameAndPortW + 1)
            {
                return E_INVALIDARG;
            }

            strHostname.assign(szHostNameAndPortW + 1, sW);
        }
        else
        {
            sW = wcschr(szHostNameAndPortW, L':');
            if (!sW)
            {
                strHostname = szHostNameAndPortW;
            }
            else
            {
                strHostname.assign(szHostNameAndPortW, sW);
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    return strHostname.empty() ? E_INVALIDARG : S_OK;
}

static __forceinline USHORT readUint16BE(_In_reads_bytes_(2) const BYTE *p)
{
    return ((USHORT)p[0] << 8) | (USHORT)p[1];
}

static __forceinline VOID writeUint16BE(_Out_writes_bytes_(2) LPBYTE p, _In_ USHORT v)
{
    p[0] = (BYTE)((v >> 8) & 0xFF);
    p[1] = (BYTE)( v       & 0xFF);
}

static __forceinline DWORD readUint32BE(_In_reads_bytes_(4) const BYTE *p)
{
    return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | ((DWORD)p[3]);
}

static __forceinline void writeUint32BE(_Out_writes_bytes_(4) LPBYTE p, _In_ DWORD v)
{
    p[0] = (BYTE)((v >> 24) & 0xFF);
    p[1] = (BYTE)((v >> 16) & 0xFF);
    p[2] = (BYTE)((v >>  8) & 0xFF);
    p[3] = (BYTE)( v        & 0xFF);
}

static HRESULT Utf8BytesToWstring(_In_reads_bytes_(nStrLen) LPBYTE lpStr, _In_ SIZE_T nStrLen, _Out_ std::wstring &strOut)
{
    int nWideLen, nWritten;
    HRESULT hRes;

    strOut.clear();

    if (nStrLen == 0)
    {
        return S_OK;
    }

    // First call: get required wchar count
    nWideLen = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<LPCCH>(const_cast<LPBYTE>(lpStr)),
                                     static_cast<int>(nStrLen), nullptr, 0);
    if (nWideLen <= 0)
    {
        return E_INVALIDARG;
    }

    hRes = TranslateToHresult([&]()
    {
        strOut.resize(static_cast<size_t>(nWideLen));
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Second call: convert
    nWritten = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<LPCCH>(const_cast<LPBYTE>(lpStr)),
                                     static_cast<int>(nStrLen), strOut.data(), nWideLen);
    if (nWritten < 0)
    {
        nWritten = 0;
    }
    strOut[nWritten] = 0;

    // Done
    return S_OK;
}
