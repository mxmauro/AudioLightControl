#include "Rgb5050Lighting.h"
#include "Utils\Timer.h"
#include <crtdbg.h>

// -----------------------------------------------------------------------------

CRgb5050Lighting::CRgb5050Lighting(_In_ const CAudioProcessors &cAudioProcessors) noexcept : CBaseLighting(cAudioProcessors)
{
}

HRESULT CRgb5050Lighting::SetupEffects(_In_ const std::vector<CConfig::DeviceEffect> &vecEffects) noexcept
{
    CBaseAudioProcessor *lpRgbAudioProcessor = nullptr;
    CBaseAudioProcessor *lpWhiteAudioProcessor = nullptr;
    CBaseAudioProcessor *lpUVAudioProcessor = nullptr;

    if (vecEffects.empty())
    {
        return E_INVALIDARG;
    }

    for (CConfig::DeviceEffect eEffect : vecEffects)
    {
        CBaseAudioProcessor *lpAudioProcessor = m_cAudioProcessors.FindByType(CConfig::GetAudioProcessorType(eEffect));

        if (!lpAudioProcessor)
        {
            return E_NOT_SET;
        }
        if ((!lpRgbAudioProcessor) && lpAudioProcessor->HasRGBColor() != FALSE)
        {
            lpRgbAudioProcessor = lpAudioProcessor;
        }
        if ((!lpWhiteAudioProcessor) && lpAudioProcessor->HasWhiteColor() != FALSE)
        {
            lpWhiteAudioProcessor = lpAudioProcessor;
        }
        if ((!lpUVAudioProcessor) && lpAudioProcessor->HasUVColor() != FALSE)
        {
            lpUVAudioProcessor = lpAudioProcessor;
        }
    }

    if (lpRgbAudioProcessor)
    {
        m_lpAudioProcessor = lpRgbAudioProcessor;
        m_eOutputMode = OutputMode::Rgb;
    }
    else if (lpWhiteAudioProcessor)
    {
        m_lpAudioProcessor = lpWhiteAudioProcessor;
        m_eOutputMode = OutputMode::White;
    }
    else if (lpUVAudioProcessor)
    {
        m_lpAudioProcessor = lpUVAudioProcessor;
        m_eOutputMode = OutputMode::UV;
    }
    else
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    return S_OK;
}

HRESULT CRgb5050Lighting::SetupNodeDevice(_In_ CAudioLightNodeDevice *lpNodeDevice, _In_ BYTE byLightingSlot) noexcept
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

VOID CRgb5050Lighting::FirstRender() noexcept
{
    TimerTick();
}

VOID CRgb5050Lighting::Process() noexcept
{
    ULONGLONG ullNow;
    BYTE uAutoRed, uAutoGreen, uAutoBlue;
    BYTE uCurrentRed, uCurrentGreen, uCurrentBlue;
    ULONG ulCurrentLuminance, ulNewLuminance;
    BOOL bSendCommand = FALSE;

    ullNow = GetCurrentTimeMs();
    if (!GetAutomaticRGB(uAutoRed, uAutoGreen, uAutoBlue))
    {
        return;
    }
    ulNewLuminance = MathHelpers::GetLuminance(uAutoRed, uAutoGreen, uAutoBlue);

    {
        CRwMutex::CAutoExclusiveLock lock(m_cCurrentStateRwMutex);

        if (m_sCurrentState.bFixedIsActive == FALSE)
        {
            GetCurrentRGBNoLock(ullNow, &uCurrentRed, &uCurrentGreen, &uCurrentBlue);
            ulCurrentLuminance = MathHelpers::GetLuminance(uCurrentRed, uCurrentGreen, uCurrentBlue);

            if (ulNewLuminance > ulCurrentLuminance)
            {
                m_sCurrentState.uRed = uAutoRed;
                m_sCurrentState.uGreen = uAutoGreen;
                m_sCurrentState.uBlue = uAutoBlue;
                m_sCurrentState.uFadeMs = 100;
                m_sCurrentState.ullStartMs = ullNow;

                bSendCommand = TRUE;
            }
        }
    }
    if (bSendCommand != FALSE)
    {
        SendSetAndFadeToColorCommand(uAutoRed, uAutoGreen, uAutoBlue, 100);
    }
}

VOID CRgb5050Lighting::TimerTick() noexcept
{
    ULONGLONG ullNow = GetCurrentTimeMs();
    BYTE uCurrentRed, uCurrentGreen, uCurrentBlue;
    INT iColorBarWidth;

    CBaseLighting::TimerTick();

    {
        CRwMutex::CAutoSharedLock lock(m_cCurrentStateRwMutex);

        GetCurrentRGBNoLock(ullNow, &uCurrentRed, &uCurrentGreen, &uCurrentBlue);
    }

    iColorBarWidth = GetClientWidth();
    if (iColorBarWidth > 0)
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;1H\x1b[48;2;%d;%d;%dm%*ls\x1b[0m",
                                         uCurrentRed, uCurrentGreen, uCurrentBlue, iColorBarWidth, L"");
    }
}

HRESULT CRgb5050Lighting::OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept
{
    ULONGLONG ullNow;
    BYTE uCurrentRed, uCurrentGreen, uCurrentBlue;
    BOOL bSendCommand = FALSE;
    ullNow = GetCurrentTimeMs();

    {
        CRwMutex::CAutoExclusiveLock lock(m_cCurrentStateRwMutex);

        switch (sAction.eType)
        {
            case ManualColorActionType_t::SetFixedPaletteColor:
                m_sCurrentState.bFixedIsActive = TRUE;
                m_sCurrentState.uRed = sAction.uRed;
                m_sCurrentState.uGreen = sAction.uGreen;
                m_sCurrentState.uBlue = sAction.uBlue;
                m_sCurrentState.uFixedBrightness = 255;
                m_sCurrentState.uFadeMs = 100;
                m_sCurrentState.ullStartMs = ullNow;
                bSendCommand = TRUE;
                break;

            case ManualColorActionType_t::IncreaseBrightness:
                if (m_sCurrentState.bFixedIsActive == FALSE)
                {
                    m_sCurrentState.uRed = 255;
                    m_sCurrentState.uGreen = 255;
                    m_sCurrentState.uBlue = 255;
                    m_sCurrentState.uFadeMs = 100;
                    m_sCurrentState.uFixedBrightness = 255;

                    m_sCurrentState.bFixedIsActive = TRUE;
                }
                else
                {
                    m_sCurrentState.uFadeMs = 0;
                    m_sCurrentState.uFixedBrightness =
                        (m_sCurrentState.uFixedBrightness < 240) ? (m_sCurrentState.uFixedBrightness + 16) : 255;
                }
                m_sCurrentState.ullStartMs = ullNow;
                bSendCommand = TRUE;
                break;

            case ManualColorActionType_t::DecreaseBrightness:
                if (m_sCurrentState.bFixedIsActive == FALSE)
                {
                    m_sCurrentState.uRed = 255;
                    m_sCurrentState.uGreen = 255;
                    m_sCurrentState.uBlue = 255;
                    m_sCurrentState.uFadeMs = 100;
                    m_sCurrentState.uFixedBrightness = 255;

                    m_sCurrentState.bFixedIsActive = TRUE;
                }
                else
                {
                    m_sCurrentState.uFadeMs = 0;
                    m_sCurrentState.uFixedBrightness =
                        (m_sCurrentState.uFixedBrightness >= 16) ? (m_sCurrentState.uFixedBrightness - 16) : 0;
                }
                m_sCurrentState.ullStartMs = ullNow;
                bSendCommand = TRUE;
                break;

            case ManualColorActionType_t::ReturnToAutomaticMode:
                m_sCurrentState.bFixedIsActive = FALSE;
                break;

            default:
                return E_INVALIDARG;
        }

        if (bSendCommand != FALSE)
        {
            GetCurrentRGBNoLock(ullNow, &uCurrentRed, &uCurrentGreen, &uCurrentBlue);
        }
    }

    if (bSendCommand != FALSE)
    {
        SendSetColorCommand(uCurrentRed, uCurrentGreen, uCurrentBlue);
    }

    return S_OK;
}

VOID CRgb5050Lighting::OnNodeConnectionStateChanged(_In_ BOOL bConnected, _In_ HRESULT hRes) noexcept
{
    if (bConnected == FALSE)
    {
        SetDeviceStatus(FAILED(hRes) ? L"Disconnected" : L"Connecting", 0);
        return;
    }

    SetDeviceStatus(L"Connected", 92);
    BYTE uCurrentRed, uCurrentGreen, uCurrentBlue;
    BOOL bFixedIsActive;
    SetAndFadeToColorCommand_t sCmd;

    {
        CRwMutex::CAutoSharedLock lock(m_cCurrentStateRwMutex);

        GetCurrentRGBNoLock(GetCurrentTimeMs(), &uCurrentRed, &uCurrentGreen, &uCurrentBlue);
        bFixedIsActive = m_sCurrentState.bFixedIsActive;
    }

    if (bFixedIsActive != FALSE)
    {
        SendSetColorCommand(uCurrentRed, uCurrentGreen, uCurrentBlue);
        return;
    }

    sCmd.uRedStart = MathHelpers::be16((USHORT)uCurrentRed | ((USHORT)uCurrentRed << 8));
    sCmd.uGreenStart = MathHelpers::be16((USHORT)uCurrentGreen | ((USHORT)uCurrentGreen << 8));
    sCmd.uBlueStart = MathHelpers::be16((USHORT)uCurrentBlue | ((USHORT)uCurrentBlue << 8));
    sCmd.uRedEnd = sCmd.uGreenEnd = sCmd.uBlueEnd = MathHelpers::be16(0);
    sCmd.uFadeMs = MathHelpers::be16(100);

    if (m_lpNodeDevice)
    {
        m_lpNodeDevice->SendCommand(m_byLightingSlot, CMD_SET_AND_FADE_TO_COLOR, &sCmd, sizeof(sCmd), FALSE);
    }
}

HRESULT CRgb5050Lighting::ValidateNodeLightingConfiguration(_In_ const CIotCommClient::LightingOutput_t &sLighting,
                                                            _Out_writes_z_(nReasonLen) LPWSTR szReasonW,
                                                            _In_ SIZE_T nReasonLen) noexcept
{
    if (sLighting.eType != CIotCommClient::LightingTypeRgb5050)
    {
        wcsncpy_s(szReasonW, nReasonLen, L"Configured as RGB5050 but device output type differs", _TRUNCATE);
        return E_INVALIDARG;
    }
    szReasonW[0] = 0;
    return S_OK;
}

BOOL CRgb5050Lighting::GetAutomaticRGB(_Out_ BYTE &uRed, _Out_ BYTE &uGreen, _Out_ BYTE &uBlue) const noexcept
{
    _ASSERT(m_lpAudioProcessor);

    switch (m_eOutputMode)
    {
        case OutputMode::Rgb:
            {
                CBaseAudioProcessor::RGB sColor = m_lpAudioProcessor->GetRGBColor();

                uRed = static_cast<BYTE>(sColor.uRed >> 8);
                uGreen = static_cast<BYTE>(sColor.uGreen >> 8);
                uBlue = static_cast<BYTE>(sColor.uBlue >> 8);
            }
            return TRUE;

        case OutputMode::White:
            {
                BYTE uWhite = static_cast<BYTE>(m_lpAudioProcessor->GetWhiteColor() >> 8);

                uRed = uWhite;
                uGreen = uWhite;
                uBlue = uWhite;
            }
            return TRUE;

        case OutputMode::UV:
            {
                BYTE uUV = static_cast<BYTE>(m_lpAudioProcessor->GetUVColor() >> 8);

                uRed = static_cast<BYTE>((static_cast<USHORT>(uUV) * 6) / 10);
                uGreen = 0;
                uBlue = uUV;
            }
            return TRUE;
    }

    return FALSE;
}

VOID CRgb5050Lighting::GetCurrentRGBNoLock(_In_ ULONGLONG ullNow, _Out_ LPBYTE lpuRed, _Out_ LPBYTE lpuGreen,
                                            _Out_ LPBYTE lpuBlue) noexcept
{
    if (m_sCurrentState.bFixedIsActive == FALSE)
    {
        if (ullNow >= m_sCurrentState.ullStartMs && ullNow < m_sCurrentState.ullStartMs + (ULONGLONG)m_sCurrentState.uFadeMs)
        {
            float fProgress = 1.0f - float(ullNow - m_sCurrentState.ullStartMs) / float(m_sCurrentState.uFadeMs);

            *lpuRed = BYTE((float)m_sCurrentState.uRed * fProgress);
            *lpuGreen = BYTE((float)m_sCurrentState.uGreen * fProgress);
            *lpuBlue = BYTE((float)m_sCurrentState.uBlue * fProgress);
        }
        else
        {
            *lpuRed = 0;
            *lpuGreen = 0;
            *lpuBlue = 0;
        }
    }
    else
    {
        *lpuRed = (BYTE)((USHORT)m_sCurrentState.uRed * (USHORT)m_sCurrentState.uFixedBrightness / 255);
        *lpuGreen = (BYTE)((USHORT)m_sCurrentState.uGreen * (USHORT)m_sCurrentState.uFixedBrightness / 255);
        *lpuBlue = (BYTE)((USHORT)m_sCurrentState.uBlue * (USHORT)m_sCurrentState.uFixedBrightness / 255);
    }
}

VOID CRgb5050Lighting::SendSetColorCommand(_In_ BYTE uRed, _In_ BYTE uGreen, _In_ BYTE uBlue) noexcept
{
    SetColorCommand_t sCmd;

    sCmd.uRed = MathHelpers::be16((USHORT)uRed | ((USHORT)uRed << 8));
    sCmd.uGreen = MathHelpers::be16((USHORT)uGreen | ((USHORT)uGreen << 8));
    sCmd.uBlue = MathHelpers::be16((USHORT)uBlue | ((USHORT)uBlue << 8));

    if (m_lpNodeDevice)
    {
        m_lpNodeDevice->SendCommand(m_byLightingSlot, CMD_SET_COLOR, &sCmd, sizeof(sCmd), FALSE);
    }
}

VOID CRgb5050Lighting::SendSetAndFadeToColorCommand(_In_ BYTE uRed, _In_ BYTE uGreen, _In_ BYTE uBlue, _In_ USHORT uFadeMs) noexcept
{
    SetAndFadeToColorCommand_t sCmd;

    sCmd.uRedStart = MathHelpers::be16((USHORT)uRed | ((USHORT)uRed << 8));
    sCmd.uGreenStart = MathHelpers::be16((USHORT)uGreen | ((USHORT)uGreen << 8));
    sCmd.uBlueStart = MathHelpers::be16((USHORT)uBlue | ((USHORT)uBlue << 8));
    sCmd.uRedEnd = sCmd.uGreenEnd = sCmd.uBlueEnd = MathHelpers::be16(0);
    sCmd.uFadeMs = MathHelpers::be16(uFadeMs);

    if (m_lpNodeDevice)
    {
        m_lpNodeDevice->SendCommand(m_byLightingSlot, CMD_SET_AND_FADE_TO_COLOR, &sCmd, sizeof(sCmd), FALSE);
    }
}
