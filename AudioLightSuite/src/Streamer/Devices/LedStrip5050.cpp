#include "LedStrip5050.h"
#include "Utils\MessageBoxUtils.h"
#include "Utils\Timer.h"

#define CMD_SET_AND_FADE_TO_COLOR 3

// -----------------------------------------------------------------------------

#pragma pack(1)
typedef struct SetAndFadeToColorCommand_s {
    USHORT uRedStart;
    USHORT uGreenStart;
    USHORT uBlueStart;
    USHORT uRedEnd;
    USHORT uGreenEnd;
    USHORT uBlueEnd;
    USHORT uFadeMs;
} SetAndFadeToColorCommand;
#pragma pack()

// -----------------------------------------------------------------------------

HRESULT CLedStrip5050IotDevice::SetupAudioProcessors(_In_ CAudioProcessorList &vecAudioProcessors) noexcept
{
    WCHAR szErrMsgBufW[512];

    for (const auto &audioProc : vecAudioProcessors)
    {
        if (strcmp(audioProc->GetType(), "SpectrumColor") == 0)
        {
            m_lpSpectrumColorAudioProc = dynamic_cast<CSpectrumColorAudioProcessor *>(audioProc.get());
        }
    }

    if (!m_lpSpectrumColorAudioProc)
    {
        swprintf_s(szErrMsgBufW, 512, L"Unable to find 'SpectrumColor' audio processor for device \"%s\".", m_szDeviceNameW);
        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    // Done
    return S_OK;
}

VOID CLedStrip5050IotDevice::FirstRender() noexcept
{
    TimerTick();
    OnIotConnectionStateChanged(IsIotConnected());
}

VOID CLedStrip5050IotDevice::Process() noexcept
{
    CSpectrumColorAudioProcessor::RGB sCurrentRGB;
    ULONGLONG ullNow;
    BYTE uCurrentRed, uCurrentGreen, uCurrentBlue;
    ULONG ulCurrentLuminance, ulNewLuminance;
    BOOL bSendCommand = FALSE;

    ullNow = GetCurrentTimeMs();
    sCurrentRGB = m_lpSpectrumColorAudioProc->GetCurrentColor();
    ulNewLuminance = MathHelpers::GetLuminance(sCurrentRGB.r, sCurrentRGB.g, sCurrentRGB.b);

    {
        CMutex::CAutoLock lock(m_cStateMutex);

        if (m_bFixedIsActive == FALSE)
        {
            GetCurrentRGBNoLock(ullNow, &uCurrentRed, &uCurrentGreen, &uCurrentBlue);

            ulCurrentLuminance = MathHelpers::GetLuminance(uCurrentRed, uCurrentGreen, uCurrentBlue);

            if (ulNewLuminance > ulCurrentLuminance)
            {
                m_sCurrent.uRed = sCurrentRGB.r;
                m_sCurrent.uGreen = sCurrentRGB.g;
                m_sCurrent.uBlue = sCurrentRGB.b;
                m_sCurrent.uFadeMs = 100;
                m_sCurrent.ullStartMs = ullNow;

                bSendCommand = TRUE;
            }
        }
    }
    if (bSendCommand != FALSE)
    {
        SendSetAndFadeToColorCommand(sCurrentRGB.r, sCurrentRGB.g, sCurrentRGB.b, 100);
    }
}

VOID CLedStrip5050IotDevice::TimerTick() noexcept
{
    ULONGLONG ullNow = GetCurrentTimeMs();
    BYTE uCurrentRed, uCurrentGreen, uCurrentBlue;
    INT iColorBarWidth;

    {
        CMutex::CAutoLock lock(m_cStateMutex);

        GetCurrentRGBNoLock(ullNow, &uCurrentRed, &uCurrentGreen, &uCurrentBlue);
    }

    iColorBarWidth = GetClientWidth();
    if (iColorBarWidth > 0)
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;1H\x1b[48;2;%d;%d;%dm%*ls\x1b[0m",
                                         uCurrentRed, uCurrentGreen, uCurrentBlue, iColorBarWidth, L"");
    }
}

HRESULT CLedStrip5050IotDevice::OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept
{
    ULONGLONG ullNow;
    BYTE uCurrentRed, uCurrentGreen, uCurrentBlue;
    BOOL bSendCommand = FALSE;
    ullNow = GetCurrentTimeMs();

    {
        CMutex::CAutoLock lock(m_cStateMutex);

        switch (sAction.eType)
        {
            case ManualColorActionType_t::SetFixedPaletteColor:
                m_bFixedIsActive = TRUE;
                m_sCurrent.uRed = sAction.uRed;
                m_sCurrent.uGreen = sAction.uGreen;
                m_sCurrent.uBlue = sAction.uBlue;
                m_sCurrent.uFixedBrightness = 255;
                m_sCurrent.uFadeMs = 100;
                m_sCurrent.ullStartMs = ullNow;
                bSendCommand = TRUE;
                break;

            case ManualColorActionType_t::IncreaseBrightness:
                if (m_bFixedIsActive == FALSE)
                {
                    m_sCurrent.uRed = 255;
                    m_sCurrent.uGreen = 255;
                    m_sCurrent.uBlue = 255;
                    m_sCurrent.uFadeMs = 100;
                    m_sCurrent.uFixedBrightness = 255;

                    m_bFixedIsActive = TRUE;
                }
                else
                {
                    m_sCurrent.uFadeMs = 0;
                    m_sCurrent.uFixedBrightness = (m_sCurrent.uFixedBrightness < 240) ? (m_sCurrent.uFixedBrightness + 16) : 255;
                }
                m_sCurrent.ullStartMs = ullNow;
                bSendCommand = TRUE;
                break;

            case ManualColorActionType_t::DecreaseBrightness:
                if (m_bFixedIsActive == FALSE)
                {
                    m_sCurrent.uRed = 255;
                    m_sCurrent.uGreen = 255;
                    m_sCurrent.uBlue = 255;
                    m_sCurrent.uFadeMs = 100;
                    m_sCurrent.uFixedBrightness = 255;

                    m_bFixedIsActive = TRUE;
                }
                else
                {
                    m_sCurrent.uFadeMs = 0;
                    m_sCurrent.uFixedBrightness = (m_sCurrent.uFixedBrightness >= 16) ? (m_sCurrent.uFixedBrightness - 16) : 0;
                }
                m_sCurrent.ullStartMs = ullNow;
                bSendCommand = TRUE;
                break;

            case ManualColorActionType_t::ReturnToAutomaticMode:
                m_bFixedIsActive = FALSE;
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
        SendSetAndFadeToColorCommand(uCurrentRed, uCurrentGreen, uCurrentBlue, 100);
    }

    return S_OK;
}

VOID CLedStrip5050IotDevice::OnIotConnectionStateChanged(_In_ BOOL bConnected) noexcept
{
    int iColor = bConnected ? 92 : 91;
    LPCWSTR szStatusW = bConnected ? L"Connected   ": L"Disconnected";

    if (bConnected != FALSE && m_bFixedIsActive != FALSE)
    {
        BYTE uCurrentRed, uCurrentGreen, uCurrentBlue;
        SetAndFadeToColorCommand cmd;

        {
            CMutex::CAutoLock lock(m_cStateMutex);

            GetCurrentRGBNoLock(GetCurrentTimeMs(), &uCurrentRed, &uCurrentGreen, &uCurrentBlue);
        }

        cmd.uRedStart = MathHelpers::be16((USHORT)uCurrentRed | ((USHORT)uCurrentRed << 8));
        cmd.uGreenStart = MathHelpers::be16((USHORT)uCurrentGreen | ((USHORT)uCurrentGreen << 8));
        cmd.uBlueStart = MathHelpers::be16((USHORT)uCurrentBlue | ((USHORT)uCurrentBlue << 8));
        cmd.uRedEnd = cmd.uGreenEnd = cmd.uBlueEnd = MathHelpers::be16(0);
        cmd.uFadeMs = MathHelpers::be16(100);

        SendIotMessage(CMD_SET_AND_FADE_TO_COLOR, &cmd, sizeof(cmd), FALSE);
    }

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H\x1b[97mIot Link state:\x1b[0m \x1b[%dm%s\x1b[0m",
                                     3, iColor, szStatusW);
}

VOID CLedStrip5050IotDevice::GetCurrentRGBNoLock(_In_ ULONGLONG ullNow, _Out_ LPBYTE lpuRed, _Out_ LPBYTE lpuGreen,
                                                 _Out_ LPBYTE lpuBlue) noexcept
{
    if (m_bFixedIsActive == FALSE)
    {
        if (ullNow >= m_sCurrent.ullStartMs && ullNow < m_sCurrent.ullStartMs + (ULONGLONG)m_sCurrent.uFadeMs)
        {
            float fProgress = 1.0f - float(ullNow - m_sCurrent.ullStartMs) / float(m_sCurrent.uFadeMs);

            *lpuRed = BYTE((float)m_sCurrent.uRed * fProgress);
            *lpuGreen = BYTE((float)m_sCurrent.uGreen * fProgress);
            *lpuBlue = BYTE((float)m_sCurrent.uBlue * fProgress);
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
        *lpuRed = (BYTE)((USHORT)m_sCurrent.uRed * (USHORT)m_sCurrent.uFixedBrightness / 255);
        *lpuGreen = (BYTE)((USHORT)m_sCurrent.uGreen * (USHORT)m_sCurrent.uFixedBrightness / 255);
        *lpuBlue = (BYTE)((USHORT)m_sCurrent.uBlue * (USHORT)m_sCurrent.uFixedBrightness / 255);
    }
}

VOID CLedStrip5050IotDevice::SendSetAndFadeToColorCommand(_In_ BYTE uRed, _In_ BYTE uGreen, _In_ BYTE uBlue, _In_ USHORT uFadeMs) noexcept
{
    SetAndFadeToColorCommand cmd;

    cmd.uRedStart = MathHelpers::be16((USHORT)uRed | ((USHORT)uRed << 8));
    cmd.uGreenStart = MathHelpers::be16((USHORT)uGreen | ((USHORT)uGreen << 8));
    cmd.uBlueStart = MathHelpers::be16((USHORT)uBlue | ((USHORT)uBlue << 8));
    cmd.uRedEnd = cmd.uGreenEnd = cmd.uBlueEnd = MathHelpers::be16(0);
    cmd.uFadeMs = MathHelpers::be16(uFadeMs);

    SendIotMessage(CMD_SET_AND_FADE_TO_COLOR, &cmd, sizeof(cmd), FALSE);
}
