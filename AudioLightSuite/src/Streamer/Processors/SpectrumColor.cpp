#include "SpectrumColor.h"
#include "TUI\TUiFrameWindow.h"

// -----------------------------------------------------------------------------

VOID CSpectrumColorAudioProcessor::Initialize(_In_ Config &sConfig) noexcept
{
    m_sConfig = sConfig;
}

VOID CSpectrumColorAudioProcessor::FirstRender() noexcept
{
    _ASSERT(m_lpTerminalControl);
    RenderStaticLabels();
    RenderCurrentState();
    RenderOutputView();
}

HRESULT CSpectrumColorAudioProcessor::OnPostAttachEvent() noexcept
{
    CTUiFrameWindow *lpFrameWindow;

    lpFrameWindow = dynamic_cast<CTUiFrameWindow *>(ParentWindow());
    if (lpFrameWindow)
    {
        return lpFrameWindow->SetCaption(L"Spectrum Color Processor");
    }

    return S_OK;
}

VOID CSpectrumColorAudioProcessor::Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept
{
    if (sFrame.ulSamplesCount == 0)
    {
        return;
    }

    float fBlockTime = sFrame.fBlockTimeSec;

    float fSubKickThumpRMS = sFrame.fBandRmsNorm[CAudioAnalyzer::BAND_SUB_KICK_THUMP_INDEX];
    float fKickBassRMS = sFrame.fBandRmsNorm[CAudioAnalyzer::BAND_KICK_BASS_INDEX];
    float fMidBodyRMS = sFrame.fBandRmsNorm[CAudioAnalyzer::BAND_MID_BODY_INDEX];
    float fHiHatsSparkleAirRMS = sFrame.fBandRmsNorm[CAudioAnalyzer::BAND_HI_HATS_SPARKLE_AIR_INDEX];

    float fPowerRMS = 0.0f;
    BOOL bUsingMidBodyAsPower = FALSE;
    if (m_fSubKickThumpValidityTimer > MATH_EPSILON)
    {
        fPowerRMS = m_fSubKickThumpDecayTimer / m_sConfig.fInitialDecayTimer;

        m_fSubKickThumpValidityTimer = MathHelpers::SubstractWithMin(m_fSubKickThumpValidityTimer, fBlockTime, 0.0f);
        m_fSubKickThumpDecayTimer = MathHelpers::SubstractWithMin(m_fSubKickThumpDecayTimer, fBlockTime, 0.0f);

        if (fSubKickThumpRMS >= m_sConfig.fSubKickThumpPowerThreshold)
        {
            fPowerRMS = 1.0f;
            m_fSubKickThumpValidityTimer = m_sConfig.fInitialValidityTimer;
            m_fSubKickThumpDecayTimer = m_sConfig.fInitialDecayTimer;
        }
    }
    else if (m_fKickBassValidityTimer > MATH_EPSILON)
    {
        fPowerRMS = m_fKickBassDecayTimer / m_sConfig.fInitialDecayTimer;

        m_fKickBassValidityTimer = MathHelpers::SubstractWithMin(m_fKickBassValidityTimer, fBlockTime, 0.0f);
        m_fKickBassDecayTimer = MathHelpers::SubstractWithMin(m_fKickBassDecayTimer, fBlockTime, 0.0f);

        if (fKickBassRMS >= m_sConfig.fKickBassPowerThreshold)
        {
            fPowerRMS = 1.0f;
            m_fKickBassValidityTimer = m_sConfig.fInitialValidityTimer;
            m_fKickBassDecayTimer = m_sConfig.fInitialDecayTimer;
        }
    }
    else
    {
        if (fSubKickThumpRMS >= m_sConfig.fSubKickThumpPowerThreshold)
        {
            fPowerRMS = 1.0f;
            m_fSubKickThumpValidityTimer = m_sConfig.fInitialValidityTimer;
            m_fSubKickThumpDecayTimer = m_sConfig.fInitialDecayTimer;
        }
        else if (fKickBassRMS >= m_sConfig.fKickBassPowerThreshold)
        {
            fPowerRMS = 1.0f;
            m_fKickBassValidityTimer = m_sConfig.fInitialValidityTimer;
            m_fKickBassDecayTimer = m_sConfig.fInitialDecayTimer;
        }
    }

    if (m_fSubKickThumpValidityTimer < MATH_EPSILON && m_fKickBassValidityTimer < MATH_EPSILON)
    {
        fPowerRMS = fMidBodyRMS;
        bUsingMidBodyAsPower = TRUE;
    }

    float fHiHatsSparkleAirPowerRMS = 0.0f;
    if (m_fHiHatsSparkleAirValidityTimer > MATH_EPSILON)
    {
        fHiHatsSparkleAirPowerRMS = m_fHiHatsSparkleAirDecayTimer / m_sConfig.fInitialDecayTimer;

        m_fHiHatsSparkleAirValidityTimer = MathHelpers::SubstractWithMin(m_fHiHatsSparkleAirValidityTimer, fBlockTime, 0.0f);
        m_fHiHatsSparkleAirDecayTimer = MathHelpers::SubstractWithMin(m_fHiHatsSparkleAirDecayTimer, fBlockTime, 0.0f);

        if (bUsingMidBodyAsPower && fHiHatsSparkleAirRMS >= m_sConfig.fHiHatsSparkleAirPowerThreshold)
        {
            fHiHatsSparkleAirPowerRMS = 1.0f;
            m_fHiHatsSparkleAirValidityTimer = m_sConfig.fInitialValidityTimer;
            m_fHiHatsSparkleAirDecayTimer = m_sConfig.fInitialDecayTimer;
        }
    }
    else
    {
        if (bUsingMidBodyAsPower && fHiHatsSparkleAirRMS >= m_sConfig.fHiHatsSparkleAirPowerThreshold)
        {
            fHiHatsSparkleAirPowerRMS = 1.0f;
            m_fHiHatsSparkleAirValidityTimer = m_sConfig.fInitialValidityTimer;
            m_fHiHatsSparkleAirDecayTimer = m_sConfig.fInitialDecayTimer;
        }
    }

    // Update hue rotation; mid drives speed
    const float fSpeed = MathHelpers::Lerp(m_sConfig.fHueSpeedMin, m_sConfig.fHueSpeedMax, fMidBodyRMS);
    m_fLightsHuePhase += fSpeed * fBlockTime;
    m_fLightsHuePhase = MathHelpers::Frac(m_fLightsHuePhase);

    // Base color from rotating palette
    float fBaseR, fBaseG, fBaseB;
    PaletteColor(m_fLightsHuePhase, fBaseR, fBaseG, fBaseB); // 0..1

    // Bass -> brightness (value) with gamma + clamp range
    float fPowerN = MathHelpers::RemapClamp(fPowerRMS, m_sConfig.fPowerMin, m_sConfig.fPowerMax);
    fPowerN = powf(fPowerN, m_sConfig.fPowerGamma); // "gamma" shaping

    // Mid body slightly boosts brightness/saturation so the body feels alive
    float fValue = MathHelpers::Clamp(fPowerN);
    // float fSatBoost = MathHelpers::Clamp(fMidBodyRMS * m_sConfig.fMidBodySaturationBoost);

    // Apply “saturation boost” by pulling color away from gray
    float fGray = (fBaseR + fBaseG + fBaseB) * (1.0f / 3.0f);
    fBaseR = MathHelpers::Clamp(fGray + (fBaseR - fGray));
    fBaseG = MathHelpers::Clamp(fGray + (fBaseG - fGray));
    fBaseB = MathHelpers::Clamp(fGray + (fBaseB - fGray));

    float fR = fBaseR * fValue;
    float fG = fBaseG * fValue;
    float fB = fBaseB * fValue;

    // Treble -> white sparks (fast, glittery)
    // Smooth treble a bit, then use "excess" as an onset measure
    m_fLightsHiHatsSparkleAirSmooth = m_fLightsHiHatsSparkleAirSmooth +
        (fHiHatsSparkleAirPowerRMS - m_fLightsHiHatsSparkleAirSmooth) * 0.12f;

    float fOnset = (fHiHatsSparkleAirPowerRMS > m_fLightsHiHatsSparkleAirSmooth) ?
        fHiHatsSparkleAirPowerRMS - m_fLightsHiHatsSparkleAirSmooth : 0.0f;

    // Gate + gain
    float fSparkIn = 0.0f;
    if (fHiHatsSparkleAirPowerRMS > m_sConfig.fSparkThreshold)
    {
        fSparkIn = MathHelpers::Clamp(fOnset * m_sConfig.fSparkGain);
    }

    // Add some randomness so it "sparks" instead of turning uniformly white
    // (random only matters when treble is present)
    float fRand = NextRand();
    float fTwinkle = MathHelpers::Clamp((fRand - 0.6f) * 2.5f); // mostly 0, sometimes >0
    fSparkIn = MathHelpers::Clamp(fSparkIn + fTwinkle * fHiHatsSparkleAirPowerRMS * 0.35f);

    // Attack/release for spark envelope
    if (fSparkIn > m_fLightsSpark)
    {
        m_fLightsSpark = m_fLightsSpark + (fSparkIn - m_fLightsSpark) * m_sConfig.fSparkAttack;
    }
    else
    {
        m_fLightsSpark = m_fLightsSpark + (fSparkIn - m_fLightsSpark) * m_sConfig.fSparkDecay;
    }

    m_fLightsSpark = MathHelpers::Clamp(m_fLightsSpark);

    float fWhite = MathHelpers::Clamp(m_fLightsSpark * m_sConfig.fSparkWhiteMax);

    // Additive white overlay, then clamp
    fR = MathHelpers::Clamp(fR + fWhite);
    fG = MathHelpers::Clamp(fG + fWhite);
    fB = MathHelpers::Clamp(fB + fWhite);

    // Master
    fR = MathHelpers::Clamp(fR * m_sConfig.fMaster);
    fG = MathHelpers::Clamp(fG * m_sConfig.fMaster);
    fB = MathHelpers::Clamp(fB * m_sConfig.fMaster);

    // Convert to 8-bit
    {
        CRwMutex::CAutoExclusiveLock lock(m_cCurrentStateMutex);

        m_sCurrentState.sOutputRGB.r = (BYTE)(fR * 255.0f + 0.5f);
        m_sCurrentState.sOutputRGB.g = (BYTE)(fG * 255.0f + 0.5f);
        m_sCurrentState.sOutputRGB.b = (BYTE)(fB * 255.0f + 0.5f);

        m_sCurrentState.fSubKickThumpRMS = fSubKickThumpRMS;
        m_sCurrentState.fKickBassRMS = fKickBassRMS;
        m_sCurrentState.fMidBodyRMS = fMidBodyRMS;
        m_sCurrentState.fHiHatsSparkleAirRMS = fHiHatsSparkleAirRMS;
        m_sCurrentState.fPowerRMS = fPowerRMS;
        m_sCurrentState.fSubKickThumpValidityTimer = m_fSubKickThumpValidityTimer;
        m_sCurrentState.fSubKickThumpDecayTimer = m_fSubKickThumpDecayTimer;
        m_sCurrentState.fKickBassValidityTimer = m_fKickBassValidityTimer;
        m_sCurrentState.fKickBassDecayTimer = m_fKickBassDecayTimer;
        m_sCurrentState.fHuePhase = m_fLightsHuePhase;
        m_sCurrentState.fHueSpeed = fSpeed;
        m_sCurrentState.fBaseRed = fBaseR;
        m_sCurrentState.fBaseGreen = fBaseG;
        m_sCurrentState.fBaseBlue = fBaseB;
        m_sCurrentState.fValue = fValue;
        m_sCurrentState.fWhite = fWhite;
        m_sCurrentState.fHiHatsSparkleAirPowerRMS = fHiHatsSparkleAirPowerRMS;
        m_sCurrentState.fSparkSmooth = m_fLightsHiHatsSparkleAirSmooth;
        m_sCurrentState.fOnset = fOnset;
        m_sCurrentState.fSparkIn = fSparkIn;
        m_sCurrentState.fSpark = m_fLightsSpark;
        m_sCurrentState.fTwinkle = fTwinkle;
        m_sCurrentState.bUsingMidBodyAsPower = bUsingMidBodyAsPower;
    }
}

VOID CSpectrumColorAudioProcessor::TimerTick() noexcept
{
    if (!m_lpTerminalControl)
    {
        return;
    }

    RenderCurrentState();
    RenderOutputView();
}

VOID CSpectrumColorAudioProcessor::RenderStaticLabels() noexcept
{
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;1H\x1b[96mRMS Bands\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;1H\x1b[97mSub Kick:\x1b[0m\x1b[2;19H\x1b[97mKick Bass:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;1H\x1b[97mMid Body:\x1b[0m\x1b[3;19H\x1b[97mHi-Hat:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[4;1H\x1b[96mPower\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[5;1H\x1b[97mPower:\x1b[0m\x1b[5;15H\x1b[97mMid Power:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;1H\x1b[97mSub  Valid:\x1b[0m\x1b[6;20H\x1b[97mDecay:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;1H\x1b[97mBass Valid:\x1b[0m\x1b[7;20H\x1b[97mDecay:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[8;1H\x1b[96mColor\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[9;1H\x1b[97mBase RGB:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[10;1H\x1b[97mHue:\x1b[0m\x1b[10;16H\x1b[97mSpeed:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[11;1H\x1b[97mValue:\x1b[0m\x1b[11;16H\x1b[97mWhite:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[12;1H\x1b[96mSpark\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[13;1H\x1b[97mSource:\x1b[0m\x1b[13;16H\x1b[97mSmooth:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[14;1H\x1b[97mOnset:\x1b[0m\x1b[14;16H\x1b[97mInput:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[15;1H\x1b[97mSpark:\x1b[0m\x1b[15;16H\x1b[97mTwinkle:\x1b[0m");
}

VOID CSpectrumColorAudioProcessor::RenderCurrentState() noexcept
{
    CurrentState_t sCurrentState;

    {
        CRwMutex::CAutoSharedLock lock(m_cCurrentStateMutex);

        sCurrentState = m_sCurrentState;
    }

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;11H%6.4f\x1b[2;30H%6.4f",
                                     sCurrentState.fSubKickThumpRMS, sCurrentState.fKickBassRMS);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;11H%6.4f\x1b[3;30H%6.4f",
                                     sCurrentState.fMidBodyRMS, sCurrentState.fHiHatsSparkleAirRMS);

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[5;8H%5.3f\x1b[5;26H%ls", sCurrentState.fPowerRMS,
                                     (sCurrentState.bUsingMidBodyAsPower != FALSE) ? L"YES" : L"NO ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;13H%5.3f\x1b[6;27H%5.3f",
                                     sCurrentState.fSubKickThumpValidityTimer, sCurrentState.fSubKickThumpDecayTimer);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;13H%5.3f\x1b[7;27H%5.3f",
                                     sCurrentState.fKickBassValidityTimer, sCurrentState.fKickBassDecayTimer);

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[9;11H%5.2f\x1b[9;17H%5.2f\x1b[9;23H%5.2f",
                                     sCurrentState.fBaseRed, sCurrentState.fBaseGreen, sCurrentState.fBaseBlue);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[10;8H%5.3f\x1b[10;23H%5.3f",
                                     sCurrentState.fHuePhase, sCurrentState.fHueSpeed);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[11;8H%5.3f\x1b[11;23H%5.3f",
                                     sCurrentState.fValue, sCurrentState.fWhite);

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[13;9H%5.3f\x1b[13;25H%5.3f",
                                     sCurrentState.fHiHatsSparkleAirPowerRMS, sCurrentState.fSparkSmooth);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[14;9H%5.3f\x1b[14;25H%5.3f",
                                     sCurrentState.fOnset, sCurrentState.fSparkIn);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[15;9H%5.3f\x1b[15;25H%5.3f",
                                     sCurrentState.fSpark, sCurrentState.fTwinkle);
}

// Rotating palette: red->yellow->green->cyan->blue->magenta->red
void CSpectrumColorAudioProcessor::PaletteColor(_In_ float t, _Out_ float &r, _Out_ float &g, _Out_ float &b) noexcept
{
    // 6 segments
    const float seg = t * 6.0f;
    int i = (int)seg;
    float f = seg - (float)i;

    // Anchor colors
    // 0: R(1,0,0)
    // 1: Y(1,1,0)
    // 2: G(0,1,0)
    // 3: C(0,1,1)
    // 4: B(0,0,1)
    // 5: M(1,0,1)
    // 6: R(1,0,0)
    float r0 = 0, g0 = 0, b0 = 0, r1 = 0, g1 = 0, b1 = 0;

    switch (i)
    {
        default:
        case 0:
            r0 = 1.0f; g0 = 0.0f; b0 = 0.0f; r1 = 1.0f; g1 = 1.0f; b1 = 0.0f;
            break;
        case 1:
            r0 = 1.0f; g0 = 1.0f; b0 = 0.0f; r1 = 0.0f; g1 = 1.0f; b1 = 0.0f;
            break;
        case 2:
            r0 = 0.0f; g0 = 1.0f; b0 = 0.0f; r1 = 0.0f; g1 = 1.0f; b1 = 1.0f;
            break;
        case 3:
            r0 = 0.0f; g0 = 1.0f; b0 = 1.0f; r1 = 0.0f; g1 = 0.0f; b1 = 1.0f;
            break;
        case 4:
            r0 = 0.0f; g0 = 0.0f; b0 = 1.0f; r1 = 1.0f; g1 = 0.0f; b1 = 1.0f;
            break;
        case 5:
            r0 = 1.0f; g0 = 0.0f; b0 = 1.0f; r1 = 1.0f; g1 = 0.0f; b1 = 0.0f;
            break;
    }

    r = MathHelpers::Clamp(MathHelpers::Lerp(r0, r1, f));
    g = MathHelpers::Clamp(MathHelpers::Lerp(g0, g1, f));
    b = MathHelpers::Clamp(MathHelpers::Lerp(b0, b1, f));
}

float CSpectrumColorAudioProcessor::NextRand() noexcept
{
    // xorshift32
    UINT32 x = m_ulRng;
    x ^= (x << 13);
    x ^= (x >> 17);
    x ^= (x << 5);
    m_ulRng = x;

    // Convert top 24 bits to [0,1)
    UINT32 v = (x >> 8) & 0x00FFFFFFu;
    return (float)v * (1.0f / 16777216.0f);
}
