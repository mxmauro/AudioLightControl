#pragma once

#include "BaseIotDevice.h"
#include "TUI\TUiFrameWindow.h"
#include "Utils\FNV.h"
#include "Utils\RundownProtection.h"

// -----------------------------------------------------------------------------

CBaseIotDevice::CBaseIotDevice() noexcept : CTUiWindow(), CPersistantSettings()
{
    // Setup IoT client callbacks
    m_sIot.cClient.OnMessageReceived([this](_In_ USHORT nCmd, _In_ const BYTE *lpData, _In_ SIZE_T nDataLen)
    {
        this->OnIotMessage(nCmd, lpData, nDataLen);
    });
    m_sIot.cClient.OnClose([this](_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason)
    {
        this->OnIotClose(nCode, szReasonA, hrInternalReason);
    });
}

CBaseIotDevice::~CBaseIotDevice() noexcept
{
    DeinitComm();

    if (m_sIot.szHostNameW)
    {
        memset(m_sIot.szHostNameW, 0, wcslen(m_sIot.szHostNameW) * sizeof(WCHAR));
        free(m_sIot.szHostNameW);
    }
    if (m_sIot.szUserNameA)
    {
        memset(m_sIot.szUserNameA, 0, strlen(m_sIot.szUserNameA) * sizeof(CHAR));
        free(m_sIot.szUserNameA);
    }
    memset(m_sIot.aRawPrivateKey, 0, sizeof(m_sIot.aRawPrivateKey));
    if (m_szDeviceNameW)
    {
        memset(m_szDeviceNameW, 0, wcslen(m_szDeviceNameW) * sizeof(WCHAR));
        free(m_szDeviceNameW);
    }
}

HRESULT CBaseIotDevice::OnPostAttachEvent() noexcept
{
    CTUiFrameWindow *lpFrameWindow;

    lpFrameWindow = dynamic_cast<CTUiFrameWindow *>(ParentWindow());
    if (lpFrameWindow && m_szDeviceNameW)
    {
        return lpFrameWindow->SetCaption(m_szDeviceNameW);
    }

    return S_OK;
}

HRESULT CBaseIotDevice::SetupDeviceInfo(_In_z_ LPCWSTR szDeviceNameW) noexcept
{
    if ((!szDeviceNameW) || *szDeviceNameW == 0)
    {
        return E_INVALIDARG;
    }

    m_szDeviceNameW = _wcsdup(szDeviceNameW);
    if (!m_szDeviceNameW)
    {
        return E_OUTOFMEMORY;
    }

    // Done
    return S_OK;
}

HRESULT CBaseIotDevice::SetupCommInfo(_In_z_ LPCWSTR szHostNameW, _In_z_ LPCSTR szUserNameA,
                                      _In_ const BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES]) noexcept
{
    if ((!szHostNameW) || *szHostNameW == 0 || (!szUserNameA) || *szUserNameA == 0 || !aRawPrivateKey)
    {
        return E_INVALIDARG;
    }

    m_sIot.szHostNameW = _wcsdup(szHostNameW);
    if (!m_sIot.szHostNameW)
    {
error_no_mem:
        if (m_sIot.szHostNameW)
        {
            memset(m_sIot.szHostNameW, 0, wcslen(m_sIot.szHostNameW) * sizeof(WCHAR));
            free(m_sIot.szHostNameW);
            m_sIot.szHostNameW = nullptr;
        }
        if (m_sIot.szUserNameA)
        {
            memset(m_sIot.szUserNameA, 0, strlen(m_sIot.szUserNameA) * sizeof(CHAR));
            free(m_sIot.szUserNameA);
            m_sIot.szUserNameA = nullptr;
        }
        if (m_szDeviceNameW)
        {
            memset(m_szDeviceNameW, 0, wcslen(m_szDeviceNameW) * sizeof(WCHAR));
            free(m_szDeviceNameW);
            m_szDeviceNameW = nullptr;
        }
        return E_OUTOFMEMORY;
    }
    m_sIot.szUserNameA = _strdup(szUserNameA);
    if (!m_sIot.szUserNameA)
    {
        goto error_no_mem;
    }
    memcpy(m_sIot.aRawPrivateKey, aRawPrivateKey, CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES);

    // Done
    return S_OK;
}

ULONGLONG CBaseIotDevice::GetID() const noexcept
{
    LPCSTR szTypeA = GetType();
    ULONGLONG ullHash;

    ullHash = Hash::Fnv1A64(m_szDeviceNameW, wcslen(m_szDeviceNameW) * sizeof(WCHAR));
    return Hash::Fnv1A64(szTypeA, strlen(szTypeA), ullHash);
}

HRESULT CBaseIotDevice:: InitComm() noexcept
{
    HRESULT hRes;

    // Start IoT worker thread
    hRes = m_sIot.cClientClosedEvent.Create(TRUE);
    if (SUCCEEDED(hRes))
    {
        hRes = m_sIot.cThread.Start([this](_In_ CThread*)
        {
            return IotThreadProc();
        });
    }

    // Done
    return hRes;
}

VOID CBaseIotDevice::DeinitComm() noexcept
{
    m_sIot.cThread.Stop();
}

VOID CBaseIotDevice::TimerTick() noexcept
{
    // Nothing to do
}

BOOL CBaseIotDevice::SupportsManualColorActions() const noexcept
{
    return FALSE;
}

HRESULT CBaseIotDevice::OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept
{
    UNREFERENCED_PARAMETER(sAction);

    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

HRESULT CBaseIotDevice::SendIotMessage(_In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload, _In_ SIZE_T nPayloadLen,
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
    if (bReliable)
    {
        return m_sIot.cClient.SendCommand(nCmd, lpPayload, nPayloadLen);
    }
    return m_sIot.cClient.SendUdpCommand(nCmd, lpPayload, nPayloadLen);
}

BOOL CBaseIotDevice::IsIotConnected() const noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_lRundownProt);

    return (cAutoRundownProt.IsAcquired() && m_sIot.cClient.GetLinkState() == CIotCommClient::LinkStateConnected) ? TRUE : FALSE;
}

VOID CBaseIotDevice::OnIotConnectionStateChanged(_In_ BOOL bConnected) noexcept
{
    // Nothing to do
}

DWORD CBaseIotDevice::IotThreadProc() noexcept
{
    HANDLE hEvents[2];
    DWORD dw, dwLoopTimeoutMs;
    CIotCommClient::CCommandResult cResult;
    HRESULT hRes;

    hEvents[0] = m_sIot.cThread.GetExitEvent();
    hEvents[1] = m_sIot.cClientClosedEvent.GetEvent();
    dwLoopTimeoutMs = 0;
    while (::WaitForSingleObject(hEvents[0], dwLoopTimeoutMs) == WAIT_TIMEOUT)
    {
        m_sIot.cClientClosedEvent.Reset();
        hRes = m_sIot.cClient.Connect(m_sIot.szHostNameW, m_sIot.szUserNameA, m_sIot.aRawPrivateKey, 10000, hEvents[0]);
        if (FAILED(hRes))
        {
            // Cannot connect, try to reconnect after a short delay
            dwLoopTimeoutMs = 1000;
            continue;
        }

        // Open UDP channel
        cResult = m_sIot.cClient.OpenUdpChannel();
        if (cResult.Failed())
        {
            m_sIot.cClient.Close(WINHTTP_WEB_SOCKET_ENDPOINT_TERMINATED_CLOSE_STATUS);

            // Cannot open UDP channel, try to reconnect after a short delay
            dwLoopTimeoutMs = 1000;
            continue;
        }

        // Notify connection state change
        OnIotConnectionStateChanged(TRUE);

        // Wait until either exit event or client closed event is signaled
        dw = ::WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);

        // In either case, notify connection state change
        OnIotConnectionStateChanged(FALSE);

        if (dw != WAIT_OBJECT_0 + 1)
        {
            break;
        }

        // Client closed, try to reconnect after a short delay
        dwLoopTimeoutMs = 2000;
    }

    RundownProt_WaitForRelease(&m_lRundownProt);

    m_sIot.cClient.ForceClose();

    // Done
    return 0;
}

VOID CBaseIotDevice::OnIotMessage(_In_ USHORT nCmd, _In_ const BYTE *lpData, _In_ SIZE_T nDataLen) noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_lRundownProt);

    if (!cAutoRundownProt.IsAcquired())
    {
        return;
    }
}

VOID CBaseIotDevice::OnIotClose(_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason) noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_lRundownProt);

    if (cAutoRundownProt.IsAcquired())
    {
        m_sIot.cClientClosedEvent.Set();
    }
}
