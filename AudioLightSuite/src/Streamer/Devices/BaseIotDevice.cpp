#pragma once

#include "BaseIotDevice.h"
#include "TUI\TUiFrameWindow.h"
#include "Utils\FNV.h"
#include "Utils\Timer.h"

// -----------------------------------------------------------------------------

CBaseIotDevice::CBaseIotDevice(_In_ const CAudioProcessors &cAudioProcessors) noexcept : CTUiWindow(), CPersistantSettings(),
                                                                                         m_cAudioProcessors(cAudioProcessors)
{
    SetDeviceStatus(L"Not initialized", 0);
}

CBaseIotDevice::~CBaseIotDevice() noexcept
{
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

ULONGLONG CBaseIotDevice::GetID() const noexcept
{
    LPCSTR szTypeA = GetType();
    ULONGLONG ullHash;

    ullHash = Hash::Fnv1A64(m_szDeviceNameW, wcslen(m_szDeviceNameW) * sizeof(WCHAR));
    return Hash::Fnv1A64(szTypeA, strlen(szTypeA), ullHash);
}

HRESULT CBaseIotDevice::InitComm() noexcept
{
    return S_OK;
}

VOID CBaseIotDevice::DeinitComm() noexcept
{
}

VOID CBaseIotDevice::TimerTick() noexcept
{
    RenderDeviceStatus();
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

VOID CBaseIotDevice::SetDeviceStatus(_In_z_ LPCWSTR szReasonW, _In_ INT iColor) noexcept
{
    CMutex::CAutoLock lock(m_sDeviceStatus.cMutex);

    wcsncpy_s(m_sDeviceStatus.szTextW, szReasonW, _TRUNCATE);
    m_sDeviceStatus.nTextLen = wcslen(m_sDeviceStatus.szTextW);
    m_sDeviceStatus.iTextColor = iColor;

    m_sDeviceStatus.ullLastRefreshMs = 0;
    m_sDeviceStatus.nTextOffset = SIZE_MAX;
    m_sDeviceStatus.iLastVisibleStatusChars = INT_MAX;
}

VOID CBaseIotDevice::RenderDeviceStatus() noexcept
{
    static constexpr WCHAR STATUS_SCROLL_SEPARATOR[] = L" | ";
    WCHAR szStatusW[128];
    SIZE_T nStatusLen;
    INT iStatusColor;
    ULONGLONG ullNow;
    INT iVisibleStatusChars;

    iVisibleStatusChars = GetClientWidth() - 8;
    if (iVisibleStatusChars <= 0)
    {
        return;
    }
    if (static_cast<SIZE_T>(iVisibleStatusChars) > _countof(szStatusW))
    {
        iVisibleStatusChars = static_cast<INT>(_countof(szStatusW));
    }

    {
        CMutex::CAutoLock lock(m_sDeviceStatus.cMutex);

        ullNow = GetCurrentTimeMs();
        if (m_sDeviceStatus.nTextOffset != SIZE_MAX && ullNow - m_sDeviceStatus.ullLastRefreshMs < 200)
        {
            return;
        }
        m_sDeviceStatus.ullLastRefreshMs = ullNow;

        // NOTE: Remember the client width does not

        if (m_sDeviceStatus.nTextLen <= static_cast<SIZE_T>(iVisibleStatusChars))
        {
            if (m_sDeviceStatus.nTextOffset != SIZE_MAX && m_sDeviceStatus.iLastVisibleStatusChars == iVisibleStatusChars)
            {
                return; // No need to re-render a visible text
            }

            m_sDeviceStatus.nTextOffset = 0; // No offset

            wcsncpy_s(szStatusW, m_sDeviceStatus.szTextW, _TRUNCATE);
            nStatusLen = m_sDeviceStatus.nTextLen;
        }
        else
        {
            SIZE_T nTextOfs, nFullTextLen;

            nFullTextLen = m_sDeviceStatus.nTextLen + (_countof(STATUS_SCROLL_SEPARATOR) - 1);

            m_sDeviceStatus.nTextOffset += 1;
            if (m_sDeviceStatus.nTextOffset >= nFullTextLen)
            {
                m_sDeviceStatus.nTextOffset = 0;
            }

            nTextOfs = m_sDeviceStatus.nTextOffset;
            nStatusLen = static_cast<SIZE_T>(iVisibleStatusChars);
            for (SIZE_T n = 0; n < nStatusLen; n++)
            {
                szStatusW[n] = (nTextOfs < m_sDeviceStatus.nTextLen) ? m_sDeviceStatus.szTextW[nTextOfs]
                                                                        : STATUS_SCROLL_SEPARATOR[nTextOfs - m_sDeviceStatus.nTextLen];

                nTextOfs += 1;
                if (nTextOfs >= nFullTextLen)
                {
                    nTextOfs = 0;
                }
            }
        }

        m_sDeviceStatus.iLastVisibleStatusChars = iVisibleStatusChars;
        iStatusColor = m_sDeviceStatus.iTextColor;
    }

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H\x1b[97mStatus:\x1b[0m \x1b[%dm%-*.*s\x1b[0m", DEVICE_STATUS_ROW,
                                     iStatusColor, iVisibleStatusChars, static_cast<INT>(nStatusLen), szStatusW);
}
