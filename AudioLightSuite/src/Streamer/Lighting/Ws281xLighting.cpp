#include "Ws281xLighting.h"

// -----------------------------------------------------------------------------

CWs281xLighting::CWs281xLighting(_In_ const CAudioProcessors &cAudioProcessors) noexcept : CBaseLighting(cAudioProcessors)
{
}

HRESULT CWs281xLighting::SetupEffects(_In_ const std::vector<CConfig::DeviceEffect> &vecEffects) noexcept
{
    CBaseAudioProcessor *lpStripProcessor = nullptr;
    CBaseAudioProcessor *lpRgbProcessor = nullptr;
    CBaseAudioProcessor *lpWhiteProcessor = nullptr;
    CBaseAudioProcessor *lpUvProcessor = nullptr;

    if (vecEffects.empty())
    {
        return E_INVALIDARG;
    }
    for (CConfig::DeviceEffect eEffect : vecEffects)
    {
        CBaseAudioProcessor *lpProcessor = m_cAudioProcessors.FindByType(CConfig::GetAudioProcessorType(eEffect));

        if (!lpProcessor)
        {
            return E_NOT_SET;
        }
        if (!lpStripProcessor && lpProcessor->HasRGBStripColors() != FALSE)
        {
            lpStripProcessor = lpProcessor;
        }
        if (!lpRgbProcessor && lpProcessor->HasRGBColor() != FALSE)
        {
            lpRgbProcessor = lpProcessor;
        }
        if (!lpWhiteProcessor && lpProcessor->HasWhiteColor() != FALSE)
        {
            lpWhiteProcessor = lpProcessor;
        }
        if (!lpUvProcessor && lpProcessor->HasUVColor() != FALSE)
        {
            lpUvProcessor = lpProcessor;
        }
    }
    if (lpStripProcessor)
    {
        m_lpAudioProcessor = lpStripProcessor;
        m_eOutputMode = OutputMode::StripRgb;
    }
    else if (lpRgbProcessor)
    {
        m_lpAudioProcessor = lpRgbProcessor;
        m_eOutputMode = OutputMode::Rgb;
    }
    else if (lpWhiteProcessor)
    {
        m_lpAudioProcessor = lpWhiteProcessor;
        m_eOutputMode = OutputMode::White;
    }
    else if (lpUvProcessor)
    {
        m_lpAudioProcessor = lpUvProcessor;
        m_eOutputMode = OutputMode::UV;
    }
    else
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    return S_OK;
}

HRESULT CWs281xLighting::SetupStripLength(_In_ USHORT nStripLength) noexcept
{
    try
    {
        if (nStripLength == 0 || nStripLength > 300)
        {
            return E_INVALIDARG;
        }
        m_vecStripColors.resize(nStripLength);
        m_vecPreviewColors.resize(nStripLength);
        m_vecRgbBytes.resize(static_cast<SIZE_T>(nStripLength) * 3);
        m_nStripLength = nStripLength;
    }
    catch (const std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

HRESULT CWs281xLighting::SetupNodeDevice(_In_ CAudioLightNodeDevice *lpNodeDevice, _In_ BYTE byLightingSlot) noexcept
{
    HRESULT hRes;

    if ((!lpNodeDevice) || m_lpNodeDevice || byLightingSlot > 1)
    {
        return E_INVALIDARG;
    }
    hRes = lpNodeDevice->Subscribe(byLightingSlot, this, m_cNodeSubscription);
    if (SUCCEEDED(hRes))
    {
        m_lpNodeDevice = lpNodeDevice;
        m_byLightingSlot = byLightingSlot;
        SetDeviceStatus(L"Disconnected", 0);
    }
    return hRes;
}

VOID CWs281xLighting::FirstRender() noexcept
{
    TimerTick();
}

VOID CWs281xLighting::TimerTick() noexcept
{
    CBaseLighting::TimerTick();
    RenderStripPreview();
}

VOID CWs281xLighting::Process() noexcept
{
    CBaseAudioProcessor::RGB sColor;

    if ((!m_lpNodeDevice) || m_lpNodeDevice->IsConnected() == FALSE)
    {
        return;
    }
    if (GetColor(sColor) != FALSE)
    {
        SendUniformColor(sColor);
        return;
    }
    if (m_eOutputMode != OutputMode::StripRgb || m_vecStripColors.empty())
    {
        return;
    }

    m_lpAudioProcessor->GetRGBStripColors(m_vecStripColors.data(), m_vecStripColors.size());
    for (SIZE_T i = 0; i < m_vecStripColors.size(); i++)
    {
        m_vecRgbBytes[i * 3] = static_cast<BYTE>(m_vecStripColors[i].uRed >> 8);
        m_vecRgbBytes[i * 3 + 1] = static_cast<BYTE>(m_vecStripColors[i].uGreen >> 8);
        m_vecRgbBytes[i * 3 + 2] = static_cast<BYTE>(m_vecStripColors[i].uBlue >> 8);
    }
    m_lpNodeDevice->SendRawWs281xRgbData(m_byLightingSlot, 1, m_nStripLength, m_vecRgbBytes.data(), m_vecRgbBytes.size());
}

HRESULT CWs281xLighting::OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept
{
    CMutex::CAutoLock lock(m_cManualColorMutex);

    switch (sAction.eType)
    {
        case ManualColorActionType_t::SetFixedPaletteColor:
            m_sManualColor.bFixedIsActive = TRUE;
            m_sManualColor.uRed = sAction.uRed;
            m_sManualColor.uGreen = sAction.uGreen;
            m_sManualColor.uBlue = sAction.uBlue;
            m_sManualColor.uBrightness = 255;
            break;

        case ManualColorActionType_t::IncreaseBrightness:
        case ManualColorActionType_t::DecreaseBrightness:
            if (m_sManualColor.bFixedIsActive == FALSE)
            {
                m_sManualColor.bFixedIsActive = TRUE;
                m_sManualColor.uRed = m_sManualColor.uGreen = m_sManualColor.uBlue = 255;
                m_sManualColor.uBrightness = 255;
            }
            else if (sAction.eType == ManualColorActionType_t::IncreaseBrightness)
            {
                m_sManualColor.uBrightness = (m_sManualColor.uBrightness < 240) ? m_sManualColor.uBrightness + 16 : 255;
            }
            else
            {
                m_sManualColor.uBrightness = (m_sManualColor.uBrightness >= 16) ? m_sManualColor.uBrightness - 16 : 0;
            }
            break;

        case ManualColorActionType_t::ReturnToAutomaticMode:
            m_sManualColor.bFixedIsActive = FALSE;
            break;

        default:
            return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT CWs281xLighting::ValidateNodeLightingConfiguration(_In_ const CIotCommClient::LightingOutput_t &sLighting,
                                                           _Out_writes_z_(nReasonLen) LPWSTR szReasonW,
                                                           _In_ SIZE_T nReasonLen) noexcept
{
    if (sLighting.eType != CIotCommClient::LightingTypeWs281x || sLighting.nLedCount != m_nStripLength)
    {
        wcsncpy_s(szReasonW, nReasonLen, L"Configured as WS281x but Node output differs", _TRUNCATE);
        return E_INVALIDARG;
    }
    szReasonW[0] = 0;
    return S_OK;
}

VOID CWs281xLighting::OnNodeConnectionStateChanged(_In_ BOOL bConnected, _In_ HRESULT hRes) noexcept
{
    SetDeviceStatus((bConnected != FALSE) ? L"Connected" : (FAILED(hRes) ? L"Disconnected" : L"Connecting"),
                    (bConnected != FALSE) ? 92 : 0);
}

BOOL CWs281xLighting::GetColor(_Out_ CBaseAudioProcessor::RGB &sColor) noexcept
{
    {
        CMutex::CAutoLock lock(m_cManualColorMutex);

        if (m_sManualColor.bFixedIsActive != FALSE)
        {
            sColor.uRed = static_cast<USHORT>((static_cast<UINT32>(m_sManualColor.uRed) * m_sManualColor.uBrightness / 255) << 8);
            sColor.uGreen = static_cast<USHORT>((static_cast<UINT32>(m_sManualColor.uGreen) * m_sManualColor.uBrightness / 255) << 8);
            sColor.uBlue = static_cast<USHORT>((static_cast<UINT32>(m_sManualColor.uBlue) * m_sManualColor.uBrightness / 255) << 8);
            return TRUE;
        }
    }

    switch (m_eOutputMode)
    {
        case OutputMode::Rgb:
            sColor = m_lpAudioProcessor->GetRGBColor();
            return TRUE;

        case OutputMode::White:
            sColor.uRed = sColor.uGreen = sColor.uBlue = m_lpAudioProcessor->GetWhiteColor();
            return TRUE;

        case OutputMode::UV:
            sColor.uRed = static_cast<USHORT>((static_cast<UINT32>(m_lpAudioProcessor->GetUVColor()) * 6) / 10);
            sColor.uGreen = 0;
            sColor.uBlue = m_lpAudioProcessor->GetUVColor();
            return TRUE;

        default:
            return FALSE;
    }
}

VOID CWs281xLighting::SendUniformColor(_In_ const CBaseAudioProcessor::RGB &sColor) noexcept
{
    SetColorCommand_t sCmd;

    sCmd.uRed = MathHelpers::be16(sColor.uRed);
    sCmd.uGreen = MathHelpers::be16(sColor.uGreen);
    sCmd.uBlue = MathHelpers::be16(sColor.uBlue);
    m_lpNodeDevice->SendCommand(m_byLightingSlot, CMD_SET_COLOR, &sCmd, sizeof(sCmd), FALSE);
}

VOID CWs281xLighting::RenderStripPreview() noexcept
{
    CBaseAudioProcessor::RGB sColor;
    INT iWidth;

    if ((!m_lpTerminalControl) || (!GetClientRegionHandle()) || (!m_lpAudioProcessor))
    {
        return;
    }

    iWidth = GetClientWidth();
    if (iWidth < 1)
    {
        return;
    }

    if (GetColor(sColor) != FALSE)
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;1H\x1b[48;2;%u;%u;%um%*ls\x1b[0m",
                                         sColor.uRed >> 8, sColor.uGreen >> 8, sColor.uBlue >> 8, iWidth, L"");
        return;
    }

    if (m_eOutputMode != OutputMode::StripRgb || m_vecPreviewColors.empty())
    {
        return;
    }

    m_lpAudioProcessor->GetRGBStripColors(m_vecPreviewColors.data(), m_vecPreviewColors.size());
    for (INT iColumn = 0; iColumn < iWidth; iColumn++)
    {
        const SIZE_T nColorIndex = static_cast<SIZE_T>(iColumn) * m_vecPreviewColors.size() / static_cast<SIZE_T>(iWidth);
        const CBaseAudioProcessor::RGB &sPreviewColor = m_vecPreviewColors[nColorIndex];

        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;%dH\x1b[48;2;%u;%u;%um \x1b[0m", iColumn + 1,
                                         sPreviewColor.uRed >> 8, sPreviewColor.uGreen >> 8, sPreviewColor.uBlue >> 8);
    }
}
