#include "AudioLightNodeIotDevice.h"
#include "Utils\RundownProtection.h"

// -----------------------------------------------------------------------------

CAudioLightNodeIotDevice::CAudioLightNodeIotDevice(_In_ const CAudioProcessors &cAudioProcessors) noexcept :
    CBaseIotDevice(cAudioProcessors)
{
    m_sIot.cClient.OnMessageReceived([this](_In_ USHORT nCmd, _In_ const BYTE *lpData, _In_ SIZE_T nDataLen)
    {
        OnIotMessage(nCmd, lpData, nDataLen);
    });
    m_sIot.cClient.OnClose([this](_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason)
    {
        OnIotClose(nCode, szReasonA, hrInternalReason);
    });

    SetDeviceStatus(L"Disconnected", 0);
}

CAudioLightNodeIotDevice::~CAudioLightNodeIotDevice() noexcept
{
    DeinitComm();

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

HRESULT CAudioLightNodeIotDevice::SetupCommInfo(_In_z_ LPCWSTR szHostNameW, _In_z_ LPCSTR szUserNameA,
                                                _In_ const BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES]) noexcept
{
    LPWSTR szHostNameCopyW;
    LPSTR szUserNameCopyA;

    if ((!szHostNameW) || *szHostNameW == 0 || (!szUserNameA) || *szUserNameA == 0 || !aRawPrivateKey)
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
    memcpy(m_sIot.aRawPrivateKey, aRawPrivateKey, CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES);
    return S_OK;
}

HRESULT CAudioLightNodeIotDevice::InitComm() noexcept
{
    HRESULT hRes;

    hRes = m_sIot.cClientClosedEvent.Create(TRUE);
    if (SUCCEEDED(hRes))
    {
        hRes = m_sIot.cThread.Start([this](_In_ CThread &)
        {
            IotThreadProc();
        });
    }
    return hRes;
}

VOID CAudioLightNodeIotDevice::DeinitComm() noexcept
{
    m_sIot.cThread.Stop();
}

HRESULT CAudioLightNodeIotDevice::SendIotMessage(_In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload, _In_ SIZE_T nPayloadLen,
                                                 _In_ BOOL bReliable) noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_lRundownProt);

    if (!cAutoRundownProt.IsAcquired())
    {
        return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
    }
    if ((!lpPayload) || nPayloadLen == 0)
    {
        return E_INVALIDARG;
    }
    if (IsIotConnected() == FALSE)
    {
        return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE);
    }
    return (bReliable != FALSE) ? m_sIot.cClient.SendCommand(nCmd, lpPayload, nPayloadLen)
                                : m_sIot.cClient.SendUdpCommand(nCmd, lpPayload, nPayloadLen);
}

BOOL CAudioLightNodeIotDevice::IsIotConnected() const noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_lRundownProt);

    return (cAutoRundownProt.IsAcquired() && m_sIot.cClient.GetLinkState() == CIotCommClient::LinkStateConnected &&
            _InterlockedOr(&m_iConnected, 0) != 0) ? TRUE : FALSE;
}

VOID CAudioLightNodeIotDevice::OnIotConnectionStateChanged(_In_ BOOL bConnected) noexcept
{
    UNREFERENCED_PARAMETER(bConnected);
}

VOID CAudioLightNodeIotDevice::IotThreadProc() noexcept
{
    HANDLE hEvents[2];
    DWORD dw;
    DWORD dwLoopTimeoutMs;
    CIotCommClient::CCommandResult cResult;
    CIotCommClient::CDeviceConfigCommandResult cDeviceConfig;
    WCHAR szReasonW[128];
    BOOL bConfigurationFailed;
    HRESULT hRes;

    hEvents[0] = m_sIot.cThread.GetExitEvent();
    hEvents[1] = m_sIot.cClientClosedEvent.GetEvent();
    dwLoopTimeoutMs = 0;
    while (::WaitForSingleObject(hEvents[0], dwLoopTimeoutMs) == WAIT_TIMEOUT)
    {
        m_sIot.cClientClosedEvent.Reset();
        SetDeviceStatus(L"Connecting", 93);
        hRes = m_sIot.cClient.Connect(m_sIot.szHostNameW, m_sIot.szUserNameA, m_sIot.aRawPrivateKey, 10000, hEvents[0]);
        if (FAILED(hRes))
        {
            SetDeviceStatus(L"Disconnected", 0);
            dwLoopTimeoutMs = 2000;
            continue;
        }

        bConfigurationFailed = FALSE;
        cDeviceConfig = m_sIot.cClient.GetDeviceConfigCommand();
        if (cDeviceConfig.Failed())
        {
            SetDeviceStatus(L"Unable to read device configuration", 91);
            bConfigurationFailed = TRUE;
        }
        else if ((cDeviceConfig.CommandFlags() & CMD_FLAG_GET_DEVICE_CONFIG) == 0)
        {
            SetDeviceStatus(L"Device does not advertise configuration retrieval", 91);
            bConfigurationFailed = TRUE;
        }
        else
        {
            szReasonW[0] = 0;
            hRes = ValidateDeviceConfiguration(cDeviceConfig, szReasonW, _countof(szReasonW));
            if (FAILED(hRes))
            {
                SetDeviceStatus(szReasonW[0] != 0 ? szReasonW : L"Device configuration is incompatible", 91);
                bConfigurationFailed = TRUE;
            }
            else
            {
                cResult = m_sIot.cClient.OpenUdpChannel();
                if (cResult.Failed())
                {
                    SetDeviceStatus(L"Unable to open UDP channel", 91);
                    bConfigurationFailed = TRUE;
                }
                else
                {
                    SetDeviceStatus(L"Connected", 92);
                }
            }
        }

        if (bConfigurationFailed != FALSE)
        {
            m_sIot.cClient.Close(WINHTTP_WEB_SOCKET_ENDPOINT_TERMINATED_CLOSE_STATUS);
            dwLoopTimeoutMs = 10000;
            continue;
        }

        _InterlockedExchange(&m_iConnected, 1);
        OnIotConnectionStateChanged(TRUE);

        dw = ::WaitForMultipleObjects(_countof(hEvents), hEvents, FALSE, INFINITE);

        _InterlockedExchange(&m_iConnected, 0);
        SetDeviceStatus(L"Disconnected", 0);
        OnIotConnectionStateChanged(FALSE);

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

VOID CAudioLightNodeIotDevice::OnIotMessage(_In_ USHORT nCmd, _In_ const BYTE *lpData, _In_ SIZE_T nDataLen) noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_lRundownProt);

    UNREFERENCED_PARAMETER(nCmd);
    UNREFERENCED_PARAMETER(lpData);
    UNREFERENCED_PARAMETER(nDataLen);
}

VOID CAudioLightNodeIotDevice::OnIotClose(_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason) noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_lRundownProt);

    UNREFERENCED_PARAMETER(nCode);
    UNREFERENCED_PARAMETER(szReasonA);
    UNREFERENCED_PARAMETER(hrInternalReason);

    if (cAutoRundownProt.IsAcquired())
    {
        m_sIot.cClientClosedEvent.Set();
    }
}
