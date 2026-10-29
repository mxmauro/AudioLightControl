#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "..\Utils\Mutex.h"

// -----------------------------------------------------------------------------

class CHttpClient
{
public:
    typedef std::shared_ptr<std::vector<BYTE>> CResponseBody;

    typedef std::function<VOID(_In_ const BYTE* /*lpData*/, _In_ SIZE_T /*nDataLen*/, _In_ BOOL /*bIsText*/)> WsDataReceivedCallback;
    typedef std::function<VOID(_In_ USHORT /*nCode*/, _In_opt_z_ LPCSTR /*szReasonA*/)> WsClosedCallback;
    typedef std::function<VOID(_In_ HRESULT /*hRes*/, _In_ BOOL /*bReadOperation*/)> WsErrorCallback;

    typedef struct RequestParams_s {
        LPCWSTR szUrlW{ nullptr };
        LPCWSTR szMethodW{ nullptr };
        LPCWSTR *lpszHeadersW{ nullptr };
        SIZE_T nHeadersCount{ 0 };
        LPCVOID lpBody{ nullptr };
        SIZE_T nBodyLen = 0;
        DWORD dwTimeoutMs{ INFINITE };
        HANDLE hAbortEvent{ nullptr };
        WsDataReceivedCallback cWsDataReceivedCallback;
        WsClosedCallback cWsClosedCallback;
        WsErrorCallback cWsErrorCallback;
    } RequestParams_t;

public:
    CHttpClient() noexcept = default;
    CHttpClient(_In_ const CHttpClient &) = delete;
    CHttpClient(_Inout_ CHttpClient &&other) noexcept;
    ~CHttpClient() noexcept;

    CHttpClient &operator=(_In_ const CHttpClient &) = delete;
    CHttpClient &operator=(_Inout_ CHttpClient &&other) noexcept;

    HRESULT DoRequest(_In_ RequestParams_t &sRequestParams) noexcept;

    DWORD GetStatusCode() const noexcept;

    HRESULT GetResponseBody(_Out_ CResponseBody &cResponseBody) noexcept;

    HRESULT GetResponseHeader(_In_z_ LPCWSTR szHeaderNameW, _Out_ std::wstring &strHeaderValue, _In_opt_ int iSubIndex = 0) noexcept;

    HRESULT WsInitiate() noexcept;
    HRESULT WsSend(_In_ LPCVOID lpData, _In_ SIZE_T nDataLen, _In_ BOOL bAsText) noexcept;
    HRESULT WsClose(_In_ USHORT nCode = WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, _In_opt_z_ LPCSTR szReasonA = nullptr) noexcept;
    VOID WsForceClose() noexcept;

private:
    static constexpr HRESULT MAX_CLOSE_REASON_LEN = 123;

    typedef struct DelayedCallCallback_s {
        WsDataReceivedCallback cWsDataReceivedCallback;
        WsClosedCallback cWsClosedCallback;
        WsErrorCallback cWsErrorCallback;
        std::vector<BYTE> data;
        BOOL bDataIsText{ FALSE };
        USHORT nCloseCode{ 0 };
        CHAR szCloseReasonA[MAX_CLOSE_REASON_LEN + 1]{};
        HRESULT hrError{ S_OK };
        BOOL bErrorReadOperation{ FALSE };
        BOOL bRunCleanup{ FALSE };
    } DelayedCallCallback_t;

private:
    HRESULT ResizeRxBuffer(_In_ SIZE_T nSize) noexcept;

    VOID CompleteRequest(_In_ HRESULT hRes) noexcept;

    BOOL Cleanup(_In_ BOOL bLeaveResponse) noexcept;
    VOID WaitForCleanupCompletion() noexcept;

    static VOID CALLBACK StaticAsyncCallback(_In_ HINTERNET hInternet, _In_ DWORD_PTR dwContext,
                                             _In_ DWORD dwInternetStatus, _In_ LPVOID lpvStatusInformation,
                                             _In_ DWORD dwStatusInformationLength) noexcept;
    VOID OnHandleClosingCallback(_In_ HINTERNET hInternet) noexcept;
    VOID AsyncCallback(_In_ HINTERNET hInternet, _In_ DWORD dwInternetStatus, _In_ LPVOID lpvStatusInformation,
                       _In_ DWORD dwStatusInformationLength, _Inout_ DelayedCallCallback_t *lpDcc) noexcept;

    HRESULT OnResponseHeadersAvailable() noexcept;
    HRESULT OnResponseDataAvailable(_In_ DWORD dwBytesAvailable) noexcept;
    HRESULT OnResponseDataRead(_In_ DWORD dwBytesRead) noexcept;

    VOID OnWsReadComplete(_In_ WINHTTP_WEB_SOCKET_STATUS *lpWsStatus, _Inout_ DelayedCallCallback_t *lpDcc) noexcept;
    HRESULT OnWsIncomingMessage(_In_ WINHTTP_WEB_SOCKET_STATUS *lpWsStatus, _Inout_ DelayedCallCallback_t *lpDcc) noexcept;
    VOID OnWsIncomingClose(_In_ WINHTTP_WEB_SOCKET_STATUS *lpWsStatus, _Inout_ DelayedCallCallback_t *lpDcc) noexcept;

    VOID OnWsError(_In_ HRESULT hRes, _In_ BOOL bIsRead, _Inout_ DelayedCallCallback_t *lpDcc) noexcept;

private:
    mutable CMutex m_cMutex;
    mutable LONG volatile m_nRundownProt{ 0 };

    HINTERNET m_hSession{ nullptr };
    HINTERNET m_hConnect{ nullptr };
    HINTERNET m_hRequest{ nullptr };
    HINTERNET m_hWebSocket{ nullptr };

    WsDataReceivedCallback m_cWsDataReceivedCallback;
    WsClosedCallback m_cWsClosedCallback;
    WsErrorCallback m_cWsErrorCallback;

    LONG volatile m_nStateMask{ 0 };

    std::vector<BYTE> m_aRxBuffer;

    int m_nCurrentWsRxMsgType{ 0 };
    std::vector<BYTE> m_aCurrentWsRxMsg;

    DWORD m_dwStatusCode{ 0 };
    CResponseBody m_cResponseBody;
    std::map<std::wstring, std::wstring> m_aResponseHeaders;

    HRESULT volatile m_hrReadLastError{ S_OK };
    HANDLE m_hRequestCompletedEvent{ nullptr };
    PVOID volatile m_lpClosingRequestHandle{ nullptr };
};
