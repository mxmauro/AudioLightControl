#include "HttpClient.h"
#include "..\Utils\Exception2HR.h"
#include "..\Utils\RundownProtection.h"
#include <Mstcpip.h>
#include <string>
#include <utility>

#pragma comment(lib, "winhttp.lib")

#define STATE_MASK_WAITING_FOR_WS_INIT         0x0001
#define STATE_MASK_WS_CLOSE_OR_ERROR_CB_CALLED 0x0002
#define STATE_MASK_SOCKET_CLOSE_CALLED         0x0004
#define STATE_MASK_CLEANUP_IN_PROGRESS         0x0008

#define WS_KEEPALIVE_TIME_MS 15000

#define CURRENT_MESSAGE_TYPE_NONE   0
#define CURRENT_MESSAGE_TYPE_TEXT   1
#define CURRENT_MESSAGE_TYPE_BINARY 2

#define MAX_FREE_DATA_BUFFERS 16

#define HRESULT_FROM_LASTERROR() HRESULT_FROM_WIN32(::GetLastError())

#define _InterlockedRead(destination) _InterlockedOr(destination, 0)

// -----------------------------------------------------------------------------

typedef struct DelayedCallCallback_s {
    CHttpClient::WsDataReceivedCallback cWsDataReceivedCallback;
    CHttpClient::WsClosedCallback cWsClosedCallback;
    CHttpClient::WsErrorCallback cWsErrorCallback;
    std::vector<BYTE> data;
    BOOL bDataIsText{ FALSE };
    USHORT nCloseCode{ 0 };
    LPCSTR szCloseReasonA{ nullptr };
    HRESULT hrError{ S_OK };
    BOOL bErrorReadOperation{ FALSE };
} DelayedCallCallback_t;

// -----------------------------------------------------------------------------

static const LPCWSTR szDefaultUserAgentW = L"WinHttpRequest/1.0";

// -----------------------------------------------------------------------------

static std::map<std::wstring, std::wstring> ParseResponseHeaders(_In_ const std::wstring &strRawHeaders);
static std::vector<std::wstring> ParseAcceptTypes(_In_ const std::wstring &strAcceptHeader);

static HRESULT SanitizeHeaders(_In_ LPCWSTR *lpszHeadersW, _In_ SIZE_T nHeadersCount,
                               _Out_ std::map<std::wstring, std::wstring> &aOutHeaders) noexcept;
static HRESULT SanitizeHeader(_In_z_ LPCWSTR szHeaderW, _Out_ std::pair<std::wstring, std::wstring> &pairHeader) noexcept;

static bool IsWhitespaceOrNewLine(_In_ wchar_t ch) noexcept;
static void TrimSpacesAndNewLines(_Inout_ std::wstring &s);

static bool IsValidHeaderNameChar(_In_ wchar_t ch) noexcept;
static bool IsValidHeaderName(_In_ const std::wstring &name) noexcept;
static bool IsValidHeaderValue(_In_ const std::wstring &value) noexcept;
static HRESULT ParseUrl(_In_z_ LPCWSTR szUrlW, _Out_ std::wstring &strHostname, _Out_ std::wstring &strPath, _Out_ INTERNET_PORT &nPort,
                        _Out_ BOOL &bIsHttps, _Out_ BOOL &bIsWebSocket) noexcept;
static HRESULT GetOsVersion(_Out_ LPOSVERSIONINFOW lpOsviW);

// -----------------------------------------------------------------------------

CHttpClient::CHttpClient(_Inout_ CHttpClient &&other) noexcept
{
    if (this != &other)
    {
        // WinHTTP callbacks capture the object address in the request context.
        // Quiesce the source before transferring ownership so no callback can target the old object.
        other.Cleanup(TRUE);
        other.WaitForCleanupCompletion();

        m_hSession = other.m_hSession;
        other.m_hSession = nullptr;
        m_hConnect = other.m_hConnect;
        other.m_hConnect = nullptr;
        m_hRequest = other.m_hRequest;
        other.m_hRequest = nullptr;
        m_hWebSocket = other.m_hWebSocket;
        other.m_hWebSocket = nullptr;

        m_cWsDataReceivedCallback = std::move(other.m_cWsDataReceivedCallback);
        m_cWsClosedCallback = std::move(other.m_cWsClosedCallback);
        m_cWsErrorCallback = std::move(other.m_cWsErrorCallback);

        _InterlockedExchange(&m_nStateMask, _InterlockedExchange(&other.m_nStateMask, 0));

        m_aRxBuffer = std::move(other.m_aRxBuffer);

        m_nCurrentWsRxMsgType = other.m_nCurrentWsRxMsgType;
        other.m_nCurrentWsRxMsgType = 0;
        m_aCurrentWsRxMsg = std::move(other.m_aCurrentWsRxMsg);

        m_dwStatusCode = other.m_dwStatusCode;
        other.m_dwStatusCode = 0;
        m_cResponseBody = std::move(other.m_cResponseBody);
        m_aResponseHeaders = std::move(other.m_aResponseHeaders);

        _InterlockedExchange(&m_hrReadLastError, _InterlockedExchange(&other.m_hrReadLastError, 0));
        m_hRequestCompletedEvent = other.m_hRequestCompletedEvent;
        other.m_hRequestCompletedEvent = nullptr;
        m_lpClosingRequestHandle = _InterlockedExchangePointer(&other.m_lpClosingRequestHandle, nullptr);
    }
}

CHttpClient::~CHttpClient() noexcept
{
    Cleanup(FALSE);
    WaitForCleanupCompletion();
}

CHttpClient& CHttpClient::operator=(_Inout_ CHttpClient &&other) noexcept
{
    if (this != &other)
    {
        // WinHTTP callbacks capture the object address in the request context.
        // Quiesce both objects before transferring ownership to avoid leaking or misrouting callbacks.
        Cleanup(FALSE);
        WaitForCleanupCompletion();
        other.Cleanup(TRUE);
        other.WaitForCleanupCompletion();

        m_hSession = other.m_hSession;
        other.m_hSession = nullptr;
        m_hConnect = other.m_hConnect;
        other.m_hConnect = nullptr;
        m_hRequest = other.m_hRequest;
        other.m_hRequest = nullptr;
        m_hWebSocket = other.m_hWebSocket;
        other.m_hWebSocket = nullptr;

        m_cWsDataReceivedCallback = std::move(other.m_cWsDataReceivedCallback);
        m_cWsClosedCallback = std::move(other.m_cWsClosedCallback);
        m_cWsErrorCallback = std::move(other.m_cWsErrorCallback);

        _InterlockedExchange(&m_nStateMask, _InterlockedExchange(&other.m_nStateMask, 0));

        m_aRxBuffer = std::move(other.m_aRxBuffer);

        m_nCurrentWsRxMsgType = other.m_nCurrentWsRxMsgType;
        other.m_nCurrentWsRxMsgType = 0;
        m_aCurrentWsRxMsg = std::move(other.m_aCurrentWsRxMsg);

        m_dwStatusCode = other.m_dwStatusCode;
        other.m_dwStatusCode = 0;
        m_cResponseBody = std::move(other.m_cResponseBody);
        m_aResponseHeaders = std::move(other.m_aResponseHeaders);

        _InterlockedExchange(&m_hrReadLastError, _InterlockedExchange(&other.m_hrReadLastError, 0));
        m_hRequestCompletedEvent = other.m_hRequestCompletedEvent;
        other.m_hRequestCompletedEvent = nullptr;
        m_lpClosingRequestHandle = _InterlockedExchangePointer(&other.m_lpClosingRequestHandle, nullptr);
    }
    return *this;
}

HRESULT CHttpClient::DoRequest(_In_ RequestParams_t &sRequestParams) noexcept
{
    std::map<std::wstring, std::wstring> aRequestHeaders;
    std::wstring strHostname;
    std::wstring strPath;
    INTERNET_PORT nPort;
    std::wstring strAgentHeader, strAcceptHeader;
    std::vector<std::wstring> aAcceptTypes;
    std::vector<LPCWSTR> aAcceptPtrs;
    OSVERSIONINFOW sOsviW;
    DWORD dwBuildNumber, dwHttpOpts;
    BOOL bHttpOpts;
    struct tcp_keepalive sKeepAliveSettings;
    BOOL bIsHttps, bIsWebSocket;
    HANDLE hEvents[2];
    HRESULT hRes;

    // Validate parameters
    if ((!sRequestParams.szUrlW) || sRequestParams.szUrlW[0] == 0)
    {
        return E_INVALIDARG;
    }
    hRes = ParseUrl(sRequestParams.szUrlW, strHostname, strPath, nPort, bIsHttps, bIsWebSocket);
    if (FAILED(hRes))
    {
        return hRes;
    }

    // WebSocket requests must use GET method, if specified
    if (bIsWebSocket && sRequestParams.szMethodW && _wcsicmp(sRequestParams.szMethodW, L"GET") != 0)
    {
        return E_INVALIDARG;
    }

    // Parse and validate headers
    if (sRequestParams.nHeadersCount > 0 && (!sRequestParams.lpszHeadersW))
    {
        return E_INVALIDARG;
    }
    hRes = SanitizeHeaders(sRequestParams.lpszHeadersW, sRequestParams.nHeadersCount, aRequestHeaders);
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Check optional body
    if (sRequestParams.nBodyLen > 0 && (!sRequestParams.lpBody))
    {
        return E_INVALIDARG;
    }

    // If dealing with websocket, validate callbacks
    if (bIsWebSocket)
    {
        if (!(sRequestParams.cWsDataReceivedCallback && sRequestParams.cWsClosedCallback))
        {
            return E_INVALIDARG;
        }
    }

    // Find and extract agent header, if any provided
    hRes = TranslateToHresult([&]()
    {
        for (auto it = aRequestHeaders.begin(); it != aRequestHeaders.end();)
        {
            if (strAgentHeader.empty() && _wcsicmp(it->first.c_str(), L"Agent") == 0)
            {
                strAgentHeader = std::move(it->second);
                it = aRequestHeaders.erase(it);
                continue;
            }
            if (strAcceptHeader.empty() && _wcsicmp(it->first.c_str(), L"Accept") == 0)
            {
                strAcceptHeader = std::move(it->second);
                it = aRequestHeaders.erase(it);
                continue;
            }

            ++it;
        }
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Get Windows build number for setting options later
    memset(&sOsviW, 0, sizeof(sOsviW));
    sOsviW.dwOSVersionInfoSize = sizeof(sOsviW);
    hRes = GetOsVersion(&sOsviW);
    if (FAILED(hRes))
    {
        return hRes;
    }

    dwBuildNumber = 0;
    if (sOsviW.dwMajorVersion > 10)
    {
        dwBuildNumber = MAXDWORD;
    }
    else if (sOsviW.dwMajorVersion == 10)
    {
        dwBuildNumber = (sOsviW.dwMinorVersion > 0) ? MAXDWORD : sOsviW.dwBuildNumber;
    }

    // Clean old request
    if (!Cleanup(FALSE))
    {
        // We cannot initiate a new request if there is one being shut down
        return HRESULT_FROM_WIN32(ERROR_OPERATION_IN_PROGRESS);
    }
    WaitForCleanupCompletion();
    RundownProt_Initialize(&m_nRundownProt);

    {
        // Request-owned mutable state below is intentionally updated by WinHTTP callbacks while DoRequest() is active.
        // m_cMutex excludes competing public request owners; it is not used to re-serialize the callback path.
        CAutoRundownProtection cAutoRundownProt(&m_nRundownProt);

        if (!cAutoRundownProt.IsAcquired())
        {
            return E_NOT_VALID_STATE;
        }

        CMutex::CAutoLock cLock(m_cMutex);

        // Create session
        m_hSession = ::WinHttpOpen((!strAgentHeader.empty()) ? strAgentHeader.c_str() : szDefaultUserAgentW,
                                 WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        if (!m_hSession)
        {
            hRes = HRESULT_FROM_LASTERROR();
            goto done;
        }

        // Set callback, context and other options
        if (::WinHttpSetStatusCallback(m_hSession, CHttpClient::StaticAsyncCallback,
                                       WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_CLOSE_CONNECTION |
                                       WINHTTP_CALLBACK_FLAG_HANDLES,
                                       0) == WINHTTP_INVALID_STATUS_CALLBACK)
        {
            hRes = HRESULT_FROM_LASTERROR();
            goto done;
        }

        // Initialize keep alive options. Also, we don't use on PING/PONG control frames.
        sKeepAliveSettings.onoff = 1;
        sKeepAliveSettings.keepalivetime = WS_KEEPALIVE_TIME_MS;
        sKeepAliveSettings.keepaliveinterval = WS_KEEPALIVE_TIME_MS;
        if (!::WinHttpSetOption(m_hSession, WINHTTP_OPTION_TCP_KEEPALIVE, &sKeepAliveSettings, (DWORD)sizeof(sKeepAliveSettings)))
        {
            hRes = HRESULT_FROM_LASTERROR();
            goto done;
        }

        // Set minimum TLS version to 1.2 and enable decompression. Also, enable IPv6 fast fallback and TCP fast open if supported.
        // For websocket requests, also enable TLS false start if supported.
        if (bIsHttps != FALSE)
        {
            dwHttpOpts = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
            if (!::WinHttpSetOption(m_hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &dwHttpOpts, (DWORD)sizeof(dwHttpOpts)))
            {
                hRes = HRESULT_FROM_LASTERROR();
                goto done;
            }
        }
        // Enable decompression support.
        dwHttpOpts = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
        if (!::WinHttpSetOption(m_hSession, WINHTTP_OPTION_DECOMPRESSION, &dwHttpOpts, (DWORD)sizeof(dwHttpOpts)))
        {
            hRes = HRESULT_FROM_LASTERROR();
            goto done;
        }
        // Enable IPv6 fast fallback.
        if (dwBuildNumber >= 18362)
        {
            bHttpOpts = TRUE;
            if (!::WinHttpSetOption(m_hSession, WINHTTP_OPTION_IPV6_FAST_FALLBACK, &bHttpOpts, (DWORD)sizeof(bHttpOpts)))
            {
                hRes = HRESULT_FROM_LASTERROR();
                goto done;
            }
        }
        // Enable TCP fast open and enable TLS false start if supported.
        if (dwBuildNumber >= 19041)
        {
            bHttpOpts = TRUE;
            if (!::WinHttpSetOption(m_hSession, WINHTTP_OPTION_TCP_FAST_OPEN, &bHttpOpts, (DWORD)sizeof(bHttpOpts)))
            {
                hRes = HRESULT_FROM_LASTERROR();
                goto done;
            }
            if (bIsHttps != FALSE)
            {
                if (!::WinHttpSetOption(m_hSession, WINHTTP_OPTION_TLS_FALSE_START, &bHttpOpts, (DWORD)sizeof(bHttpOpts)))
                {
                    hRes = HRESULT_FROM_LASTERROR();
                    goto done;
                }
            }
        }
        // Disable global connection pooling to avoid potential issues with WebSocket connections getting reused for non-WebSocket
        // requests.
        if (dwBuildNumber >= 22000)
        {
            bHttpOpts = TRUE;
            if (!::WinHttpSetOption(m_hSession, WINHTTP_OPTION_DISABLE_GLOBAL_POOLING, &bHttpOpts, (DWORD)sizeof(bHttpOpts)))
            {
                hRes = HRESULT_FROM_LASTERROR();
                goto done;
            }
        }

        // Connect
        m_hConnect = ::WinHttpConnect(m_hSession, strHostname.c_str(), nPort, 0);
        if (!m_hConnect)
        {
            hRes = HRESULT_FROM_LASTERROR();
            goto done;
        }

        // Parse accept types
        if (!strAcceptHeader.empty())
        {
            hRes = TranslateToHresult([&]()
            {
                aAcceptTypes = ParseAcceptTypes(strAcceptHeader);

                aAcceptPtrs.reserve(aAcceptTypes.size() + 1);

                for (auto &t : aAcceptTypes)
                {
                    aAcceptPtrs.push_back(t.c_str());
                }
                aAcceptPtrs.push_back(nullptr);
            });
            if (FAILED(hRes))
            {
                goto done;
            }
        }

        // Open request
        m_hRequest = ::WinHttpOpenRequest(m_hConnect,
                                        ((sRequestParams.szMethodW && sRequestParams.szMethodW[0] != 0)
                                         ? sRequestParams.szMethodW : L"GET"),
                                        strPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                        ((aAcceptPtrs.size() > 0) ? aAcceptPtrs.data() : WINHTTP_DEFAULT_ACCEPT_TYPES),
                                        (bIsHttps ? WINHTTP_FLAG_SECURE : 0));
        if (!m_hRequest)
        {
            hRes = HRESULT_FROM_LASTERROR();
            goto done;
        }

        // If a websocket request, set upgrade option
        if (bIsWebSocket)
        {
            if (!::WinHttpSetOption(m_hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
            {
                hRes = HRESULT_FROM_LASTERROR();
                goto done;
            }
        }

        // Add headers
        if (!aRequestHeaders.empty())
        {
            std::wstring strAllHeaders;

            hRes = TranslateToHresult([&]()
            {
                for (const auto &header : aRequestHeaders)
                {
                    strAllHeaders += header.first + L": " + header.second + L"\r\n";
                }
            });
            if (FAILED(hRes))
            {
                goto done;
            }

            if (!strAllHeaders.empty())
            {
                if (!::WinHttpAddRequestHeaders(m_hRequest, strAllHeaders.c_str(), (DWORD)(strAllHeaders.length()),
                                                WINHTTP_ADDREQ_FLAG_ADD))
                {
                    hRes = HRESULT_FROM_LASTERROR();
                    goto done;
                }
            }
        }

        // Create request completion event
        m_hRequestCompletedEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_hRequestCompletedEvent)
        {
            hRes = HRESULT_FROM_LASTERROR();
            goto done;
        }

        // Send request
        if (!::WinHttpSendRequest(m_hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)sRequestParams.lpBody,
                                  (DWORD)sRequestParams.nBodyLen, (DWORD)sRequestParams.nBodyLen, (DWORD_PTR)this))
        {
            hRes = HRESULT_FROM_LASTERROR();
            goto done;
        }

        // After WinHttpSendRequest() hands off to WinHTTP, callbacks populate the request-owned response state and then
        // signal m_hRequestCompletedEvent. DoRequest() only consumes that state after the wait finishes.
        // Wait for request to complete
        hEvents[0] = m_hRequestCompletedEvent;
        hEvents[1] = sRequestParams.hAbortEvent;
        switch (::WaitForMultipleObjects(sRequestParams.hAbortEvent ? 2 : 1, hEvents, FALSE,
                                         (sRequestParams.dwTimeoutMs != 0) ? sRequestParams.dwTimeoutMs : INFINITE))
        {
            case WAIT_OBJECT_0:
                // Get error
                hRes = _InterlockedRead(&m_hrReadLastError);
                break;

            case WAIT_OBJECT_0 + 1:
                hRes = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                break;

            case WAIT_TIMEOUT:
                hRes = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                break;

            default:
                hRes = E_UNEXPECTED;
                break;
        }

        // Complete upgrade if websocket
        if (SUCCEEDED(hRes) && bIsWebSocket)
        {
            if (m_dwStatusCode == 101)
            {
                // Save callbacks
                m_cWsDataReceivedCallback = sRequestParams.cWsDataReceivedCallback;
                m_cWsClosedCallback = sRequestParams.cWsClosedCallback;
                m_cWsErrorCallback = sRequestParams.cWsErrorCallback;

                // Mark as waiting for init
                _InterlockedOr(&m_nStateMask, STATE_MASK_WAITING_FOR_WS_INIT);
            }
            else
            {
                hRes = S_FALSE;
            }
        }

done:
        ;
    }

    // Do not cleanup on a successful websocket upgrade
    if (!(bIsWebSocket && hRes == S_OK))
    {
        Cleanup(SUCCEEDED(hRes));
        WaitForCleanupCompletion();
    }

    // Done
    return hRes;
}

DWORD CHttpClient::GetStatusCode() const noexcept
{
    CMutex::CAutoLock cLock(m_cMutex);

    return m_dwStatusCode;
}

HRESULT CHttpClient::GetResponseBody(_Out_ CResponseBody &cResponseBody) noexcept
{
    CMutex::CAutoLock cLock(m_cMutex);

    return TranslateToHresult([&]()
    {
        if (!m_cResponseBody)
        {
            cResponseBody = std::make_shared<std::vector<BYTE>>();
        }
        else
        {
            cResponseBody = m_cResponseBody;
        }
    });
}

HRESULT CHttpClient::GetResponseHeader(_In_z_ LPCWSTR szHeaderNameW, _Out_ std::wstring &strHeaderValue, _In_opt_ int iSubIndex) noexcept
{
    strHeaderValue.clear();

    if ((!szHeaderNameW) || szHeaderNameW[0] == 0)
    {
        return E_INVALIDARG;
    }

    {
        CMutex::CAutoLock cLock(m_cMutex);

        for (const auto &header : m_aResponseHeaders)
        {
            if (_wcsicmp(header.first.c_str(), szHeaderNameW) == 0)
            {
                if (iSubIndex <= 0)
                {
                    return TranslateToHresult([&]()
                    {
                        strHeaderValue = header.second;
                    });
                }

                iSubIndex--;
            }
        }
    }

    // Not set
    return E_NOT_SET;
}

HRESULT CHttpClient::WsInitiate() noexcept
{
    CMutex::CAutoLock cLock(m_cMutex);
    DWORD dwTimeoutMs;
    HRESULT hRes;

    if ((!m_hRequest) || (_InterlockedRead(&m_nStateMask) & STATE_MASK_WAITING_FOR_WS_INIT) == 0)
    {
        return E_NOT_SET;
    }

    // Complete upgrade
    m_hWebSocket = ::WinHttpWebSocketCompleteUpgrade(m_hRequest, (DWORD_PTR)this);
    if (!m_hWebSocket)
    {
        hRes = HRESULT_FROM_LASTERROR();
        goto done;
    }

    // Set close timeout to 10 seconds
    dwTimeoutMs = 10000;
    if (!::WinHttpSetOption(m_hWebSocket, WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &dwTimeoutMs, (DWORD)sizeof(dwTimeoutMs)))
    {
        hRes = HRESULT_FROM_LASTERROR();
        goto done;
    }

    // Set keep-alive interval to max
    // NOTE: WinHTTP seems to expect to receive a frame from server within the keep-alive interval, it doesn't seems to be
    //       related to a websocket PING/PONG frames. Because our server doesn't send these type of frames, the connection
    //       is closed when there is no activity.
    dwTimeoutMs = MAXDWORD;
    if (!::WinHttpSetOption(m_hWebSocket, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, &dwTimeoutMs, (DWORD)sizeof(dwTimeoutMs)))
    {
        hRes = HRESULT_FROM_LASTERROR();
        goto done;
    }

    // Prepare read buffer
    hRes = ResizeRxBuffer(4096);
    if (FAILED(hRes))
    {
        goto done;
    }

    // Set initial async read
#pragma warning(suppress : 6387)
    hRes = HRESULT_FROM_WIN32(::WinHttpWebSocketReceive(m_hWebSocket, m_aRxBuffer.data(), (DWORD)m_aRxBuffer.size(), nullptr, nullptr));
    if (FAILED(hRes))
    {
        goto done;
    }

// Done
done:
if (FAILED(hRes))
{
    Cleanup(FALSE);
}
return hRes;
}

HRESULT CHttpClient::WsSend(_In_ LPCVOID lpData, _In_ SIZE_T nDataLen, _In_ BOOL bAsText) noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_nRundownProt);
    HRESULT hRes;

    if ((!lpData) || nDataLen == 0 || nDataLen > 0xFFFFFFFF)
    {
        return E_INVALIDARG;
    }

    if ((!cAutoRundownProt.IsAcquired()) || (!m_hWebSocket))
    {
        return E_NOT_VALID_STATE;
    }

    // Send message
    hRes = HRESULT_FROM_WIN32(::WinHttpWebSocketSend(m_hWebSocket,
                                                        (bAsText) ? WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE
                                                        : WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                                        const_cast<LPVOID>(lpData), (DWORD)nDataLen));
    // Done
    return hRes;
}

HRESULT CHttpClient::WsClose(_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA) noexcept
{
    DWORD dwReasonLen;
    HINTERNET hWebSocketLocal = nullptr;
    WsClosedCallback cWsClosedCallback;
    CHAR szCloseReasonCopyA[MAX_CLOSE_REASON_LEN + 1]{};
    BOOL bCallCloseCb = FALSE;
    LONG nPrevStateMask;

    dwReasonLen = (DWORD)strlen(szReasonA ? szReasonA : "");
    if (dwReasonLen == 0)
    {
        szReasonA = nullptr;
    }
    else if (dwReasonLen > 123)
    {
        return E_INVALIDARG;
    }

    {
        CAutoRundownProtection cAutoRundownProt(&m_nRundownProt);

        if ((!cAutoRundownProt.IsAcquired()) || (!m_hWebSocket))
        {
            return E_NOT_VALID_STATE;
        }

        nPrevStateMask = _InterlockedOr(&m_nStateMask, STATE_MASK_SOCKET_CLOSE_CALLED | STATE_MASK_WS_CLOSE_OR_ERROR_CB_CALLED);
        if ((nPrevStateMask & STATE_MASK_SOCKET_CLOSE_CALLED) == 0)
        {
            hWebSocketLocal = m_hWebSocket;

            if ((nPrevStateMask & STATE_MASK_WS_CLOSE_OR_ERROR_CB_CALLED) == 0 && m_cWsClosedCallback)
            {
                cWsClosedCallback = m_cWsClosedCallback;
                if (szReasonA)
                {
                    strcpy_s(szCloseReasonCopyA, szReasonA);
                }
                bCallCloseCb = TRUE;
            }
        }
    }

    if (hWebSocketLocal)
    {
        ::WinHttpWebSocketClose(hWebSocketLocal, nCode, const_cast<LPSTR>(szReasonA), dwReasonLen);
    }

    Cleanup(FALSE);

    if (bCallCloseCb != FALSE && cWsClosedCallback)
    {
        cWsClosedCallback(nCode, szReasonA ? szCloseReasonCopyA : nullptr);
    }

    // Done
    return S_OK;
}

VOID CHttpClient::WsForceClose() noexcept
{
    _InterlockedOr(&m_nStateMask, STATE_MASK_WS_CLOSE_OR_ERROR_CB_CALLED);

    Cleanup(FALSE);
}

HRESULT CHttpClient::ResizeRxBuffer(_In_ SIZE_T nSize) noexcept
{
    return TranslateToHresult([&]()
    {
        m_aRxBuffer.resize(nSize);
    });
}

VOID CHttpClient::CompleteRequest(_In_ HRESULT hRes) noexcept
{
    _InterlockedCompareExchange(&m_hrReadLastError, hRes, S_OK);
    ::SetEvent(m_hRequestCompletedEvent);
}

BOOL CHttpClient::Cleanup(_In_ BOOL bLeaveResponse) noexcept
{
    if ((_InterlockedOr(&m_nStateMask, STATE_MASK_CLEANUP_IN_PROGRESS) & STATE_MASK_CLEANUP_IN_PROGRESS) != 0)
    {
        return FALSE; // Cleanup already in progress
    }

    RundownProt_WaitForRelease(&m_nRundownProt);

    if (m_hWebSocket)
    {
        ::WinHttpCloseHandle(m_hWebSocket);
        m_hWebSocket = nullptr;
    }
    if (m_hRequest)
    {
        _InterlockedExchangePointer(&m_lpClosingRequestHandle, m_hRequest);
        if (!::WinHttpCloseHandle(m_hRequest))
        {
            _InterlockedExchangePointer(&m_lpClosingRequestHandle, nullptr);
            ::WakeByAddressSingle((PVOID)&m_lpClosingRequestHandle);
        }
        m_hRequest = nullptr;
    }
    if (m_hConnect)
    {
        ::WinHttpCloseHandle(m_hConnect);
        m_hConnect = nullptr;
    }
    if (m_hSession)
    {
        ::WinHttpCloseHandle(m_hSession);
        m_hSession = nullptr;
        //XXX
    }
    if (m_hRequestCompletedEvent)
    {
        ::CloseHandle(m_hRequestCompletedEvent);
        m_hRequestCompletedEvent = nullptr;
    }

    m_cWsDataReceivedCallback = nullptr;
    m_cWsClosedCallback = nullptr;
    m_cWsErrorCallback = nullptr;

    m_nCurrentWsRxMsgType = CURRENT_MESSAGE_TYPE_NONE;
    m_aCurrentWsRxMsg.clear();

    m_aRxBuffer.clear();

    _InterlockedExchange(&m_hrReadLastError, S_OK);

    if (!bLeaveResponse)
    {
        CMutex::CAutoLock cLock(m_cMutex);

        m_dwStatusCode = 0;
        m_aResponseHeaders.clear();
        m_cResponseBody.reset();
    }

    _InterlockedExchange(&m_nStateMask, 0);

    // Done
    return TRUE;
}

VOID CHttpClient::WaitForCleanupCompletion() noexcept
{
    while (true)
    {
        PVOID lpClosingHandle;

        lpClosingHandle = _InterlockedCompareExchangePointer(&m_lpClosingRequestHandle, nullptr, nullptr);
        if (!lpClosingHandle)
        {
            break;
        }
        ::WaitOnAddress((volatile VOID *)&m_lpClosingRequestHandle, &lpClosingHandle, sizeof(lpClosingHandle), INFINITE);
    }
    while ((_InterlockedRead(&m_nStateMask) & STATE_MASK_CLEANUP_IN_PROGRESS) != 0)
    {
        ::Sleep(20);
    }
}

VOID CALLBACK CHttpClient::StaticAsyncCallback(_In_ HINTERNET hInternet, _In_ DWORD_PTR dwContext,
                                               _In_ DWORD dwInternetStatus, _In_ LPVOID lpvStatusInformation,
                                               _In_ DWORD dwStatusInformationLength) noexcept
{
    CHttpClient *lpThis = reinterpret_cast<CHttpClient *>(dwContext);
    DelayedCallCallback_t sDcc;

    if (!lpThis)
    {
        return;
    }

    if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
    {
        lpThis->OnHandleClosingCallback(hInternet);
        return;
    }

    lpThis->AsyncCallback(hInternet, dwInternetStatus, lpvStatusInformation, dwStatusInformationLength, &sDcc);
    if (sDcc.bRunCleanup)
    {
        lpThis->Cleanup(FALSE);
    }

    if (sDcc.cWsDataReceivedCallback)
    {
        sDcc.cWsDataReceivedCallback(sDcc.data.data(), sDcc.data.size(), sDcc.bDataIsText);
    }
    if (sDcc.cWsClosedCallback)
    {
        sDcc.cWsClosedCallback(sDcc.nCloseCode, sDcc.szCloseReasonA);
    }
    if (sDcc.cWsErrorCallback)
    {
        sDcc.cWsErrorCallback(sDcc.hrError, sDcc.bErrorReadOperation);
    }
}

VOID CHttpClient::OnHandleClosingCallback(_In_ HINTERNET hInternet) noexcept
{
    if (_InterlockedCompareExchangePointer(&m_lpClosingRequestHandle, nullptr, hInternet) == hInternet)
    {
        ::WakeByAddressSingle((PVOID)&m_lpClosingRequestHandle);
    }
}

VOID CHttpClient::AsyncCallback(_In_ HINTERNET hInternet, _In_ DWORD dwInternetStatus, _In_ LPVOID lpvStatusInformation,
                                _In_ DWORD dwStatusInformationLength, _Inout_ DelayedCallCallback_t *lpDcc) noexcept
{
    // WinHTTP callback completion for the active request is treated as serialized here. The callback path is therefore
    // the request-side owner of m_dwStatusCode, m_aResponseHeaders, m_cResponseBody, m_aRxBuffer, and websocket RX state.
    CAutoRundownProtection cAutoRundownProt(&m_nRundownProt);
    HRESULT hRes;

    if (!cAutoRundownProt.IsAcquired())
    {
        return;
    }

    switch (dwInternetStatus)
    {
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            if (!::WinHttpReceiveResponse(m_hRequest, nullptr))
            {
                CompleteRequest(HRESULT_FROM_LASTERROR());
                break;
            }
            break;

        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            hRes = OnResponseHeadersAvailable();
            if (FAILED(hRes))
            {
                CompleteRequest(hRes);
                break;
            }
            if (!::WinHttpQueryDataAvailable(m_hRequest, nullptr))
            {
                CompleteRequest(HRESULT_FROM_LASTERROR());
                break;
            }
            break;

        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
            if (*((LPDWORD)lpvStatusInformation) > 0)
            {
                OnResponseDataAvailable(*((LPDWORD)lpvStatusInformation));
            }
            else
            {
                // End of data
                CompleteRequest(S_OK); // Signal completion
            }
            break;

        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            if (m_hWebSocket)
            {
                OnWsReadComplete(reinterpret_cast<WINHTTP_WEB_SOCKET_STATUS *>(lpvStatusInformation), lpDcc);
            }
            else
            {
                hRes = OnResponseDataRead((DWORD)dwStatusInformationLength);
                if (SUCCEEDED(hRes))
                {
                    // Continue reading
                    if (!::WinHttpQueryDataAvailable(m_hRequest, nullptr))
                    {
                        hRes = HRESULT_FROM_LASTERROR();
                    }
                }
                if (FAILED(hRes))
                {
                    CompleteRequest(hRes);
                }
            }
            break;

        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            {
                LPWINHTTP_ASYNC_RESULT lpAsyncResult = reinterpret_cast<LPWINHTTP_ASYNC_RESULT>(lpvStatusInformation);

                hRes = HRESULT_FROM_WIN32(lpAsyncResult->dwError);

                if (m_hWebSocket)
                {
                    WINHTTP_WEB_SOCKET_ASYNC_RESULT *lpWsAsyncResult;

                    lpWsAsyncResult = reinterpret_cast<WINHTTP_WEB_SOCKET_ASYNC_RESULT *>(lpvStatusInformation);
                    switch (lpWsAsyncResult->Operation)
                    {
                        case WINHTTP_WEB_SOCKET_SEND_OPERATION:
                            OnWsError(hRes, FALSE, lpDcc);
                            break;

                        case WINHTTP_WEB_SOCKET_RECEIVE_OPERATION:
                            OnWsError(hRes, TRUE, lpDcc);
                            break;

                        // case WINHTTP_WEB_SOCKET_CLOSE_OPERATION: // Not used
                    }
                }
                else
                {
                    CompleteRequest(hRes); // Signal completion
                }
            }
            break;

        case WINHTTP_CALLBACK_STATUS_CONNECTION_CLOSED:
            hRes = S_OK;
            break;

    }
}

HRESULT CHttpClient::OnResponseHeadersAvailable() noexcept
{
    DWORD _dwStatusCode;
    std::map<std::wstring, std::wstring> responseHeaders;
    DWORD dwSize;
    HRESULT hRes;

    // Callback-owned response snapshot update for the in-flight request.
    // DoRequest() reads the published values only after CompleteRequest() signals completion.
    // Get status code
    dwSize = sizeof(_dwStatusCode);
    if (!::WinHttpQueryHeaders(m_hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                               &_dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX))
    {
        return HRESULT_FROM_LASTERROR();
    }

    // Get all headers
    hRes = S_OK;
    dwSize = 0;
    if (!::WinHttpQueryHeaders(m_hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                               &dwSize, WINHTTP_NO_HEADER_INDEX))
    {
        hRes = HRESULT_FROM_LASTERROR();
        if (hRes == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
        {
            std::wstring strRawHeaders;

            hRes = TranslateToHresult([&]()
            {
                strRawHeaders.resize((SIZE_T)dwSize / sizeof(wchar_t));
            });
            if (SUCCEEDED(hRes))
            {
                if (::WinHttpQueryHeaders(m_hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                          strRawHeaders.data(), &dwSize, WINHTTP_NO_HEADER_INDEX))
                {
                    hRes = TranslateToHresult([&]()
                    {
                        strRawHeaders.resize(wcslen(strRawHeaders.c_str()));
                        responseHeaders = ParseResponseHeaders(strRawHeaders);
                    });
                }
                else
                {
                    return HRESULT_FROM_LASTERROR();
                }
            }
        }
    }

    // Done
    if (SUCCEEDED(hRes))
    {
        m_dwStatusCode = _dwStatusCode;
        m_aResponseHeaders = std::move(responseHeaders);
    }
    return hRes;
}

HRESULT CHttpClient::OnResponseDataAvailable(_In_ DWORD dwBytesAvailable) noexcept
{
    HRESULT hRes;

    // Setup buffer to hold data
    hRes = ResizeRxBuffer((SIZE_T)dwBytesAvailable);
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Read data
    if (!::WinHttpReadData(m_hRequest, m_aRxBuffer.data(), dwBytesAvailable, nullptr))
    {
        return HRESULT_FROM_LASTERROR();
    }

    // Done
    return S_OK;
}

HRESULT CHttpClient::OnResponseDataRead(_In_ DWORD dwBytesRead) noexcept
{
    if (dwBytesRead == 0)
    {
        return S_OK;
    }

    // Callback-owned append into the request response buffer.
    return TranslateToHresult([&]()
    {
        if (!m_cResponseBody)
        {
            m_cResponseBody = std::make_shared<std::vector<BYTE>>();
        }
        m_cResponseBody->insert(m_cResponseBody->end(), m_aRxBuffer.begin(), m_aRxBuffer.begin() + (SIZE_T)dwBytesRead);
    });
}

VOID CHttpClient::OnWsReadComplete(_In_ WINHTTP_WEB_SOCKET_STATUS *lpWsStatus, _Inout_ DelayedCallCallback_t *lpDcc) noexcept
{
    HRESULT hRes;

    switch (lpWsStatus->eBufferType)
    {
        case WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE:
        case WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE:
        case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE:
        case WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE:
            hRes = OnWsIncomingMessage(lpWsStatus, lpDcc);
            break;

        case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE:
            OnWsIncomingClose(lpWsStatus, lpDcc);
            hRes = S_OK;
            break;

        default:
            hRes = E_UNEXPECTED;
            break;
    }

    if (SUCCEEDED(hRes))
    {
#pragma warning(suppress : 6387)
        hRes = HRESULT_FROM_WIN32(::WinHttpWebSocketReceive(m_hWebSocket, m_aRxBuffer.data(), (DWORD)m_aRxBuffer.size(), nullptr,
                                                            nullptr));
    }

    // Done
    if (SUCCEEDED(hRes))
    {
        if (lpWsStatus->eBufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
        {
            lpDcc->bRunCleanup = TRUE;
        }
    }
    else
    {
        OnWsError(hRes, TRUE, lpDcc);
    }
}

HRESULT CHttpClient::OnWsIncomingMessage(_In_ WINHTTP_WEB_SOCKET_STATUS *lpWsStatus, _Inout_ DelayedCallCallback_t *lpDcc) noexcept
{
    HRESULT hRes;

    if (lpWsStatus->eBufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
        lpWsStatus->eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
    {
        // A text frame cannot appear in the middle of a binary frame set
        if (m_nCurrentWsRxMsgType == CURRENT_MESSAGE_TYPE_BINARY)
        {
            return HRESULT_FROM_WIN32(ERROR_DATA_NOT_ACCEPTED);
        }

        // Set current message type
        m_nCurrentWsRxMsgType = CURRENT_MESSAGE_TYPE_TEXT;
    }
    else
    {
        // A binary frame cannot appear in the middle of a text frame set
        if (m_nCurrentWsRxMsgType == CURRENT_MESSAGE_TYPE_TEXT)
        {
            return HRESULT_FROM_WIN32(ERROR_DATA_NOT_ACCEPTED);
        }

        // Set current message type
        m_nCurrentWsRxMsgType = CURRENT_MESSAGE_TYPE_BINARY;
    }

    // Add buffer
    if (lpWsStatus->dwBytesTransferred > 0)
    {
        hRes = TranslateToHresult([&]()
        {
            m_aCurrentWsRxMsg.insert(m_aCurrentWsRxMsg.end(), m_aRxBuffer.begin(),
                                   m_aRxBuffer.begin() + (SIZE_T)(lpWsStatus->dwBytesTransferred));
        });
        if (FAILED(hRes))
        {
            return hRes;
        }
    }

    // Call callback if the message ended
    if (lpWsStatus->eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
        lpWsStatus->eBufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
    {
        m_nCurrentWsRxMsgType = CURRENT_MESSAGE_TYPE_NONE;

        if (m_cWsDataReceivedCallback)
        {
            lpDcc->cWsDataReceivedCallback = m_cWsDataReceivedCallback;
            lpDcc->data = std::move(m_aCurrentWsRxMsg);
            lpDcc->bDataIsText = (lpWsStatus->eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) ? TRUE : FALSE;
        }
        else
        {
            m_aCurrentWsRxMsg.clear();
        }
    }

    // Done
    return S_OK;
}

VOID CHttpClient::OnWsIncomingClose(_In_ WINHTTP_WEB_SOCKET_STATUS *lpWsStatus, _Inout_ DelayedCallCallback_t *lpDcc) noexcept
{
    USHORT nCloseCode;
    CHAR szReasonA[MAX_CLOSE_REASON_LEN + 1];
    DWORD dwReasonBytes;
    HRESULT hRes;

    nCloseCode = 0;
    dwReasonBytes = 0;
    hRes = HRESULT_FROM_WIN32(::WinHttpWebSocketQueryCloseStatus(m_hWebSocket, &nCloseCode, szReasonA, MAX_CLOSE_REASON_LEN,
                                                                 &dwReasonBytes));
    if (SUCCEEDED(hRes))
    {
        dwReasonBytes /= sizeof(CHAR);
        while (dwReasonBytes > 0 && szReasonA[dwReasonBytes - 1] == 0)
        {
            dwReasonBytes--;
        }
        szReasonA[dwReasonBytes] = 0;
    }
    else
    {
        nCloseCode = WINHTTP_WEB_SOCKET_EMPTY_CLOSE_STATUS;
        szReasonA[0] = 0;
    }

    ::WinHttpWebSocketClose(m_hWebSocket, nCloseCode, szReasonA, static_cast<DWORD>(strlen(szReasonA)));

    if ((_InterlockedOr(&m_nStateMask, STATE_MASK_WS_CLOSE_OR_ERROR_CB_CALLED) & STATE_MASK_WS_CLOSE_OR_ERROR_CB_CALLED) == 0)
    {
        if (m_cWsClosedCallback)
        {
            lpDcc->cWsClosedCallback = m_cWsClosedCallback;
            lpDcc->nCloseCode = nCloseCode;
            strcpy_s(lpDcc->szCloseReasonA, szReasonA);
        }
    }
}

VOID CHttpClient::OnWsError(_In_ HRESULT hRes, _In_ BOOL bIsRead, _Inout_ DelayedCallCallback_t *lpDcc) noexcept
{
    if ((_InterlockedOr(&m_nStateMask, STATE_MASK_WS_CLOSE_OR_ERROR_CB_CALLED) & STATE_MASK_WS_CLOSE_OR_ERROR_CB_CALLED) == 0)
    {
        if (m_cWsErrorCallback)
        {
            lpDcc->cWsErrorCallback = m_cWsErrorCallback;
            lpDcc->hrError = hRes;
            lpDcc->bErrorReadOperation = bIsRead;
        }
    }
    lpDcc->bRunCleanup = TRUE;
}

// -----------------------------------------------------------------------------

static std::map<std::wstring, std::wstring> ParseResponseHeaders(_In_ const std::wstring &strRawHeaders)
{
    std::map<std::wstring, std::wstring> responseHeaders;
    size_t pos = 0;

    while (pos < strRawHeaders.length())
    {
        size_t lineEnd = strRawHeaders.find(L"\r\n", pos);
        if (lineEnd == std::wstring::npos)
            break;

        std::wstring line = strRawHeaders.substr(pos, lineEnd - pos);
        size_t colon = line.find(L':');
        if (colon != std::wstring::npos)
        {
            std::wstring name = line.substr(0, colon);
            std::wstring value = line.substr(colon + 1);

            // Trim leading space from value
            size_t firstNonSpace = value.find_first_not_of(L' ');
            if (firstNonSpace != std::wstring::npos)
            {
                value = value.substr(firstNonSpace);
            }

            responseHeaders[name] = value;
        }

        pos = lineEnd + 2;
    }

    return responseHeaders;
}

static std::vector<std::wstring> ParseAcceptTypes(_In_ const std::wstring &strAcceptHeader)
{
    std::vector<std::wstring> types;
    std::wstring token;

    size_t pos = 0;
    while (pos < strAcceptHeader.size())
    {
        size_t comma = strAcceptHeader.find(L',', pos);
        if (comma == std::wstring::npos)
        {
            comma = strAcceptHeader.size();
        }

        token = strAcceptHeader.substr(pos, comma - pos);
        TrimSpacesAndNewLines(token);

        // Strip parameters: everything after ';'
        size_t semi = token.find(L';');
        if (semi != std::wstring::npos)
        {
            token.resize(semi);
        }
        TrimSpacesAndNewLines(token);

        if (!token.empty())
        {
            types.push_back(token);
        }

        pos = comma + 1;
    }

    return types;
}

static HRESULT SanitizeHeaders(_In_ LPCWSTR *lpszHeadersW, _In_ SIZE_T nHeadersCount,
                               _Out_ std::map<std::wstring, std::wstring> &aOutHeaders) noexcept
{
    aOutHeaders.clear();
    for (SIZE_T i = 0; i < nHeadersCount; i++)
    {
        std::pair<std::wstring, std::wstring> pairHeader;
        HRESULT hRes;

        hRes = TranslateToHresult([&]()
        {
            hRes = SanitizeHeader(lpszHeadersW[i], pairHeader);
            if (SUCCEEDED(hRes))
            {
                aOutHeaders.insert(pairHeader);
            }
        });
        if (FAILED(hRes))
        {
            aOutHeaders.clear();
            return hRes;
        }
    }

    // Done
    return S_OK;
}

static HRESULT SanitizeHeader(_In_z_ LPCWSTR szHeaderW, _Out_ std::pair<std::wstring, std::wstring> &pairHeader) noexcept
{
    pairHeader = {}; // always initialize output

    if (!szHeaderW)
    {
        return E_INVALIDARG;
    }

    std::wstring strInput(szHeaderW);

    TrimSpacesAndNewLines(strInput);

    if (strInput.empty())
    {
        return E_INVALIDARG;
    }
    // Must be "Name: Value"
    const size_t nColonPos = strInput.find(L':');
    if (nColonPos == std::wstring::npos)
    {
        return E_INVALIDARG;
    }

    std::wstring strName = strInput.substr(0, nColonPos);
    std::wstring strValue = strInput.substr(nColonPos + 1);

    TrimSpacesAndNewLines(strName);
    TrimSpacesAndNewLines(strValue);

    if (!IsValidHeaderName(strName))
    {
        return E_INVALIDARG;
    }

    // Value may be empty (e.g., "X-Foo:" is legal in many contexts)
    if (!IsValidHeaderValue(strValue))
    {
        return E_INVALIDARG;
    }

    pairHeader.first = std::move(strName);
    pairHeader.second = std::move(strValue);

    // Done
    return S_OK;
}

static bool IsWhitespaceOrNewLine(_In_ wchar_t ch) noexcept
{
    // HTTP "optional whitespace" is SP / HTAB; we also trim other common Unicode whitespace.
    return ch == L' ' || ch == L'\t' || iswspace(ch);
}

static void TrimSpacesAndNewLines(_Inout_ std::wstring &s)
{
    size_t b = 0;
    while (b < s.size() && IsWhitespaceOrNewLine(s[b]))
    {
        ++b;
    }

    size_t e = s.size();
    while (e > b && IsWhitespaceOrNewLine(s[e - 1]))
    {
        --e;
    }
    if (b == 0 && e == s.size())
        return;

    s.assign(s.begin() + static_cast<std::ptrdiff_t>(b),
             s.begin() + static_cast<std::ptrdiff_t>(e));
}

static bool IsValidHeaderNameChar(_In_ wchar_t ch) noexcept
{
    // Conservative "token" subset: ALPHA / DIGIT / '-' (commonly safe for custom headers).
    // (You can broaden this to full RFC tchar if you need.)
    return (ch >= L'a' && ch <= L'z') ||
           (ch >= L'A' && ch <= L'Z') ||
           (ch >= L'0' && ch <= L'9') ||
           (ch == L'-');
}

static bool IsValidHeaderName(_In_ const std::wstring &name) noexcept
{
    if (name.empty())
        return false;

    for (wchar_t ch : name)
    {
        if (!IsValidHeaderNameChar(ch))
            return false;
    }
    return true;
}

static bool IsValidHeaderValue(_In_ const std::wstring &value) noexcept
{
    // Reject CR/LF and other control chars (prevents header injection / request splitting).
    for (wchar_t ch : value)
    {
        if (ch == L'\r' || ch == L'\n')
            return false;

        // Disallow ASCII control chars except HTAB; also disallow DEL.
        if ((ch < 0x20 && ch != L'\t') || ch == 0x7F)
            return false;
    }
    return true;
}

static HRESULT ParseUrl(_In_z_ LPCWSTR szUrlW, _Out_ std::wstring &strHostname, _Out_ std::wstring &strPath,
                        _Out_ INTERNET_PORT &nPort, _Out_ BOOL &bIsHttps, _Out_ BOOL &bIsWebSocket) noexcept
{
    URL_COMPONENTS sUrlComp = { 0 };
    wchar_t szHostnameW[256] = { 0 };
    wchar_t szPathW[2048] = { 0 };

    if (!szUrlW)
    {
        return E_INVALIDARG;
    }

    bIsHttps = bIsWebSocket = FALSE;
    if (_wcsnicmp(szUrlW, L"ws://", 5) == 0)
    {
        bIsWebSocket = TRUE;
        szUrlW += 5;
    }
    else if (_wcsnicmp(szUrlW, L"wss://", 6) == 0)
    {
        bIsHttps = bIsWebSocket = TRUE;
        szUrlW += 6;
    }
    else if (_wcsnicmp(szUrlW, L"http://", 7) == 0)
    {
        szUrlW += 7;
    }
    else if (_wcsnicmp(szUrlW, L"https://", 8) == 0)
    {
        bIsHttps = TRUE;
        szUrlW += 8;
    }
    else
    {
        return E_INVALIDARG;
    }

    // Check if user:pass
    for (LPCWSTR sW = szUrlW; *sW != 0 && *sW != L'/'; sW++)
    {
        if (*sW == L'@')
        {
            return E_INVALIDARG;
        }
    }

    return TranslateToHresult([&]()
    {
        std::wstring strTempUrl;

        strTempUrl = bIsHttps ? L"https://" : L"http://";
        strTempUrl += szUrlW;

        sUrlComp.dwStructSize = sizeof(sUrlComp);
        sUrlComp.lpszHostName = szHostnameW;
        sUrlComp.dwHostNameLength = _countof(szHostnameW);
        sUrlComp.lpszUrlPath = szPathW;
        sUrlComp.dwUrlPathLength = _countof(szPathW);
        if (!::WinHttpCrackUrl(strTempUrl.c_str(), 0, 0, &sUrlComp))
        {
            throw (HRESULT)HRESULT_FROM_LASTERROR();
        }

        strHostname = szHostnameW;
        strPath = szPathW;
        nPort = sUrlComp.nPort;
        if (nPort == 0)
        {
            nPort = bIsHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        }
    });
}

static HRESULT GetOsVersion(_Out_ LPOSVERSIONINFOW lpOsviW)
{
    typedef LONG (WINAPI *lpfnRtlGetVersion)(_Out_ LPOSVERSIONINFOW lpOSVersionInfo);
    static LPVOID volatile _fnRtlGetVersion = nullptr;
    lpfnRtlGetVersion fnRtlGetVersion;
    LONG nNtStatus;

    memset(lpOsviW, 0, lpOsviW->dwOSVersionInfoSize);

    fnRtlGetVersion = (lpfnRtlGetVersion)_InterlockedCompareExchangePointer(&_fnRtlGetVersion, nullptr, nullptr);
    if (!fnRtlGetVersion)
    {
        HMODULE hNtDll;

        hNtDll = ::GetModuleHandleW(L"ntdll.dll");
        if (!hNtDll)
        {
            return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
        }
        fnRtlGetVersion = (lpfnRtlGetVersion)::GetProcAddress(hNtDll, "RtlGetVersion");
        if (!fnRtlGetVersion)
        {
            return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        }
        _InterlockedCompareExchangePointer(&_fnRtlGetVersion, fnRtlGetVersion, nullptr);
    }
    nNtStatus = fnRtlGetVersion(lpOsviW);
    if (nNtStatus < 0)
    {
        return HRESULT_FROM_NT(nNtStatus);
    }
    return S_OK;
}
