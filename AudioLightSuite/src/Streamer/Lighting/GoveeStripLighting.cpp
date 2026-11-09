#include "GoveeStripLighting.h"
#include "Crypto\Base64.h"
#include "Utils\Exception2HR.h"
#include "Utils\Timer.h"
#include <algorithm>

// -----------------------------------------------------------------------------

static constexpr BYTE GOVEE_MAGIC_BYTE = 0xBB;
static constexpr BYTE GOVEE_CMD_ENABLE = 0xB1;
static constexpr BYTE GOVEE_CMD_LED_DATA = 0xB0;
static constexpr ULONGLONG GOVEE_TURN_ON_REFRESH_MS = 60000;
static constexpr ULONGLONG GOVEE_STREAMING_ENABLE_REFRESH_MS = 30000;
static constexpr ULONGLONG GOVEE_COLOR_UPDATE_INTERVAL_MS = 100;

// -----------------------------------------------------------------------------

CGoveeStripLighting::CGoveeStripLighting(_In_ const CAudioProcessors &cAudioProcessors) noexcept : CBaseLighting(cAudioProcessors)
{
}

HRESULT CGoveeStripLighting::SetupEffects(_In_ const std::vector<CConfig::DeviceEffect> &vecEffects) noexcept
{
    CBaseAudioProcessor *lpRgbStripAudioProcessor = nullptr;
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
        if (m_bSupportsRazer != FALSE && (!lpRgbStripAudioProcessor) && lpAudioProcessor->HasRGBStripColors() != FALSE)
        {
            lpRgbStripAudioProcessor = lpAudioProcessor;
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

    if (lpRgbStripAudioProcessor)
    {
        m_lpAudioProcessor = lpRgbStripAudioProcessor;
        m_eOutputMode = OutputMode::StripRgb;
    }
    else if (lpRgbAudioProcessor)
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

HRESULT CGoveeStripLighting::SetupGoveeStripDevice(_In_ CGoveeStripDevice *lpGoveeStripDevice) noexcept
{
    ULONGLONG ullNow;

    if (!lpGoveeStripDevice)
    {
        return E_INVALIDARG;
    }

    m_lpGoveeStripDevice = lpGoveeStripDevice;
    ullNow = GetCurrentTimeMs();
    m_ullLastTurnOnMs = ullNow - GOVEE_TURN_ON_REFRESH_MS;
    m_ullLastStreamEnableMs = ullNow - GOVEE_STREAMING_ENABLE_REFRESH_MS;
    m_ullLastColorUpdateMs = ullNow - GOVEE_COLOR_UPDATE_INTERVAL_MS;
    return S_OK;
}

HRESULT CGoveeStripLighting::SetupSupportsRazer(_In_ BOOL bSupportsRazer) noexcept
{
    m_bSupportsRazer = (bSupportsRazer != FALSE) ? TRUE : FALSE;
    return S_OK;
}

HRESULT CGoveeStripLighting::SetupStripLength(_In_ USHORT nStripLength) noexcept
{
    HRESULT hRes;
    std::vector<CBaseAudioProcessor::RGB> vecStripColors;

    if (m_bSupportsRazer == FALSE || nStripLength == 0 || nStripLength > GOVEE_MAX_STRIP_LENGTH)
    {
        return E_INVALIDARG;
    }

    hRes = TranslateToHresult([&]()
    {
        vecStripColors.resize(nStripLength);
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    m_nStripLength = nStripLength;
    m_vecStripColors = std::move(vecStripColors);
    return S_OK;
}

VOID CGoveeStripLighting::FirstRender() noexcept
{
    SetDeviceStatus(L"Ready", 93);
    TimerTick();
}

VOID CGoveeStripLighting::Process() noexcept
{
    CBaseAudioProcessor::RGB sColor;
    USHORT uWhite, uUV;

    if (GetManualColor(sColor) != FALSE)
    {
        SendUniformColor(sColor);
        return;
    }

    switch (m_eOutputMode)
    {
        case OutputMode::StripRgb:
            m_lpAudioProcessor->GetRGBStripColors(m_vecStripColors.data(), m_vecStripColors.size());
            SendRazerColors();
            return;

        case OutputMode::Rgb:
            sColor = m_lpAudioProcessor->GetRGBColor();
            break;

        case OutputMode::White:
            uWhite = m_lpAudioProcessor->GetWhiteColor();
            sColor.uRed = sColor.uGreen = sColor.uBlue = uWhite;
            break;

        case OutputMode::UV:
            uUV = m_lpAudioProcessor->GetUVColor();
            sColor.uRed = static_cast<USHORT>((static_cast<UINT32>(uUV) * 6) / 10);
            sColor.uGreen = 0;
            sColor.uBlue = uUV;
            break;

        default:
            return;
    }

    SendUniformColor(sColor);
}

HRESULT CGoveeStripLighting::OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept
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
            if (m_sManualColor.bFixedIsActive == FALSE)
            {
                m_sManualColor.bFixedIsActive = TRUE;
                m_sManualColor.uRed = 255;
                m_sManualColor.uGreen = 255;
                m_sManualColor.uBlue = 255;
                m_sManualColor.uBrightness = 255;
            }
            else
            {
                m_sManualColor.uBrightness = (m_sManualColor.uBrightness < 240) ? (m_sManualColor.uBrightness + 16) : 255;
            }
            break;

        case ManualColorActionType_t::DecreaseBrightness:
            if (m_sManualColor.bFixedIsActive == FALSE)
            {
                m_sManualColor.bFixedIsActive = TRUE;
                m_sManualColor.uRed = 255;
                m_sManualColor.uGreen = 255;
                m_sManualColor.uBlue = 255;
                m_sManualColor.uBrightness = 255;
            }
            else
            {
                m_sManualColor.uBrightness =
                    (m_sManualColor.uBrightness >= 16) ? (m_sManualColor.uBrightness - 16) : 0;
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

BYTE CGoveeStripLighting::CalculateChecksum(_In_reads_bytes_(nDataLen) const BYTE *lpData, _In_ SIZE_T nDataLen) noexcept
{
    BYTE byChecksum = 0;

    for (SIZE_T n = 0; n < nDataLen; n++)
    {
        byChecksum ^= lpData[n];
    }
    return byChecksum;
}

HRESULT CGoveeStripLighting::CreateRazerJson(_In_ BYTE byCommand, _In_reads_bytes_(nDataLen) const BYTE *lpData, _In_ SIZE_T nDataLen,
                                              _Out_ std::string &strJsonPacket) noexcept
{
    std::vector<BYTE> vecPayload;
    std::string strPayload;
    HRESULT hRes;

    strJsonPacket.clear();

    if (nDataLen > MAXUINT16 || (nDataLen != 0 && (!lpData)))
    {
        return E_INVALIDARG;
    }

    // Create payload
    hRes = TranslateToHresult([&]()
    {
        vecPayload.resize(4 + nDataLen + 1);
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    vecPayload[0] = GOVEE_MAGIC_BYTE;
    vecPayload[1] = static_cast<BYTE>(nDataLen >> 8);
    vecPayload[2] = static_cast<BYTE>(nDataLen);
    vecPayload[3] = byCommand;
    if (nDataLen != 0)
    {
        memcpy(vecPayload.data() + 4, lpData, nDataLen);
    }
    vecPayload[4 + nDataLen] = CalculateChecksum(vecPayload.data(), 4 + nDataLen);

    // Convert payload to base 64
    hRes = Base64::Encode(vecPayload.data(), vecPayload.size(), FALSE, strPayload);
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Wrap into the json packet
    hRes = TranslateToHresult([&]()
    {
        strJsonPacket = "{\"msg\":{\"cmd\":\"razer\",\"data\":{\"pt\":\"" + strPayload + "\"}}}";
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Done
    return S_OK;
}

HRESULT CGoveeStripLighting::CreatePtRealJson(_In_reads_bytes_(nDataLen) const BYTE *lpData, _In_ SIZE_T nDataLen,
                                               _Out_ std::string &strJsonPacket) noexcept
{
    std::vector<BYTE> vecPayload;
    std::string strPayload;
    HRESULT hRes;

    strJsonPacket.clear();

    if (nDataLen > MAXUINT16 || (nDataLen != 0 && (!lpData)))
    {
        return E_INVALIDARG;
    }

    // Create payload
    hRes = TranslateToHresult([&]()
    {
        vecPayload.resize(nDataLen + 1);
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    if (nDataLen != 0)
    {
        memcpy(vecPayload.data(), lpData, nDataLen);
    }
    vecPayload[nDataLen] = CalculateChecksum(vecPayload.data(), nDataLen);

    // Convert payload to base 64
    hRes = Base64::Encode(vecPayload.data(), vecPayload.size(), FALSE, strPayload);
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Wrap into the json packet
    hRes = TranslateToHresult([&]()
    {
        strJsonPacket = "{\"msg\":{\"cmd\":\"ptReal\",\"data\":{\"command\":[\"" + strPayload + "\"]}}}";
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Done
    return S_OK;
}

/*
HRESULT CGoveeStripLighting::CreatePtRealJson(_In_reads_bytes_(nDataLen) const BYTE *lpData, _In_ SIZE_T nDataLen,
                                               _Out_ std::string &strJsonPacket) noexcept
{
    static LPCSTR szHexaA = "0123456789ABCDEF";
    BYTE byChecksum;
    HRESULT hRes;

    strJsonPacket.clear();

    if (nDataLen > MAXUINT16 || (nDataLen != 0 && !lpData))
    {
        return E_INVALIDARG;
    }

    // Calculate data checksum
    byChecksum = CalculateChecksum(lpData, nDataLen);

    // Wrap into the json packet
    hRes = TranslateToHresult([&]()
    {
        const BYTE *lpSrc = static_cast<const BYTE *>(lpData);

        strJsonPacket.reserve(sizeof("{\"msg\":{\"cmd\":\"ptReal\",\"data\":{\"command\":[\"") + 2 * (nDataLen + 1) + sizeof("\"]}}}"));

        strJsonPacket = "{\"msg\":{\"cmd\":\"ptReal\",\"data\":{\"command\":[\"";
        while (nDataLen > 0)
        {
            strJsonPacket += szHexaA[(*lpSrc) >> 4];
            strJsonPacket += szHexaA[(*lpSrc) & 0x0F];

            lpSrc++;
            nDataLen--;
        }

        strJsonPacket += szHexaA[byChecksum >> 4];
        strJsonPacket += szHexaA[byChecksum & 0x0F];

        strJsonPacket += "\"]}}}";
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Done
    return S_OK;
}
*/

BOOL CGoveeStripLighting::SendTurnOnOff(_In_ BOOL bOn) noexcept
{
    CHAR szJsonA[sizeof(
        "{\"msg\":{\"cmd\":\"turn\",\"data\":{\"value\":0}}}"
    )];
    int iJsonLength;

    iJsonLength = sprintf_s(szJsonA, _countof(szJsonA),
                            "{\"msg\":{\"cmd\":\"turn\",\"data\":{\"value\":%u}}}",
                            (bOn) ? 1 : 0);
    if (iJsonLength <= 0 || (!SendPacket(szJsonA, static_cast<SIZE_T>(iJsonLength))))
    {
        SetDeviceStatus(L"Unable to send turn on/off command", 91);
        return FALSE;
    }

    m_ullLastTurnOnMs = GetCurrentTimeMs();
    return TRUE;
}

BOOL CGoveeStripLighting::SendStreamingEnable(_In_ BOOL bEnable) noexcept
{
    BYTE byEnable = (bEnable != FALSE) ? 1 : 0;
    std::string strJsonPacket;
    HRESULT hRes;

    hRes = CreateRazerJson(GOVEE_CMD_ENABLE, &byEnable, sizeof(byEnable), strJsonPacket);
    if (FAILED(hRes) || SendPacket(strJsonPacket.c_str(), strJsonPacket.length()) == FALSE)
    {
        SetDeviceStatus(L"Unable to send enable command", 91);
        return FALSE;
    }

    m_ullLastStreamEnableMs = GetCurrentTimeMs();
    SetDeviceStatus((bEnable != FALSE) ? L"Streaming" : L"Disabled", (bEnable != FALSE) ? 92 : 0);
    return TRUE;
}

VOID CGoveeStripLighting::SendRazerColors() noexcept
{
    BYTE aData[2 + GOVEE_MAX_STRIP_LENGTH * 3];
    SIZE_T nColorCount;
    SIZE_T nDataLen;
    std::string strJsonPacket;
    ULONGLONG ullNow;
    HRESULT hRes;

    if ((!m_lpGoveeStripDevice) || m_lpGoveeStripDevice->IsReady() == FALSE || m_vecStripColors.empty())
    {
        return;
    }

    ullNow = GetCurrentTimeMs();
    if (ullNow - m_ullLastTurnOnMs >= GOVEE_TURN_ON_REFRESH_MS)
    {
        if (!SendTurnOnOff(TRUE))
        {
            return;
        }
    }
    if (ullNow - m_ullLastStreamEnableMs >= GOVEE_STREAMING_ENABLE_REFRESH_MS)
    {
        if (!SendStreamingEnable(TRUE))
        {
            return;
        }
    }
    if (ullNow - m_ullLastColorUpdateMs < GOVEE_COLOR_UPDATE_INTERVAL_MS)
    {
        return;
    }

    nColorCount = m_vecStripColors.size();
    aData[0] = 0; // Do not use Govee's gradient mode.
    aData[1] = static_cast<BYTE>(nColorCount);
    for (SIZE_T n = 0; n < nColorCount; n++)
    {
        const CBaseAudioProcessor::RGB &sColor = m_vecStripColors[n];

        aData[2 + n * 3] = static_cast<BYTE>(sColor.uRed >> 8);
        aData[3 + n * 3] = static_cast<BYTE>(sColor.uGreen >> 8);
        aData[4 + n * 3] = static_cast<BYTE>(sColor.uBlue >> 8);
    }
    nDataLen = 2 + nColorCount * 3;

    hRes = CreateRazerJson(GOVEE_CMD_LED_DATA, aData, nDataLen, strJsonPacket);
    if (FAILED(hRes) || SendPacket(strJsonPacket.c_str(), strJsonPacket.length()) == FALSE)
    {
        SetDeviceStatus(L"Unable to send razer color data", 91);
        return;
    }

    m_ullLastColorUpdateMs = ullNow;
}

VOID CGoveeStripLighting::SendColor(_In_ const CBaseAudioProcessor::RGB &sColor) noexcept
{
    BYTE aPayload[19];
    ULONGLONG ullNow;
    std::string strJsonPacket;
    HRESULT hRes;

    if ((!m_lpGoveeStripDevice) || m_lpGoveeStripDevice->IsReady() == FALSE)
    {
        return;
    }

    ullNow = GetCurrentTimeMs();
    if (ullNow - m_ullLastTurnOnMs >= GOVEE_TURN_ON_REFRESH_MS)
    {
        if (!SendTurnOnOff(TRUE))
        {
            return;
        }
    }
    if (ullNow - m_ullLastColorUpdateMs < GOVEE_COLOR_UPDATE_INTERVAL_MS)
    {
        return;
    }

    aPayload[0] = 0x33; // Prefix
    aPayload[1] = 0x05; // Set color
    aPayload[2] = 0x02; // Manual
    aPayload[3] = static_cast<BYTE>(sColor.uRed >> 8);
    aPayload[4] = static_cast<BYTE>(sColor.uGreen >> 8);
    aPayload[5] = static_cast<BYTE>(sColor.uBlue >> 8);
    memset(aPayload + 6, 0, 13);

    hRes = CreatePtRealJson(aPayload, sizeof(aPayload), strJsonPacket);
    if (FAILED(hRes) || (!SendPacket(strJsonPacket.c_str(), strJsonPacket.length())))
    {
        SetDeviceStatus(L"Unable to send single color data", 91);
        return;
    }

    m_ullLastColorUpdateMs = ullNow;
    SetDeviceStatus(L"Streaming", 92);
}

/*
VOID CGoveeStripLighting::SendColor(_In_ const CBaseAudioProcessor::RGB &sColor) noexcept
{
    CHAR szJsonA[sizeof(
        "{\"msg\":{\"cmd\":\"colorwc\",\"data\":{\"color\":{\"r\":255,\"g\":255,\"b\":255},\"colorTemInKelvin\":0}}}"
    )];
    int iJsonLength;
    ULONGLONG ullNow;

    if (m_hSocket == INVALID_SOCKET)
    {
        return;
    }

    ullNow = GetCurrentTimeMs();
    if (ullNow - m_ullLastTurnOnMs >= GOVEE_TURN_ON_REFRESH_MS)
    {
        if (!SendTurnOnOff(TRUE))
        {
            return;
        }
    }
    if (ullNow - m_ullLastColorUpdateMs < GOVEE_COLOR_UPDATE_INTERVAL_MS)
    {
        return;
    }

    iJsonLength = sprintf_s(szJsonA, _countof(szJsonA),
                            "{\"msg\":{\"cmd\":\"colorwc\",\"data\":{\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},\"colorTemInKelvin\":0}}}",
                            static_cast<UINT>(sColor.uRed >> 8), static_cast<UINT>(sColor.uGreen >> 8),
                            static_cast<UINT>(sColor.uBlue >> 8));
    if (iJsonLength <= 0 || (!SendPacket(szJsonA, static_cast<SIZE_T>(iJsonLength))))
    {
        SetDeviceStatus(L"Unable to send single color data", 91);
        return;
    }

    m_ullLastColorUpdateMs = ullNow;
    SetDeviceStatus(L"Streaming", 92);
}
*/

VOID CGoveeStripLighting::SendUniformColor(_In_ const CBaseAudioProcessor::RGB &sColor) noexcept
{
    if (m_bSupportsRazer != FALSE)
    {
        for (CBaseAudioProcessor::RGB &sStripColor : m_vecStripColors)
        {
            sStripColor = sColor;
        }
        SendRazerColors();
    }
    else
    {
        SendColor(sColor);
    }
}

BOOL CGoveeStripLighting::GetManualColor(_Out_ CBaseAudioProcessor::RGB &sColor) noexcept
{
    CMutex::CAutoLock lock(m_cManualColorMutex);

    if (m_sManualColor.bFixedIsActive == FALSE)
    {
        sColor = {};
        return FALSE;
    }

    sColor.uRed = static_cast<USHORT>((static_cast<UINT32>(m_sManualColor.uRed) * m_sManualColor.uBrightness / 255) << 8);
    sColor.uGreen = static_cast<USHORT>((static_cast<UINT32>(m_sManualColor.uGreen) * m_sManualColor.uBrightness / 255) << 8);
    sColor.uBlue = static_cast<USHORT>((static_cast<UINT32>(m_sManualColor.uBlue) * m_sManualColor.uBrightness / 255) << 8);
    return TRUE;
}

BOOL CGoveeStripLighting::SendPacket(_In_ LPCVOID lpData, _In_ SIZE_T nDataSize) noexcept
{
    return m_lpGoveeStripDevice ? m_lpGoveeStripDevice->SendPacket(lpData, nDataSize) : FALSE;
}
