#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <functional>
#include <map>
#include <queue>
#include "..\Crypto\Aes.h"
#include "..\Crypto\P256.h"
#include "..\Crypto\Sha256.h"
#include "..\Http\HttpClient.h"
#include "..\Utils\Mutex.h"
#include "..\Utils\ReaderWriterLock.h"
#include "..\Utils\WindowsEvent.h"

#define WINHTTP_WEB_SOCKET_SESSION_NOT_FOUND_CUSTOM_CLOSE_STATUS            ((WINHTTP_WEB_SOCKET_CLOSE_STATUS)4001)
#define WINHTTP_WEB_SOCKET_CREDENTIALS_CHANGE_MANDATORY_CUSTOM_CLOSE_STATUS ((WINHTTP_WEB_SOCKET_CLOSE_STATUS)4002)

// -----------------------------------------------------------------------------

class CIotCommClient
{
public:
    static constexpr HRESULT IOTCOMM_E_UNEXPECTED_SERVER_RESPONSE = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_HTTP, 0x1001);
    static constexpr HRESULT IOTCOMM_E_AUTH_FAILED = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_HTTP, 0x1002);

    static constexpr USHORT CMD_CREATE_USER = 0x7FF1;
    static constexpr USHORT CMD_DELETE_USER = 0x7FF2;
    static constexpr USHORT CMD_RESET_USER_CREDENTIALS = 0x7FF3;
    static constexpr USHORT CMD_CHANGE_USER_CREDENTIALS = 0x7FF4;
    static constexpr USHORT CMD_UDP_OPEN = 0x7FF8;
    static constexpr USHORT CMD_SET_MDNS_HOSTNAME = 0x7000;

    static constexpr BYTE USER_CREATE_FLAG_MUST_CHANGE_CREDENTIALS_ON_NEXT_LOGIN = 0x01;
    static constexpr BYTE USER_CREATE_FLAGS_MASK = USER_CREATE_FLAG_MUST_CHANGE_CREDENTIALS_ON_NEXT_LOGIN;

public:
    typedef enum LinkState_e {
        LinkStateDisconnected = 0,
        LinkStateConnecting,
        LinkStateConnected,
        LinkStateDisconnecting
    } LinkState_t;

    typedef struct DeviceFingerprintInfo_s {
        BYTE aDevicePublicKey[CECDsaKeyPair::RAW_PUBLIC_KEY_BYTES]{};
        WCHAR szFingerprintW[(CSha256::HASH_BYTES * 2) + 1]{};
    } DeviceFingerprintInfo_t;

    typedef std::function<VOID(_In_ USHORT /*nCmd*/, _In_ const BYTE* /*lpData*/, _In_ SIZE_T /*nDataLen*/)> MessageReceivedCallback_t;
    typedef std::function<VOID(_In_ USHORT /*nCode*/, _In_opt_z_ LPCSTR /*szReasonA*/, _In_ HRESULT /*hrInternalReason*/)> CloseCallback_t;
    typedef std::function<HRESULT(_In_ const DeviceFingerprintInfo_t & /*sInfo*/,
                                  _In_ HANDLE /*hCancelEvent*/)> VerifyDeviceFingerprintCallback_t;

public:
    class CCommandResult
    {
    public:
        CCommandResult() = default;
        explicit CCommandResult(_In_ HRESULT _hrInternalError, _In_opt_ DWORD _dwCode = 0,
                                _In_opt_z_ LPCWSTR szMessageW = nullptr) noexcept :
            m_hrInternalError(_hrInternalError), m_dwCode(_dwCode), m_strMessage()
        {
            try
            {
                if (szMessageW && *szMessageW)
                {
                    m_strMessage = szMessageW;
                }
            }
            catch (const std::bad_alloc &)
            {
                m_hrInternalError = E_OUTOFMEMORY;
                m_dwCode = 0;
                m_strMessage.clear();
            }
        };
        CCommandResult(_In_ const CCommandResult &) = default;
        CCommandResult(_Inout_ CCommandResult &&) = default;
        ~CCommandResult() = default;

        CCommandResult &operator=(_In_ const CCommandResult &) = default;
        CCommandResult &operator=(_Inout_ CCommandResult &&) = default;

        HRESULT HResult() const noexcept
        {
            return m_hrInternalError;
        };

        bool Succeeded() const noexcept
        {
            return SUCCEEDED(m_hrInternalError) ? true : false;
        };

        bool Failed() const noexcept
        {
            return FAILED(m_hrInternalError) ? true : false;
        };

        DWORD Code() const noexcept
        {
            return m_dwCode;
        };

        LPCWSTR Message() const noexcept
        {
            return m_strMessage.c_str();
        };

    private:
        HRESULT m_hrInternalError{ E_FAIL };
        DWORD m_dwCode{ 0 };
        std::wstring m_strMessage;
    };

public:
    CIotCommClient() = default;
    CIotCommClient(_In_ const CIotCommClient &) = delete;
    CIotCommClient(_Inout_ CIotCommClient &&) = delete;
    ~CIotCommClient() noexcept;

    CIotCommClient &operator=(_In_ const CIotCommClient &) = delete;
    CIotCommClient &operator=(_Inout_ CIotCommClient &&) = delete;

    VOID OnMessageReceived(_In_ MessageReceivedCallback_t cMessageReceivedCallback) noexcept;
    VOID OnClose(_In_ CloseCallback_t cCloseCallback) noexcept;

    HRESULT Connect(_In_z_ LPCWSTR szHostNameW, _In_z_ LPCSTR szUserNameA,
                    _In_ const BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES],
                    _In_opt_ DWORD dwTimeoutMs = INFINITE, _In_opt_ HANDLE hAbortEvent = nullptr,
                    _In_opt_ VerifyDeviceFingerprintCallback_t cVerifyDeviceFingerprintCallback = nullptr) noexcept;
    VOID Close(_In_ USHORT nCode = 1000, _In_opt_z_ LPCSTR szReasonA = nullptr) noexcept;
    VOID ForceClose() noexcept;

    LinkState_t GetLinkState() const noexcept;
    BOOL MustChangeCredentials() const noexcept;

    HRESULT SendCommand(_In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload, _In_ SIZE_T nPayloadLen);
    HRESULT SendCommandAndWaitReply(_In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload, _In_ SIZE_T nPayloadLen,
                                    _Out_ std::vector<BYTE> &reply) noexcept;
    CCommandResult OpenUdpChannel();
    HRESULT SendUdpCommand(_In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload, _In_ SIZE_T nPayloadLen);

    CCommandResult CreateUserCommand(_In_z_ LPCSTR szUserNameA, _In_reads_bytes_(nPublicKeyLen) LPCVOID lpPublicKey,
                                     _In_ SIZE_T nPublicKeyLen, _In_opt_ BYTE uFlags = 0);
    CCommandResult DeleteUserCommand(_In_z_ LPCSTR szUserNameA);
    CCommandResult ResetUserCredentialsCommand(_In_z_ LPCSTR szUserNameA, _In_reads_bytes_(nPublicKeyLen) LPCVOID lpPublicKey,
                                               _In_ SIZE_T nPublicKeyLen);
    CCommandResult ChangeUserCredentialsCommand(_In_reads_bytes_(nOldPrivateKeyLen) LPCVOID lpOldPrivateKey,
                                                _In_ SIZE_T nOldPrivateKeyLen,
                                                _In_reads_bytes_(nNewPublicKeyLen) LPCVOID lpNewPublicKey,
                                                _In_ SIZE_T nNewPublicKeyLen);
    CCommandResult SetHostNameCommand(_In_z_ LPCSTR szHostNameA);

    static CCommandResult BuildCommandResultFromStandardReply(_In_ std::vector<BYTE> &reply);

private:
    static constexpr SIZE_T NONCE_SIZE = 16;
    static constexpr SIZE_T COOKIE_SIZE = 12;
    static constexpr SIZE_T IV_SIZE = 16;
    static constexpr SIZE_T AES_KEY_LEN = 32;

    static constexpr BYTE VERSION = 1;
    static constexpr SIZE_T PACKET_HEADER_LEN = 16;
    static constexpr SIZE_T MAX_MSG_SIZE = 2000;
    static constexpr SIZE_T SESSION_IV_LEN = 12;

private:
    LONG SetConnectionState(_In_ LONG nConnState) noexcept;

    VOID OnWsMessage(_In_ const BYTE *lpData, _In_ SIZE_T nDataLen, _In_ BOOL bIsText);
    VOID OnWsClose(_In_ USHORT nCode, _In_z_ LPCSTR szReasonA);
    VOID OnWsError(_In_ HRESULT hRes, _In_ BOOL bIsRead);

    DWORD GetNextTxCounter() const;
    HRESULT EncryptAndSend(_In_ USHORT nCmd, _In_reads_bytes_(nDataLen) _In_ LPCVOID lpData, _In_ SIZE_T nDataLen);
    HRESULT EncryptAndSendUdp(_In_ USHORT nCmd, _In_reads_bytes_(nDataLen) _In_ LPCVOID lpData, _In_ SIZE_T nDataLen);
    VOID OnCommandReply(_In_ USHORT nCmd, _Inout_ std::vector<BYTE> &vecInput, _In_ DWORD dwReplyCounter);
    VOID OnServerCommand(_In_ USHORT nCmd, _In_ LPBYTE lpData, _In_ SIZE_T nDataLen);

    VOID FulfillAllWaitingCloses(_In_ USHORT nCode, _In_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason);
    VOID CancelAllWaitingReplies();
    VOID ResetUdpState() noexcept;

    BOOL Cleanup();
    VOID WaitForCleanupCompletion();

    VOID CallCloseCallbackAndCleanup(_In_ USHORT nCode, _In_z_ LPCSTR szReasonA, _In_ HRESULT hRes);

private:
    typedef struct PendingReply_s {
        HRESULT volatile hRes{ S_FALSE };
        std::vector<BYTE> data;
    } PendingReply_t;

    typedef struct WaitingClose_s {
        HRESULT volatile hRes{ S_FALSE };
        USHORT nCode{ 0 };
        CHAR szReasonA[124]{};
    } WaitingClose_t;

    CReaderWriterMutex m_cCbRwMutex;
    MessageReceivedCallback_t m_cMessageReceivedCallback;
    CloseCallback_t m_cCloseCallback;

    mutable CMutex m_cMutex;
    mutable LONG volatile m_nRundownProt{ 0 };
    CWindowsEvent m_cLocalAbortEvent;
    std::unique_ptr<CHttpClient> m_cWsClient;

    LONG volatile m_nConnectionID{ 0 };
    mutable LONG volatile m_nState{ 0 };

    DWORD m_dwNextRxCounter{ 0 };
    DWORD m_dwNextTxCounter{ 0 };
    DWORD m_dwNextUdpTxCounter{ 0 };
    DWORD m_dwUdpConnectionId{ 0 };
    CAes m_cClientAes;
    CAes m_cServerAes;
    CAes m_cUdpClientAes;
    BYTE m_aSessionMasterKey[AES_KEY_LEN]{};
    BYTE m_aClientBaseIV[SESSION_IV_LEN]{};
    BYTE m_aServerBaseIV[SESSION_IV_LEN]{};
    BYTE m_aUdpClientBaseIV[SESSION_IV_LEN]{};
    BYTE m_aWsNonce[NONCE_SIZE]{};
    std::wstring m_strHostNameW;
    SOCKET m_hUdpSocket{INVALID_SOCKET};
    std::vector<BYTE> m_vecOutput;
    std::vector<BYTE> m_vecUdpOutput;

    CMutex m_cPendingRepliesMutex;
    std::map<DWORD, PendingReply_t *> m_mapPendingReplies;
    CMutex m_cWaitingCloseMutex;
    std::queue<WaitingClose_t *> m_queueWaitingClose;
};
