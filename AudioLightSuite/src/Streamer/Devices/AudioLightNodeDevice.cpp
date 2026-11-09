#include "AudioLightNodeDevice.h"
#include "Utils\RundownProtection.h"
#include "Utils\Timer.h"
#include <algorithm>

// -----------------------------------------------------------------------------

static constexpr DWORD MIN_CONNECTING_STATUS_DURATION_MS = 250;

// -----------------------------------------------------------------------------

static VOID WaitForConnectingStatusDisplay(_In_ HANDLE hExitEvent, _In_ ULONGLONG ullConnectionAttemptStartedMs) noexcept;

// -----------------------------------------------------------------------------

CNodeLightingSubscription::CNodeLightingSubscription(_Inout_ CNodeLightingSubscription &&other) noexcept :
    m_lpNode(other.m_lpNode), m_nSubscriptionId(other.m_nSubscriptionId)
{
    other.m_lpNode = nullptr;
    other.m_nSubscriptionId = 0;
}

CNodeLightingSubscription::~CNodeLightingSubscription() noexcept
{
    Reset();
}

CNodeLightingSubscription &CNodeLightingSubscription::operator=(_Inout_ CNodeLightingSubscription &&other) noexcept
{
    if (this != &other)
    {
        Reset();
        m_lpNode = other.m_lpNode;
        m_nSubscriptionId = other.m_nSubscriptionId;
        other.m_lpNode = nullptr;
        other.m_nSubscriptionId = 0;
    }
    return *this;
}

VOID CNodeLightingSubscription::Reset() noexcept
{
    if (m_lpNode)
    {
        m_lpNode->Unsubscribe(m_nSubscriptionId);
        m_lpNode = nullptr;
        m_nSubscriptionId = 0;
    }
}

// -----------------------------------------------------------------------------

static VOID WaitForConnectingStatusDisplay(_In_ HANDLE hExitEvent, _In_ ULONGLONG ullConnectionAttemptStartedMs) noexcept
{
    ULONGLONG ullElapsedMs = GetCurrentTimeMs() - ullConnectionAttemptStartedMs;

    if (ullElapsedMs < MIN_CONNECTING_STATUS_DURATION_MS)
    {
        ::WaitForSingleObject(hExitEvent, static_cast<DWORD>(MIN_CONNECTING_STATUS_DURATION_MS - ullElapsedMs));
    }
}

// -----------------------------------------------------------------------------

CAudioLightNodeDevice::CAudioLightNodeDevice() noexcept
{
    m_sIot.cClient.OnClose([this](_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason)
    {
        OnIotClose(nCode, szReasonA, hrInternalReason);
    });
}

CAudioLightNodeDevice::~CAudioLightNodeDevice() noexcept
{
    Deinit();

    if (m_sIot.szHostNameW)
    {
        SecureZeroMemory(m_sIot.szHostNameW, wcslen(m_sIot.szHostNameW) * sizeof(WCHAR));
        free(m_sIot.szHostNameW);
    }
    if (m_sIot.szUserNameA)
    {
        SecureZeroMemory(m_sIot.szUserNameA, strlen(m_sIot.szUserNameA) * sizeof(CHAR));
        free(m_sIot.szUserNameA);
    }
    SecureZeroMemory(m_sIot.aRawPrivateKey, sizeof(m_sIot.aRawPrivateKey));
}

HRESULT CAudioLightNodeDevice::SetupCommInfo(_In_z_ LPCWSTR szHostNameW, _In_z_ LPCSTR szUserNameA,
                                             _In_ const BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES],
                                             _In_ BYTE byLightingCount) noexcept
{
    LPWSTR szHostNameCopyW;
    LPSTR szUserNameCopyA;

    if ((!szHostNameW) || *szHostNameW == 0 || (!szUserNameA) || *szUserNameA == 0 || !aRawPrivateKey ||
        byLightingCount == 0 || byLightingCount > 2)
    {
        return E_INVALIDARG;
    }

    szHostNameCopyW = _wcsdup(szHostNameW);
    if (!szHostNameCopyW)
    {
        return E_OUTOFMEMORY;
    }
    szUserNameCopyA = _strdup(szUserNameA);
    if (!szUserNameCopyA)
    {
        SecureZeroMemory(szHostNameCopyW, wcslen(szHostNameCopyW) * sizeof(WCHAR));
        free(szHostNameCopyW);
        return E_OUTOFMEMORY;
    }

    if (m_sIot.szHostNameW)
    {
        SecureZeroMemory(m_sIot.szHostNameW, wcslen(m_sIot.szHostNameW) * sizeof(WCHAR));
        free(m_sIot.szHostNameW);
    }
    if (m_sIot.szUserNameA)
    {
        SecureZeroMemory(m_sIot.szUserNameA, strlen(m_sIot.szUserNameA) * sizeof(CHAR));
        free(m_sIot.szUserNameA);
    }

    m_sIot.szHostNameW = szHostNameCopyW;
    m_sIot.szUserNameA = szUserNameCopyA;
    memcpy(m_sIot.aRawPrivateKey, aRawPrivateKey, sizeof(m_sIot.aRawPrivateKey));
    m_sIot.byLightingCount = byLightingCount;
    return S_OK;
}

HRESULT CAudioLightNodeDevice::Subscribe(_In_ BYTE byLightingSlot, _In_ IAudioLightNodeLighting *lpLighting,
                                         _Out_ CNodeLightingSubscription &subscription) noexcept
{
    Subscriber_t sSubscriber;

    if ((!lpLighting) || byLightingSlot >= m_sIot.byLightingCount || subscription.m_lpNode)
    {
        return E_INVALIDARG;
    }

    try
    {
        CMutex::CAutoLock lock(m_cSubscribersMutex);

        for (const Subscriber_t &sExisting : m_vecSubscribers)
        {
            if (sExisting.byLightingSlot == byLightingSlot)
            {
                return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
            }
        }
        sSubscriber.nId = m_nNextSubscriptionId++;
        sSubscriber.byLightingSlot = byLightingSlot;
        sSubscriber.lpLighting = lpLighting;
        m_vecSubscribers.push_back(sSubscriber);
    }
    catch (const std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    subscription.m_lpNode = this;
    subscription.m_nSubscriptionId = sSubscriber.nId;
    return S_OK;
}

VOID CAudioLightNodeDevice::Unsubscribe(_In_ SIZE_T nSubscriptionId) noexcept
{
    CMutex::CAutoLock lock(m_cSubscribersMutex);

    auto it = std::find_if(m_vecSubscribers.begin(), m_vecSubscribers.end(), [nSubscriptionId](const Subscriber_t &sSubscriber)
    {
        return sSubscriber.nId == nSubscriptionId;
    });
    if (it != m_vecSubscribers.end())
    {
        m_vecSubscribers.erase(it);
    }
}

HRESULT CAudioLightNodeDevice::Init() noexcept
{
    HRESULT hRes = m_sIot.cClientClosedEvent.Create(TRUE);

    if (SUCCEEDED(hRes))
    {
        hRes = m_sIot.cThread.Start([this](_In_ CThread &)
        {
            IotThreadProc();
        });
    }
    return hRes;
}

VOID CAudioLightNodeDevice::Deinit() noexcept
{
    m_sIot.cThread.Stop();
}

BOOL CAudioLightNodeDevice::IsConnected() const noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_lRundownProt);

    return (cAutoRundownProt.IsAcquired() && m_sIot.cClient.GetLinkState() == CIotCommClient::LinkStateConnected &&
            _InterlockedOr(&m_iConnected, 0) != 0) ? TRUE : FALSE;
}

HRESULT CAudioLightNodeDevice::SendCommand(_In_ BYTE byLightingSlot, _In_ USHORT nCmd,
                                           _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload, _In_ SIZE_T nPayloadLen,
                                           _In_ BOOL bReliable) noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_lRundownProt);
    std::vector<BYTE> vecPayload;

    if (!cAutoRundownProt.IsAcquired())
    {
        return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
    }
    if (byLightingSlot >= m_sIot.byLightingCount || (!lpPayload) || nPayloadLen == 0)
    {
        return E_INVALIDARG;
    }
    if (IsConnected() == FALSE)
    {
        return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE);
    }
    try
    {
        vecPayload.resize(nPayloadLen + 1);
    }
    catch (const std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    vecPayload[0] = byLightingSlot;
    memcpy(vecPayload.data() + 1, lpPayload, nPayloadLen);
    return (bReliable != FALSE) ? m_sIot.cClient.SendCommand(nCmd, vecPayload.data(), vecPayload.size())
                                : m_sIot.cClient.SendUdpCommand(nCmd, vecPayload.data(), vecPayload.size());
}

HRESULT CAudioLightNodeDevice::SendRawDmxChannelData(_In_ BYTE byLightingSlot, _In_ USHORT nStartChannel,
                                                     _In_reads_bytes_(nDataLen) LPCVOID lpData, _In_ SIZE_T nDataLen) noexcept
{
    return (IsConnected() != FALSE) ? m_sIot.cClient.SetRawDmxChannelDataUdpCommand(byLightingSlot, nStartChannel, lpData, nDataLen)
                                    : HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE);
}

HRESULT CAudioLightNodeDevice::SendRawWs281xRgbData(_In_ BYTE byLightingSlot, _In_ USHORT nFirstLed, _In_ USHORT nLedCount,
                                                    _In_reads_bytes_(nRgbDataLen) LPCVOID lpRgbData, _In_ SIZE_T nRgbDataLen) noexcept
{
    return (IsConnected() != FALSE) ?
        m_sIot.cClient.SetRawWs281xRgbDataUdpCommand(byLightingSlot, nFirstLed, nLedCount, lpRgbData, nRgbDataLen) :
        HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE);
}

HRESULT CAudioLightNodeDevice::ValidateLightingConfiguration(_In_ const CIotCommClient::LightingConfig_t &sConfig,
                                                             _Out_writes_z_(nReasonLen) LPWSTR szReasonW,
                                                             _In_ SIZE_T nReasonLen) noexcept
{
    if (sConfig.vecOutputs.size() != m_sIot.byLightingCount)
    {
        wcsncpy_s(szReasonW, nReasonLen, L"Node lighting count differs from the Streamer configuration", _TRUNCATE);
        return E_INVALIDARG;
    }

    // Subscriptions are fully registered before Init and removed only after Deinit, so callbacks are lock-free.
    for (const Subscriber_t &sSubscriber : m_vecSubscribers)
    {
        HRESULT hRes;

        if (sSubscriber.byLightingSlot >= sConfig.vecOutputs.size() || !sSubscriber.lpLighting)
        {
            wcsncpy_s(szReasonW, nReasonLen, L"Node lighting subscription is invalid", _TRUNCATE);
            return E_UNEXPECTED;
        }
        hRes = sSubscriber.lpLighting->ValidateNodeLightingConfiguration(sConfig.vecOutputs[sSubscriber.byLightingSlot], szReasonW,
                                                                          nReasonLen);
        if (FAILED(hRes))
        {
            return hRes;
        }
    }

    szReasonW[0] = 0;
    return S_OK;
}

VOID CAudioLightNodeDevice::NotifyConnectionState(_In_ BOOL bConnected, _In_ HRESULT hRes) noexcept
{
    for (const Subscriber_t &sSubscriber : m_vecSubscribers)
    {
        if (sSubscriber.lpLighting)
        {
            sSubscriber.lpLighting->OnNodeConnectionStateChanged(bConnected, hRes);
        }
    }
}

VOID CAudioLightNodeDevice::IotThreadProc() noexcept
{
    HANDLE hEvents[2];
    DWORD dwLoopTimeoutMs = 0;

    hEvents[0] = m_sIot.cThread.GetExitEvent();
    hEvents[1] = m_sIot.cClientClosedEvent.GetEvent();
    while (::WaitForSingleObject(hEvents[0], dwLoopTimeoutMs) == WAIT_TIMEOUT)
    {
        CIotCommClient::CLightingConfigCommandResult cLightingConfig;
        CIotCommClient::CCommandResult cResult;
        WCHAR szReasonW[128]{};
        ULONGLONG ullConnectionAttemptStartedMs;
        HRESULT hRes;
        DWORD dw;

        m_sIot.cClientClosedEvent.Reset();
        ullConnectionAttemptStartedMs = GetCurrentTimeMs();
        NotifyConnectionState(FALSE, S_OK);
        hRes = m_sIot.cClient.Connect(m_sIot.szHostNameW, m_sIot.szUserNameA, m_sIot.aRawPrivateKey, 10000, hEvents[0]);
        if (FAILED(hRes))
        {
            WaitForConnectingStatusDisplay(hEvents[0], ullConnectionAttemptStartedMs);
            NotifyConnectionState(FALSE, hRes);
            dwLoopTimeoutMs = 2000;
            continue;
        }

        cLightingConfig = m_sIot.cClient.GetLightingConfigCommand();
        if (cLightingConfig.Failed())
        {
            hRes = cLightingConfig.HResult();
        }
        else
        {
            hRes = ValidateLightingConfiguration(cLightingConfig.Config(), szReasonW, _countof(szReasonW));
        }
        if (SUCCEEDED(hRes))
        {
            cResult = m_sIot.cClient.OpenUdpChannel();
            hRes = cResult.HResult();
        }
        if (FAILED(hRes))
        {
            m_sIot.cClient.Close(WINHTTP_WEB_SOCKET_ENDPOINT_TERMINATED_CLOSE_STATUS);
            WaitForConnectingStatusDisplay(hEvents[0], ullConnectionAttemptStartedMs);
            NotifyConnectionState(FALSE, hRes);
            dwLoopTimeoutMs = 10000;
            continue;
        }

        _InterlockedExchange(&m_iConnected, 1);
        NotifyConnectionState(TRUE, S_OK);
        dw = ::WaitForMultipleObjects(_countof(hEvents), hEvents, FALSE, INFINITE);
        _InterlockedExchange(&m_iConnected, 0);
        NotifyConnectionState(FALSE, HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE));
        if (dw != WAIT_OBJECT_0 + 1)
        {
            break;
        }

        m_sIot.cClient.ForceClose();
        dwLoopTimeoutMs = 2000;
    }

    RundownProt_WaitForRelease(&m_lRundownProt);
    m_sIot.cClient.ForceClose();
}

VOID CAudioLightNodeDevice::OnIotClose(_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason) noexcept
{
    UNREFERENCED_PARAMETER(nCode);
    UNREFERENCED_PARAMETER(szReasonA);
    UNREFERENCED_PARAMETER(hrInternalReason);

    m_sIot.cClientClosedEvent.Set();
}
