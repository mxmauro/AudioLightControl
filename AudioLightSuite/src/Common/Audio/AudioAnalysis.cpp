#include "AudioAnalysis.h"
#include "..\Utils\Exception2HR.h"
#include "..\Utils\Timer.h"

// -----------------------------------------------------------------------------

static constexpr ULONGLONG MAX_ANALYSIS_HISTORY_AGE_MS = 1000;
static constexpr ULONGLONG ONSET_BASELINE_MIN_AGE_MS = 30;
static constexpr ULONGLONG ONSET_BASELINE_MAX_AGE_MS = 150;
static constexpr float CLIPPING_THRESHOLD = 0.995f;
static constexpr float SPECTRAL_HOP_DURATION_SEC = 1.0f / 46.875f;
static constexpr float SPECTRAL_EPSILON = 1.0e-12f;
static constexpr float SPECTRAL_BROADBAND_FLUX_THRESHOLD = 0.10f;
static constexpr float FFT_DISPLAY_DYNAMIC_RANGE_DB = 80.0f;
static constexpr float PI_F = 3.14159265358979323846f;
static constexpr float TEMPO_SAMPLE_DURATION_SEC = 0.020f;
static constexpr float TEMPO_MIN_BPM = 55.0f;
static constexpr float TEMPO_MAX_BPM = 210.0f;
static constexpr UINT32 TEMPO_MIN_HISTORY_SAMPLE_COUNT = 400;
static constexpr UINT32 TEMPO_ESTIMATE_INTERVAL_SAMPLES = 25;
static constexpr float TEMPO_MIN_CONFIDENCE = 0.15f;

static const CAudioAnalyzer::BandDefinition_t aBandDefinitions[CAudioAnalyzer::BAND_COUNT] = {
    { CAudioAnalyzer::BandGroupLow, 50.0f, 10.0f },
    { CAudioAnalyzer::BandGroupLow, 80.0f, 10.0f },
    { CAudioAnalyzer::BandGroupLow, 125.0f, 20.0f },
    { CAudioAnalyzer::BandGroupLow, 200.0f, 60.0f },
    { CAudioAnalyzer::BandGroupLow, 315.0f, 140.0f },
    { CAudioAnalyzer::BandGroupMid, 400.0f, 200.0f },
    { CAudioAnalyzer::BandGroupMid, 630.0f, 300.0f },
    { CAudioAnalyzer::BandGroupMid, 1000.0f, 500.0f },
    { CAudioAnalyzer::BandGroupMid, 1600.0f, 800.0f },
    { CAudioAnalyzer::BandGroupMid, 2500.0f, 1200.0f },
    { CAudioAnalyzer::BandGroupHigh, 4000.0f, 2000.0f },
    { CAudioAnalyzer::BandGroupHigh, 6300.0f, 3200.0f },
    { CAudioAnalyzer::BandGroupHigh, 9000.0f, 5400.0f },
    { CAudioAnalyzer::BandGroupHigh, 14000.0f, 4000.0f }
};

// -----------------------------------------------------------------------------

static UINT32 NextPowerOfTwo(_In_ UINT32 ulValue) noexcept;
static float EnvelopeCoefficient(_In_ float fDurationSec, _In_ float fTimeConstantSec) noexcept;
static float ComputeRangeEnergy(_In_ const std::vector<std::complex<float>> &vecFftWork, _In_ float fSampleRate,
                                _In_ float fStartHz, _In_ float fEndHz) noexcept;
static float ComputeNormalizedAutocorrelation(_In_reads_(ulSampleCount) const float *lpfSamples, _In_ UINT32 ulSampleCount,
                                              _In_ UINT32 ulLag) noexcept;

// -----------------------------------------------------------------------------

HRESULT CAudioAnalyzer::Initialize(_In_ float fSampleRate) noexcept
{
    HRESULT hRes;
    UINT32 i;

    Reset();
    m_fSampleRate = fSampleRate;

    if (m_fSampleRate > MATH_EPSILON)
    {
        UINT32 ulTargetWindowSize = static_cast<UINT32>(m_fSampleRate * SPECTRAL_HOP_DURATION_SEC + 0.5f);

        m_ulSpectralWindowSize = NextPowerOfTwo(ulTargetWindowSize);
        if (m_ulSpectralWindowSize < 256)
        {
            m_ulSpectralWindowSize = 256;
        }
        m_ulSpectralHopSize = m_ulSpectralWindowSize / 2;

        hRes = TranslateToHresult([&]()
        {
            m_vecSpectralRing.assign(m_ulSpectralWindowSize, 0.0f);
            m_vecHannWindow.resize(m_ulSpectralWindowSize);
            m_vecCurrentMagnitudes.assign(m_ulSpectralWindowSize / 2 + 1, 0.0f);
            m_vecFftMagnitudeNorm.assign(m_ulSpectralWindowSize / 2 + 1, 0.0f);
            m_vecPreviousMagnitudes.assign(m_ulSpectralWindowSize / 2 + 1, 0.0f);
            m_vecSpectralMagnitudeHistory.assign(static_cast<SIZE_T>(SPECTRAL_MEDIAN_HISTORY_COUNT) *
                                                     (m_ulSpectralWindowSize / 2 + 1), 0.0f);
            m_vecFftWork.resize(m_ulSpectralWindowSize);
        });
        if (FAILED(hRes))
        {
            Reset();
            return hRes;
        }

        for (i = 0; i < m_ulSpectralWindowSize; i++)
        {
            m_vecHannWindow[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)i / (float)(m_ulSpectralWindowSize - 1));
        }

        for (i = 0; i < BAND_COUNT; i++)
        {
            m_aBands[i] = MathHelpers::CBiQuad::CreateBandPassWithWidth(m_fSampleRate, aBandDefinitions[i].fCenterHz,
                                                                        aBandDefinitions[i].fWidthHz);
        }
    }

    // Done
    return S_OK;
}
VOID CAudioAnalyzer::Analyze(_In_reads_(ulSamplesCount) const float *lpfPCM, _In_ UINT32 ulSamplesCount, _Out_ Frame_t &sFrame) noexcept
{
    ULONGLONG ullNow;
    UINT32 ulHistoricalFrameInsertIndex;

    sFrame = {};

    if ((!lpfPCM) || ulSamplesCount == 0 || m_fSampleRate <= MATH_EPSILON)
    {
        return;
    }

    sFrame.lpfPCM = lpfPCM;
    sFrame.ulSamplesCount = ulSamplesCount;
    sFrame.ullFirstSampleIndex = m_ullNextSampleIndex;
    sFrame.ullEndSampleIndex = m_ullNextSampleIndex + ulSamplesCount;
    m_ullNextSampleIndex = sFrame.ullEndSampleIndex;
    sFrame.fBlockTimeSec = (float)ulSamplesCount / m_fSampleRate;

    ullNow = GetCurrentTimeMs();

    ComputeBandsRMS(sFrame);
    ComputeBlockMetrics(sFrame);
    ComputeGroupedEnergies(sFrame);

    ulHistoricalFrameInsertIndex = AppendHistory(sFrame, ullNow);

    NormalizeMetrics(sFrame, ulHistoricalFrameInsertIndex, ullNow);

    ComputeOnsetStrength(sFrame, ullNow);
    ComputeSpectralMetrics(sFrame);
    UpdateTempo(sFrame);
}

const CAudioAnalyzer::BandDefinition_t *CAudioAnalyzer::GetBandDefinitions() noexcept
{
    return aBandDefinitions;
}

VOID CAudioAnalyzer::Reset() noexcept
{
    SIZE_T i;

    m_ulSpectralWindowSize = 0;
    m_ulSpectralHopSize = 0;
    m_vecSpectralRing.clear();
    m_vecHannWindow.clear();
    m_vecCurrentMagnitudes.clear();
    m_vecFftMagnitudeNorm.clear();
    m_vecPreviousMagnitudes.clear();
    m_vecSpectralMagnitudeHistory.clear();
    m_vecFftWork.clear();
    for (i = 0; i < BAND_COUNT; i++)
    {
        m_aBands[i] = MathHelpers::CBiQuad::CBiQuad();
    }

    m_ulSpectralWriteIndex = 0;
    m_ulSpectralSamplesAvailable = 0;
    m_ulSamplesSinceSpectralHop = 0;
    m_bPreviousMagnitudesValid = FALSE;
    m_ulSpectralMagnitudeHistoryCount = 0;
    m_ulNextSpectralMagnitudeHistory = 0;
    m_fHighFastEnvelope = 0.0f;
    m_fHighSlowEnvelope = 0.0f;
    ZeroMemory(m_afTempoHistory, sizeof(m_afTempoHistory));
    m_fTempoAccumulatedSignal = 0.0f;
    m_fTempoAccumulatedDurationSec = 0.0f;
    m_fPreviousLowEnergy = 0.0f;
    m_fBpm = 0.0f;
    m_fBpmConfidence = 0.0f;
    m_ulNextTempoHistorySample = 0;
    m_ulTempoHistorySampleCount = 0;
    m_ulTempoSamplesSinceEstimate = 0;
    m_ulNextHistoricalFrame = 0;
    m_ullNextSampleIndex = 0;
    ZeroMemory(m_aHistoricalFrames, sizeof(m_aHistoricalFrames));

    m_fSampleRate = 0.0f;
}

VOID CAudioAnalyzer::ComputeBandsRMS(_Inout_ Frame_t &sFrame) noexcept
{
    float fBandPower[BAND_COUNT]{};
    UINT32 i, nBand;

    for (i = 0; i < sFrame.ulSamplesCount; i++)
    {
        const float fSample = sFrame.lpfPCM[i];

        for (nBand = 0; nBand < BAND_COUNT; nBand++)
        {
            const float y = m_aBands[nBand].Process(fSample);
            fBandPower[nBand] += y * y;
        }
    }

    const float fInvCount = 1.0f / (float)sFrame.ulSamplesCount;
    for (i = 0; i < BAND_COUNT; i++)
    {
        sFrame.fBandRmsRaw[i] = sqrtf(fBandPower[i] * fInvCount);
    }
}

VOID CAudioAnalyzer::ComputeGroupedEnergies(_Inout_ Frame_t &sFrame) noexcept
{
    sFrame.fLowEnergyRaw = (sFrame.fBandRmsRaw[BAND_SUB_KICK_THUMP_INDEX] +
                            sFrame.fBandRmsRaw[BAND_KICK_BASS_INDEX] +
                            sFrame.fBandRmsRaw[BAND_BASS_NOTE_BODY_INDEX] +
                            sFrame.fBandRmsRaw[BAND_BASS_WARMTH_INDEX] +
                            sFrame.fBandRmsRaw[BAND_LOW_MID_PUNCH_INDEX]) / 5.0f;
    sFrame.fMidEnergyRaw = (sFrame.fBandRmsRaw[BAND_MID_BODY_INDEX] +
                            sFrame.fBandRmsRaw[BAND_BODY_GROWL_INDEX] +
                            sFrame.fBandRmsRaw[BAND_EDGE_INDEX] +
                            sFrame.fBandRmsRaw[BAND_ATTACK_DEFINITION_INDEX] +
                            sFrame.fBandRmsRaw[BAND_PRESENCE_INDEX]) / 5.0f;
    sFrame.fHighEnergyRaw = (sFrame.fBandRmsRaw[BAND_SNAP_STICK_INDEX] +
                             sFrame.fBandRmsRaw[BAND_CYMBAL_ONSET_INDEX] +
                             sFrame.fBandRmsRaw[BAND_HI_HATS_SPARKLE_AIR_INDEX]) / 3.0f;
}

VOID CAudioAnalyzer::ComputeBlockMetrics(_Inout_ Frame_t &sFrame) noexcept
{
    double lfPower;
    UINT32 i;

    lfPower = 0.0;
    sFrame.fPeak = 0.0f;
    for (i = 0; i < sFrame.ulSamplesCount; i++)
    {
        float fSample;
        float fAbsSample;

        fSample = sFrame.lpfPCM[i];
        fAbsSample = fabsf(fSample);
        if (fAbsSample > sFrame.fPeak)
        {
            sFrame.fPeak = fAbsSample;
        }

        lfPower += (double)fSample * (double)fSample;
    }

    sFrame.fBlockRms = sqrtf((float)(lfPower / (double)sFrame.ulSamplesCount));
    sFrame.bClippingDetected = (sFrame.fPeak >= CLIPPING_THRESHOLD) ? TRUE : FALSE;
}

UINT32 CAudioAnalyzer::AppendHistory(_In_ const Frame_t &sFrame, _In_ ULONGLONG ullNow) noexcept
{
    UINT32 ulHistoricalFrameInsertIndex = m_ulNextHistoricalFrame;

    m_aHistoricalFrames[m_ulNextHistoricalFrame].ullTimestamp = ullNow;
    memcpy(m_aHistoricalFrames[m_ulNextHistoricalFrame].fBandRmsRaw, sFrame.fBandRmsRaw, sizeof(sFrame.fBandRmsRaw));

    m_aHistoricalFrames[m_ulNextHistoricalFrame].fLowEnergyRaw = sFrame.fLowEnergyRaw;
    m_aHistoricalFrames[m_ulNextHistoricalFrame].fMidEnergyRaw = sFrame.fMidEnergyRaw;
    m_aHistoricalFrames[m_ulNextHistoricalFrame].fHighEnergyRaw = sFrame.fHighEnergyRaw;
    m_aHistoricalFrames[m_ulNextHistoricalFrame].fBlockRms = sFrame.fBlockRms;

    m_ulNextHistoricalFrame += 1;
    if (m_ulNextHistoricalFrame >= MAX_HISTORICAL_ANALYSIS_FRAMES)
    {
        m_ulNextHistoricalFrame = 0;
    }

    // Done
    return ulHistoricalFrameInsertIndex;
}

VOID CAudioAnalyzer::NormalizeMetrics(_Inout_ Frame_t &sFrame, _In_ UINT32 ulHistoricalFrameInsertIndex, _In_ ULONGLONG ullNow) noexcept
{
    float fMaxBandRms[BAND_COUNT];
    float fMaxLowEnergy;
    float fMaxMidEnergy;
    float fMaxHighEnergy;
    UINT32 nBand;
    HistoricalAnalysisFrame_t *lpCurrHistoricalFrame;

    memcpy(fMaxBandRms, sFrame.fBandRmsRaw, BAND_COUNT * sizeof(float));
    fMaxLowEnergy = sFrame.fLowEnergyRaw;
    fMaxMidEnergy = sFrame.fMidEnergyRaw;
    fMaxHighEnergy = sFrame.fHighEnergyRaw;

    lpCurrHistoricalFrame = &m_aHistoricalFrames[ulHistoricalFrameInsertIndex];

    for (UINT32 i = MAX_HISTORICAL_ANALYSIS_FRAMES; i > 0; i--)
    {
        if (lpCurrHistoricalFrame != m_aHistoricalFrames)
        {
            lpCurrHistoricalFrame--;
        }
        else
        {
            lpCurrHistoricalFrame = &m_aHistoricalFrames[MAX_HISTORICAL_ANALYSIS_FRAMES - 1];
        }

        if (lpCurrHistoricalFrame->ullTimestamp == 0 || lpCurrHistoricalFrame->ullTimestamp + MAX_ANALYSIS_HISTORY_AGE_MS < ullNow)
        {
            break;
        }

        for (nBand = 0; nBand < BAND_COUNT; nBand++)
        {
            if (lpCurrHistoricalFrame->fBandRmsRaw[nBand] > fMaxBandRms[nBand])
            {
                fMaxBandRms[nBand] = lpCurrHistoricalFrame->fBandRmsRaw[nBand];
            }
        }

        if (lpCurrHistoricalFrame->fLowEnergyRaw > fMaxLowEnergy)
        {
            fMaxLowEnergy = lpCurrHistoricalFrame->fLowEnergyRaw;
        }
        if (lpCurrHistoricalFrame->fMidEnergyRaw > fMaxMidEnergy)
        {
            fMaxMidEnergy = lpCurrHistoricalFrame->fMidEnergyRaw;
        }
        if (lpCurrHistoricalFrame->fHighEnergyRaw > fMaxHighEnergy)
        {
            fMaxHighEnergy = lpCurrHistoricalFrame->fHighEnergyRaw;
        }
    }

    for (nBand = 0; nBand < BAND_COUNT; nBand++)
    {
        sFrame.fBandRmsNorm[nBand] = sFrame.fBandRmsRaw[nBand] / (fMaxBandRms[nBand] + MATH_EPSILON);
    }
    sFrame.fLowEnergyNorm = sFrame.fLowEnergyRaw / (fMaxLowEnergy + MATH_EPSILON);
    sFrame.fMidEnergyNorm = sFrame.fMidEnergyRaw / (fMaxMidEnergy + MATH_EPSILON);
    sFrame.fHighEnergyNorm = sFrame.fHighEnergyRaw / (fMaxHighEnergy + MATH_EPSILON);
}

VOID CAudioAnalyzer::ComputeOnsetStrength(_Inout_ Frame_t &sFrame, _In_ ULONGLONG ullNow) noexcept
{
    float fBaselineMidEnergy;
    float fBaselineHighEnergy;
    float fCurrentCombinedEnergy;
    float fBaselineCombinedEnergy;
    float fDelta;
    UINT32 ulBaselineCount;
    UINT32 i;

    fBaselineMidEnergy = 0.0f;
    fBaselineHighEnergy = 0.0f;
    ulBaselineCount = 0;

    for (i = 0; i < MAX_HISTORICAL_ANALYSIS_FRAMES; i++)
    {
        const HistoricalAnalysisFrame_t &sHistoricalFrame = m_aHistoricalFrames[i];
        ULONGLONG ullAge;

        if (sHistoricalFrame.ullTimestamp == 0 || sHistoricalFrame.ullTimestamp > ullNow)
        {
            continue;
        }

        ullAge = ullNow - sHistoricalFrame.ullTimestamp;
        if (ullAge < ONSET_BASELINE_MIN_AGE_MS || ullAge > ONSET_BASELINE_MAX_AGE_MS)
        {
            continue;
        }

        fBaselineMidEnergy += sHistoricalFrame.fMidEnergyRaw;
        fBaselineHighEnergy += sHistoricalFrame.fHighEnergyRaw;
        ulBaselineCount += 1;
    }

    if (ulBaselineCount == 0)
    {
        sFrame.fOnsetStrength = 0.0f;
        return;
    }

    fBaselineMidEnergy /= (float)ulBaselineCount;
    fBaselineHighEnergy /= (float)ulBaselineCount;

    fCurrentCombinedEnergy = (sFrame.fMidEnergyRaw * 0.45f) + (sFrame.fHighEnergyRaw * 0.55f);
    fBaselineCombinedEnergy = (fBaselineMidEnergy * 0.45f) + (fBaselineHighEnergy * 0.55f);
    fDelta = fCurrentCombinedEnergy - fBaselineCombinedEnergy;
    if (fDelta <= 0.0f)
    {
        sFrame.fOnsetStrength = 0.0f;
        return;
    }

    sFrame.fOnsetStrength = MathHelpers::Clamp(fDelta / MathHelpers::Max(fCurrentCombinedEnergy, MATH_EPSILON));
}

VOID CAudioAnalyzer::ComputeSpectralMetrics(_Inout_ Frame_t &sFrame) noexcept
{
    double lfEnergy5To8 = 0.0;
    double lfEnergy8To12 = 0.0;
    double lfEnergyUpper = 0.0;
    double lfFlatness = 0.0;
    double lfPercussiveRatio = 0.0;
    double lfFlux5To8 = 0.0;
    double lfFlux8To12 = 0.0;
    double lfFlux12To18 = 0.0;
    UINT32 ulHopCount = 0;
    float fBestScore = 0.0f;
    UINT32 i;

    for (i = 0; i < sFrame.ulSamplesCount; i++)
    {
        SpectralMetrics_t sMetrics;

        m_vecSpectralRing[m_ulSpectralWriteIndex] = sFrame.lpfPCM[i];
        m_ulSpectralWriteIndex = (m_ulSpectralWriteIndex + 1) % m_ulSpectralWindowSize;
        if (m_ulSpectralSamplesAvailable < m_ulSpectralWindowSize)
        {
            m_ulSpectralSamplesAvailable += 1;
        }
        m_ulSamplesSinceSpectralHop += 1;

        if (m_ulSpectralSamplesAvailable < m_ulSpectralWindowSize || m_ulSamplesSinceSpectralHop < m_ulSpectralHopSize)
        {
            continue;
        }
        m_ulSamplesSinceSpectralHop = 0;
        if (!ComputeSpectralHop(sMetrics))
        {
            continue;
        }

        float fAirRatio = MathHelpers::Clamp(sMetrics.fEnergyUpper / (sMetrics.fEnergy5To8 + sMetrics.fEnergy8To12 + SPECTRAL_EPSILON));
        float fHighToMidRatio = MathHelpers::Clamp((sMetrics.fEnergy5To8 + sMetrics.fEnergy8To12 + sMetrics.fEnergyUpper) /
                                                   (sMetrics.fVoiceMidEnergy + SPECTRAL_EPSILON));
        float fBroadbandCoverage = (float)sMetrics.ulBroadbandCoverage / 3.0f;
        float fScore = sMetrics.fFlux * (0.4f + sMetrics.fFlatness * 0.6f) *
                       (0.3f + sMetrics.fTransientStrength * 0.7f) * (0.25f + fAirRatio * 0.75f) * fHighToMidRatio;
        fScore *= (0.25f + sMetrics.fPercussiveRatio * 0.75f) * (0.25f + fBroadbandCoverage * 0.75f);

        sFrame.bSpectralReady = TRUE;
        sFrame.lpfFftMagnitudeRaw = m_vecCurrentMagnitudes.data();
        sFrame.lpfFftMagnitudeNorm = m_vecFftMagnitudeNorm.data();
        sFrame.ulFftMagnitudeCount = static_cast<UINT32>(m_vecFftMagnitudeNorm.size());
        ulHopCount += 1;
        lfEnergy5To8 += sMetrics.fEnergy5To8;
        lfEnergy8To12 += sMetrics.fEnergy8To12;
        lfEnergyUpper += sMetrics.fEnergyUpper;
        lfFlatness += sMetrics.fFlatness;
        lfPercussiveRatio += sMetrics.fPercussiveRatio;
        lfFlux5To8 += sMetrics.fFlux5To8;
        lfFlux8To12 += sMetrics.fFlux8To12;
        lfFlux12To18 += sMetrics.fFlux12To18;
        if (fScore >= fBestScore)
        {
            fBestScore = fScore;
            sFrame.fSpectralFlux = sMetrics.fFlux;
            sFrame.fSpectralFlux5To8 = sMetrics.fFlux5To8;
            sFrame.fSpectralFlux8To12 = sMetrics.fFlux8To12;
            sFrame.fSpectralFlux12To18 = sMetrics.fFlux12To18;
            sFrame.ulSpectralBroadbandCoverage = sMetrics.ulBroadbandCoverage;
            sFrame.fSpectralPercussiveRatio = sMetrics.fPercussiveRatio;
            sFrame.fHighFastEnvelope = sMetrics.fFastEnvelope;
            sFrame.fHighSlowEnvelope = sMetrics.fSlowEnvelope;
            sFrame.fHighTransientStrength = sMetrics.fTransientStrength;
            sFrame.fSpectralAirRatio = fAirRatio;
            sFrame.fSpectralHighToMidRatio = fHighToMidRatio;
            sFrame.fTrebleScore = fScore;
        }
    }

    if (ulHopCount > 0)
    {
        sFrame.fSpectralEnergy5To8 = (float)(lfEnergy5To8 / (double)ulHopCount);
        sFrame.fSpectralEnergy8To12 = (float)(lfEnergy8To12 / (double)ulHopCount);
        sFrame.fSpectralEnergyUpper = (float)(lfEnergyUpper / (double)ulHopCount);
        sFrame.fSpectralFlatness = (float)(lfFlatness / (double)ulHopCount);
        sFrame.fSpectralPercussiveRatio = (float)(lfPercussiveRatio / (double)ulHopCount);
        sFrame.fSpectralFlux5To8 = (float)(lfFlux5To8 / (double)ulHopCount);
        sFrame.fSpectralFlux8To12 = (float)(lfFlux8To12 / (double)ulHopCount);
        sFrame.fSpectralFlux12To18 = (float)(lfFlux12To18 / (double)ulHopCount);
    }
}

BOOL CAudioAnalyzer::ComputeSpectralHop(_Out_ SpectralMetrics_t &sMetrics) noexcept
{
    float fNyquist;
    float fUpperStartHz;
    float fUpperEndHz;
    float fHighEndHz;
    float fHighEnergy;
    float fFastCoeff;
    float fSlowCoeff;
    double lfFlux = 0.0;
    double alfBandFlux[3]{};
    double alfBandMagnitude[3]{};
    double lfHighMagnitude = 0.0;
    double lfLogMagnitude = 0.0;
    double lfMagnitude = 0.0;
    UINT32 ulHighBinCount = 0;
    float fMaximumMagnitude = 0.0f;
    UINT32 ulFluxFirstBin;
    UINT32 ulFluxLastBin;
    UINT32 ulPercussiveFirstBin;
    UINT32 ulPercussiveLastBin;
    UINT32 i;

    sMetrics = {};
    if (m_ulSpectralWindowSize == 0 || m_vecFftWork.size() != m_ulSpectralWindowSize)
    {
        return FALSE;
    }

    double lfWindowMean = 0.0;
    for (i = 0; i < m_ulSpectralWindowSize; i++)
    {
        lfWindowMean += m_vecSpectralRing[i];
    }
    const float fWindowMean = (float)(lfWindowMean / (double)m_ulSpectralWindowSize);

    for (i = 0; i < m_ulSpectralWindowSize; i++)
    {
        UINT32 ulRingIndex = (m_ulSpectralWriteIndex + i) % m_ulSpectralWindowSize;
        m_vecFftWork[i] = std::complex<float>((m_vecSpectralRing[ulRingIndex] - fWindowMean) * m_vecHannWindow[i], 0.0f);
    }

    for (i = 1; i < m_ulSpectralWindowSize; i++)
    {
        UINT32 ulBit = m_ulSpectralWindowSize >> 1;
        UINT32 ulReversed = 0;
        UINT32 ulTemp = i;

        while (ulBit > 0)
        {
            if ((ulTemp & 1U) != 0)
            {
                ulReversed |= ulBit;
            }
            ulTemp >>= 1;
            ulBit >>= 1;
        }
        if (i < ulReversed)
        {
            std::swap(m_vecFftWork[i], m_vecFftWork[ulReversed]);
        }
    }

    for (UINT32 ulLength = 2; ulLength <= m_ulSpectralWindowSize; ulLength <<= 1)
    {
        float fAngle = -2.0f * PI_F / (float)ulLength;
        std::complex<float> cStep(cosf(fAngle), sinf(fAngle));

        for (UINT32 ulOffset = 0; ulOffset < m_ulSpectralWindowSize; ulOffset += ulLength)
        {
            std::complex<float> cTwiddle(1.0f, 0.0f);
            UINT32 ulHalfLength = ulLength / 2;

            for (UINT32 ulIndex = 0; ulIndex < ulHalfLength; ulIndex++)
            {
                std::complex<float> cEven = m_vecFftWork[ulOffset + ulIndex];
                std::complex<float> cOdd = cTwiddle * m_vecFftWork[ulOffset + ulIndex + ulHalfLength];

                m_vecFftWork[ulOffset + ulIndex] = cEven + cOdd;
                m_vecFftWork[ulOffset + ulIndex + ulHalfLength] = cEven - cOdd;
                cTwiddle *= cStep;
            }
        }
    }

    for (i = 0; i <= m_ulSpectralWindowSize / 2; i++)
    {
        const float fMagnitude = std::abs(m_vecFftWork[i]);

        m_vecCurrentMagnitudes[i] = fMagnitude;
        m_vecFftMagnitudeNorm[i] = fMagnitude;
        if (fMagnitude > fMaximumMagnitude)
        {
            fMaximumMagnitude = fMagnitude;
        }
    }
    for (i = 0; i <= m_ulSpectralWindowSize / 2; i++)
    {
        const float fRelativeDb = 20.0f * log10f(m_vecFftMagnitudeNorm[i] /
                                                  MathHelpers::Max(fMaximumMagnitude, SPECTRAL_EPSILON));

        m_vecFftMagnitudeNorm[i] = MathHelpers::Clamp((fRelativeDb + FFT_DISPLAY_DYNAMIC_RANGE_DB) /
                                                        FFT_DISPLAY_DYNAMIC_RANGE_DB);
    }

    fNyquist = m_fSampleRate * 0.5f;
    fUpperStartHz = fNyquist > 12000.0f ? 12000.0f : 8000.0f;
    fUpperEndHz = fNyquist < 18000.0f ? fNyquist : 18000.0f;
    fHighEndHz = fNyquist < 18000.0f ? fNyquist : 18000.0f;
    sMetrics.fEnergy5To8 = ComputeRangeEnergy(m_vecFftWork, m_fSampleRate, 5000.0f, 8000.0f);
    sMetrics.fEnergy8To12 = ComputeRangeEnergy(m_vecFftWork, m_fSampleRate, 8000.0f, 12000.0f);
    sMetrics.fEnergyUpper = ComputeRangeEnergy(m_vecFftWork, m_fSampleRate, fUpperStartHz, fUpperEndHz);
    sMetrics.fVoiceMidEnergy = ComputeRangeEnergy(m_vecFftWork, m_fSampleRate, 1000.0f, 4000.0f);

    ulFluxFirstBin = static_cast<UINT32>(ceilf(5000.0f * (float)m_ulSpectralWindowSize / m_fSampleRate));
    ulFluxLastBin = static_cast<UINT32>(floorf(fHighEndHz * (float)m_ulSpectralWindowSize / m_fSampleRate));
    if (ulFluxLastBin > m_ulSpectralWindowSize / 2)
    {
        ulFluxLastBin = m_ulSpectralWindowSize / 2;
    }
    if (ulFluxFirstBin <= ulFluxLastBin)
    {
        for (i = ulFluxFirstBin; i <= ulFluxLastBin; i++)
        {
            float fFrequency = (float)i * m_fSampleRate / (float)m_ulSpectralWindowSize;
            float fWeight = 0.5f + 0.5f * MathHelpers::Clamp((fFrequency - 6000.0f) / 12000.0f);
            float fMagnitude = m_vecCurrentMagnitudes[i];
            UINT32 ulBandIndex = fFrequency < 8000.0f ? 0 : (fFrequency < 12000.0f ? 1 : 2);

            if (m_bPreviousMagnitudesValid && fMagnitude > m_vecPreviousMagnitudes[i])
            {
                lfFlux += (double)(fMagnitude - m_vecPreviousMagnitudes[i]) * fWeight;
                alfBandFlux[ulBandIndex] += fMagnitude - m_vecPreviousMagnitudes[i];
            }
            lfHighMagnitude += fMagnitude;
            alfBandMagnitude[ulBandIndex] += fMagnitude;
            lfLogMagnitude += logf(fMagnitude + SPECTRAL_EPSILON);
            lfMagnitude += fMagnitude;
            m_vecPreviousMagnitudes[i] = fMagnitude;
            ulHighBinCount += 1;
        }
    }
    m_bPreviousMagnitudesValid = TRUE;

    if (ulHighBinCount > 0)
    {
        sMetrics.fFlux = MathHelpers::Clamp((float)(lfFlux / (lfHighMagnitude + SPECTRAL_EPSILON)));
        sMetrics.fFlatness = MathHelpers::Clamp(expf((float)(lfLogMagnitude / (double)ulHighBinCount)) /
                                                ((float)(lfMagnitude / (double)ulHighBinCount) + SPECTRAL_EPSILON));
    }

    sMetrics.fFlux5To8 = MathHelpers::Clamp((float)(alfBandFlux[0] / (alfBandMagnitude[0] + SPECTRAL_EPSILON)));
    sMetrics.fFlux8To12 = MathHelpers::Clamp((float)(alfBandFlux[1] / (alfBandMagnitude[1] + SPECTRAL_EPSILON)));
    sMetrics.fFlux12To18 = MathHelpers::Clamp((float)(alfBandFlux[2] / (alfBandMagnitude[2] + SPECTRAL_EPSILON)));
    sMetrics.ulBroadbandCoverage = (sMetrics.fFlux5To8 >= SPECTRAL_BROADBAND_FLUX_THRESHOLD ? 1U : 0U) +
                                    (sMetrics.fFlux8To12 >= SPECTRAL_BROADBAND_FLUX_THRESHOLD ? 1U : 0U) +
                                    (sMetrics.fFlux12To18 >= SPECTRAL_BROADBAND_FLUX_THRESHOLD ? 1U : 0U);
    ulPercussiveFirstBin = static_cast<UINT32>(ceilf(5000.0f * (float)m_ulSpectralWindowSize / m_fSampleRate));
    ulPercussiveLastBin = static_cast<UINT32>(floorf(fHighEndHz * (float)m_ulSpectralWindowSize / m_fSampleRate));
    if (ulPercussiveLastBin > m_ulSpectralWindowSize / 2)
    {
        ulPercussiveLastBin = m_ulSpectralWindowSize / 2;
    }
    if (m_ulSpectralMagnitudeHistoryCount == SPECTRAL_MEDIAN_HISTORY_COUNT && ulPercussiveFirstBin <= ulPercussiveLastBin)
    {
        double lfPercussiveMagnitude = 0.0;
        double lfCurrentMagnitude = 0.0;

        for (i = ulPercussiveFirstBin; i <= ulPercussiveLastBin; i++)
        {
            float afHistory[SPECTRAL_MEDIAN_HISTORY_COUNT];
            float fMedian;
            UINT32 nHistory;

            for (nHistory = 0; nHistory < SPECTRAL_MEDIAN_HISTORY_COUNT; nHistory++)
            {
                UINT32 nSort;

                afHistory[nHistory] = m_vecSpectralMagnitudeHistory[nHistory * (m_ulSpectralWindowSize / 2 + 1) + i];
                for (nSort = nHistory; nSort > 0 && afHistory[nSort] < afHistory[nSort - 1]; nSort--)
                {
                    std::swap(afHistory[nSort], afHistory[nSort - 1]);
                }
            }
            fMedian = afHistory[SPECTRAL_MEDIAN_HISTORY_COUNT / 2];
            lfPercussiveMagnitude += MathHelpers::Max(0.0f, m_vecCurrentMagnitudes[i] - fMedian);
            lfCurrentMagnitude += m_vecCurrentMagnitudes[i];
        }
        sMetrics.fPercussiveRatio = MathHelpers::Clamp((float)(lfPercussiveMagnitude / (lfCurrentMagnitude + SPECTRAL_EPSILON)));
    }

    memcpy(m_vecSpectralMagnitudeHistory.data() + static_cast<SIZE_T>(m_ulNextSpectralMagnitudeHistory) *
           (m_ulSpectralWindowSize / 2 + 1), m_vecCurrentMagnitudes.data(),
           sizeof(float) * m_vecCurrentMagnitudes.size());
    m_ulNextSpectralMagnitudeHistory = (m_ulNextSpectralMagnitudeHistory + 1) % SPECTRAL_MEDIAN_HISTORY_COUNT;
    if (m_ulSpectralMagnitudeHistoryCount < SPECTRAL_MEDIAN_HISTORY_COUNT)
    {
        m_ulSpectralMagnitudeHistoryCount += 1;
    }
    fHighEnergy = (sMetrics.fEnergy5To8 + sMetrics.fEnergy8To12 + sMetrics.fEnergyUpper) / 3.0f;
    fFastCoeff = EnvelopeCoefficient((float)m_ulSpectralHopSize / m_fSampleRate, fHighEnergy >= m_fHighFastEnvelope ? 0.010f : 0.040f);
    fSlowCoeff = EnvelopeCoefficient((float)m_ulSpectralHopSize / m_fSampleRate, fHighEnergy >= m_fHighSlowEnvelope ? 0.080f : 0.250f);
    m_fHighFastEnvelope += (fHighEnergy - m_fHighFastEnvelope) * fFastCoeff;
    m_fHighSlowEnvelope += (fHighEnergy - m_fHighSlowEnvelope) * fSlowCoeff;
    sMetrics.fFastEnvelope = m_fHighFastEnvelope;
    sMetrics.fSlowEnvelope = m_fHighSlowEnvelope;
    sMetrics.fTransientStrength = MathHelpers::Clamp((m_fHighFastEnvelope - m_fHighSlowEnvelope) /
                                                      (m_fHighSlowEnvelope + SPECTRAL_EPSILON));
    return TRUE;
}

VOID CAudioAnalyzer::UpdateTempo(_Inout_ Frame_t &sFrame) noexcept
{
    float fLowOnset;
    float fSpectralOnset;
    float fTempoSignal;

    if (sFrame.fLowEnergyRaw > m_fPreviousLowEnergy)
    {
        fLowOnset = (sFrame.fLowEnergyRaw - m_fPreviousLowEnergy) / (sFrame.fLowEnergyRaw + MATH_EPSILON);
    }
    else
    {
        fLowOnset = 0.0f;
    }
    m_fPreviousLowEnergy = sFrame.fLowEnergyRaw;

    fSpectralOnset = (sFrame.bSpectralReady != FALSE) ?
        sFrame.fSpectralFlux * (0.35f + sFrame.fSpectralPercussiveRatio * 0.65f) : 0.0f;
    fTempoSignal = MathHelpers::Clamp(sFrame.fOnsetStrength * 0.50f + fLowOnset * 0.35f + fSpectralOnset * 0.15f);

    float fRemainingDurationSec = sFrame.fBlockTimeSec;
    while (fRemainingDurationSec > 0.0f)
    {
        float fDurationToAppendSec = MathHelpers::Min(fRemainingDurationSec,
                                                       TEMPO_SAMPLE_DURATION_SEC - m_fTempoAccumulatedDurationSec);

        m_fTempoAccumulatedSignal += fTempoSignal * fDurationToAppendSec;
        m_fTempoAccumulatedDurationSec += fDurationToAppendSec;
        fRemainingDurationSec -= fDurationToAppendSec;
        if (m_fTempoAccumulatedDurationSec + MATH_EPSILON < TEMPO_SAMPLE_DURATION_SEC)
        {
            continue;
        }

        float fSample = m_fTempoAccumulatedSignal / TEMPO_SAMPLE_DURATION_SEC;

        m_afTempoHistory[m_ulNextTempoHistorySample] = fSample;
        m_ulNextTempoHistorySample = (m_ulNextTempoHistorySample + 1) % TEMPO_HISTORY_SAMPLE_COUNT;
        if (m_ulTempoHistorySampleCount < TEMPO_HISTORY_SAMPLE_COUNT)
        {
            m_ulTempoHistorySampleCount += 1;
        }
        m_ulTempoSamplesSinceEstimate += 1;
        m_fTempoAccumulatedSignal = 0.0f;
        m_fTempoAccumulatedDurationSec = 0.0f;
    }

    if (m_ulTempoHistorySampleCount >= TEMPO_MIN_HISTORY_SAMPLE_COUNT &&
        m_ulTempoSamplesSinceEstimate >= TEMPO_ESTIMATE_INTERVAL_SAMPLES)
    {
        float fBpm;
        float fConfidence;

        m_ulTempoSamplesSinceEstimate = 0;
        if (EstimateTempo(fBpm, fConfidence) != FALSE)
        {
            m_fBpm = fBpm;
            m_fBpmConfidence = fConfidence;
        }
        else
        {
            m_fBpm = 0.0f;
            m_fBpmConfidence = 0.0f;
        }
    }

    sFrame.bBpmReady = (m_fBpmConfidence > 0.0f) ? TRUE : FALSE;
    sFrame.fBpm = m_fBpm;
    sFrame.fBpmConfidence = m_fBpmConfidence;
}

BOOL CAudioAnalyzer::EstimateTempo(_Out_ float &fBpm, _Out_ float &fConfidence) const noexcept
{
    float afSamples[TEMPO_HISTORY_SAMPLE_COUNT];
    float afScores[TEMPO_HISTORY_SAMPLE_COUNT]{};
    float fMean;
    float fVariance;
    float fBestScore;
    float fSecondScore;
    UINT32 ulFirstLag;
    UINT32 ulLastLag;
    UINT32 ulBestLag;
    UINT32 i;

    fBpm = 0.0f;
    fConfidence = 0.0f;
    if (m_ulTempoHistorySampleCount < TEMPO_MIN_HISTORY_SAMPLE_COUNT)
    {
        return FALSE;
    }

    fMean = 0.0f;
    for (i = 0; i < m_ulTempoHistorySampleCount; i++)
    {
        UINT32 ulIndex = (m_ulNextTempoHistorySample + TEMPO_HISTORY_SAMPLE_COUNT - m_ulTempoHistorySampleCount + i) %
                         TEMPO_HISTORY_SAMPLE_COUNT;

        afSamples[i] = m_afTempoHistory[ulIndex];
        fMean += afSamples[i];
    }
    fMean /= (float)m_ulTempoHistorySampleCount;

    fVariance = 0.0f;
    for (i = 0; i < m_ulTempoHistorySampleCount; i++)
    {
        afSamples[i] -= fMean;
        fVariance += afSamples[i] * afSamples[i];
    }
    if (fVariance <= MATH_EPSILON)
    {
        return FALSE;
    }

    ulFirstLag = static_cast<UINT32>(ceilf(60.0f / (TEMPO_MAX_BPM * TEMPO_SAMPLE_DURATION_SEC)));
    ulLastLag = static_cast<UINT32>(floorf(60.0f / (TEMPO_MIN_BPM * TEMPO_SAMPLE_DURATION_SEC)));
    if (ulFirstLag < 2)
    {
        ulFirstLag = 2;
    }
    if (ulLastLag + 1 >= m_ulTempoHistorySampleCount)
    {
        ulLastLag = m_ulTempoHistorySampleCount - 2;
    }

    fBestScore = -1.0f;
    ulBestLag = 0;
    for (UINT32 ulLag = ulFirstLag; ulLag <= ulLastLag; ulLag++)
    {
        float fScore = ComputeNormalizedAutocorrelation(afSamples, m_ulTempoHistorySampleCount, ulLag);

        if (ulLag * 2 < m_ulTempoHistorySampleCount)
        {
            fScore = fScore * 0.80f + ComputeNormalizedAutocorrelation(afSamples, m_ulTempoHistorySampleCount, ulLag * 2) * 0.20f;
        }
        afScores[ulLag] = fScore;
        if (fScore > fBestScore)
        {
            fBestScore = fScore;
            ulBestLag = ulLag;
        }
    }
    if (ulBestLag == 0 || fBestScore < 0.12f)
    {
        return FALSE;
    }

    fSecondScore = -1.0f;
    for (UINT32 ulLag = ulFirstLag; ulLag <= ulLastLag; ulLag++)
    {
        if ((ulLag + 1 < ulBestLag) || (ulLag > ulBestLag + 1))
        {
            fSecondScore = MathHelpers::Max(fSecondScore, afScores[ulLag]);
        }
    }

    float fLag = (float)ulBestLag;
    if (ulBestLag > ulFirstLag && ulBestLag < ulLastLag)
    {
        float fLeft = afScores[ulBestLag - 1];
        float fCentre = afScores[ulBestLag];
        float fRight = afScores[ulBestLag + 1];
        float fDenominator = fLeft - 2.0f * fCentre + fRight;

        if (fabsf(fDenominator) > MATH_EPSILON)
        {
            fLag += MathHelpers::Max(-0.5f, MathHelpers::Min(0.5f, 0.5f * (fLeft - fRight) / fDenominator));
        }
    }

    fBpm = 60.0f / (fLag * TEMPO_SAMPLE_DURATION_SEC);
    fConfidence = MathHelpers::Clamp(fBestScore * 0.70f +
                                     MathHelpers::Max(0.0f, fBestScore - fSecondScore) * 0.30f / fBestScore);
    return (fConfidence >= TEMPO_MIN_CONFIDENCE) ? TRUE : FALSE;
}

// -----------------------------------------------------------------------------

static float ComputeNormalizedAutocorrelation(_In_reads_(ulSampleCount) const float *lpfSamples, _In_ UINT32 ulSampleCount,
                                              _In_ UINT32 ulLag) noexcept
{
    double lfCross = 0.0;
    double lfLeftPower = 0.0;
    double lfRightPower = 0.0;

    if ((!lpfSamples) || ulLag == 0 || ulLag >= ulSampleCount)
    {
        return 0.0f;
    }

    for (UINT32 i = ulLag; i < ulSampleCount; i++)
    {
        float fLeft = lpfSamples[i];
        float fRight = lpfSamples[i - ulLag];

        lfCross += (double)fLeft * (double)fRight;
        lfLeftPower += (double)fLeft * (double)fLeft;
        lfRightPower += (double)fRight * (double)fRight;
    }

    return (float)(lfCross / sqrt(lfLeftPower * lfRightPower + SPECTRAL_EPSILON));
}

// -----------------------------------------------------------------------------

static UINT32 NextPowerOfTwo(_In_ UINT32 ulValue) noexcept
{
    UINT32 ulResult = 1;

    while (ulResult < ulValue)
    {
        ulResult <<= 1;
    }

    return ulResult;
}

static float EnvelopeCoefficient(_In_ float fDurationSec, _In_ float fTimeConstantSec) noexcept
{
    return 1.0f - expf(-fDurationSec / fTimeConstantSec);
}

static float ComputeRangeEnergy(_In_ const std::vector<std::complex<float>> &vecFftWork, _In_ float fSampleRate,
                                _In_ float fStartHz, _In_ float fEndHz) noexcept
{
    float fNyquist;
    double lfPower = 0.0;
    UINT32 ulFirstBin;
    UINT32 ulLastBin;
    UINT32 ulBinCount;
    UINT32 i;

    if (vecFftWork.size() < 2 || fEndHz <= fStartHz)
    {
        return 0.0f;
    }

    fNyquist = fSampleRate * 0.5f;
    if (fStartHz >= fNyquist)
    {
        return 0.0f;
    }
    if (fEndHz > fNyquist)
    {
        fEndHz = fNyquist;
    }

    ulFirstBin = static_cast<UINT32>(ceilf(fStartHz * (float)vecFftWork.size() / fSampleRate));
    ulLastBin = static_cast<UINT32>(floorf(fEndHz * (float)vecFftWork.size() / fSampleRate));
    if (ulLastBin >= vecFftWork.size() / 2)
    {
        ulLastBin = static_cast<UINT32>(vecFftWork.size() / 2);
    }
    if (ulFirstBin > ulLastBin)
    {
        return 0.0f;
    }

    for (i = ulFirstBin; i <= ulLastBin; i++)
    {
        lfPower += (double)std::norm(vecFftWork[i]);
    }
    ulBinCount = ulLastBin - ulFirstBin + 1;
    return sqrtf((float)(lfPower / (double)ulBinCount));
}
