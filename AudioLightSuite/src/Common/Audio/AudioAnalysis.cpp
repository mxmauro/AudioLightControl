#include "AudioAnalysis.h"
#include <cmath>

// -----------------------------------------------------------------------------

static constexpr ULONGLONG MAX_ANALYSIS_HISTORY_AGE_MS = 1000;
static constexpr ULONGLONG ONSET_BASELINE_MIN_AGE_MS = 30;
static constexpr ULONGLONG ONSET_BASELINE_MAX_AGE_MS = 150;
static constexpr float CLIPPING_THRESHOLD = 0.995f;

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
    { CAudioAnalyzer::BandGroupHigh, 9000.0f, 5400.0f }
};

// -----------------------------------------------------------------------------

VOID CAudioAnalyzer::SetFrequency(_In_ float fSampleRate) noexcept
{
    UINT32 i;

    m_fSampleRate = fSampleRate;
    if (m_fSampleRate > MATH_EPSILON)
    {
        for (i = 0; i < BAND_COUNT; i++)
        {
            m_aBands[i] = MathHelpers::CBiQuad::CreateBandPassWithWidth(m_fSampleRate, aBandDefinitions[i].fCenterHz,
                                                                        aBandDefinitions[i].fWidthHz);
        }
    }
    else
    {
        for (i = 0; i < BAND_COUNT; i++)
        {
            m_aBands[i] = MathHelpers::CBiQuad::CBiQuad();
        }
    }

    m_ulNextHistoricalFrame = 0;
    ZeroMemory(m_aHistoricalFrames, sizeof(m_aHistoricalFrames));
}

VOID CAudioAnalyzer::Analyze(_In_reads_(ulSamplesCount) const float *lpfPCM, _In_ UINT32 ulSamplesCount,
                             _Out_ Frame_t &sFrame) noexcept
{
    ULONGLONG ullNow;

    sFrame = {};

    if ((!lpfPCM) || ulSamplesCount == 0 || m_fSampleRate <= MATH_EPSILON)
    {
        return;
    }

    sFrame.lpfPCM = lpfPCM;
    sFrame.ulSamplesCount = ulSamplesCount;
    sFrame.fBlockTimeSec = (float)ulSamplesCount / m_fSampleRate;

    ullNow = GetTickCount64();

    ComputeBandsRMS(sFrame);
    ComputeBlockMetrics(sFrame);
    ComputeGroupedEnergies(sFrame);
    ComputeNormalizedMetrics(sFrame, ullNow);
    ComputeOnsetStrength(sFrame, ullNow);
    AppendHistory(sFrame, ullNow);
}

const CAudioAnalyzer::BandDefinition_t *CAudioAnalyzer::GetBandDefinitions() noexcept
{
    return aBandDefinitions;
}

VOID CAudioAnalyzer::ComputeBandsRMS(_Out_ Frame_t &sFrame) noexcept
{
    double lfBandPower[BAND_COUNT]{};
    UINT32 i, nBand;

    for (i = 0; i < sFrame.ulSamplesCount; i++)
    {
        const float x = sFrame.lpfPCM[i];

        for (nBand = 0; nBand < BAND_COUNT; nBand++)
        {
            const float y = m_aBands[nBand].Process(x);
            lfBandPower[nBand] += (double)y * (double)y;
        }
    }

    const double lfInvCount = 1.0 / (double)sFrame.ulSamplesCount;
    for (i = 0; i < BAND_COUNT; i++)
    {
        sFrame.fBandRmsRaw[i] = sqrtf((float)(lfBandPower[i] * lfInvCount));
    }
}

VOID CAudioAnalyzer::ComputeGroupedEnergies(_Inout_ Frame_t &sFrame) noexcept
{
    sFrame.fLowEnergyRaw = (sFrame.fBandRmsRaw[BAND_LOW_FIRST_INDEX] +
                            sFrame.fBandRmsRaw[BAND_LOW_FIRST_INDEX + 1] +
                            sFrame.fBandRmsRaw[BAND_LOW_FIRST_INDEX + 2] +
                            sFrame.fBandRmsRaw[BAND_LOW_FIRST_INDEX + 3] +
                            sFrame.fBandRmsRaw[BAND_LOW_LAST_INDEX]) / 5.0f;
    sFrame.fMidEnergyRaw = (sFrame.fBandRmsRaw[BAND_MID_FIRST_INDEX] +
                            sFrame.fBandRmsRaw[BAND_MID_FIRST_INDEX + 1] +
                            sFrame.fBandRmsRaw[BAND_MID_FIRST_INDEX + 2] +
                            sFrame.fBandRmsRaw[BAND_MID_FIRST_INDEX + 3] +
                            sFrame.fBandRmsRaw[BAND_MID_LAST_INDEX]) / 5.0f;
    sFrame.fHighEnergyRaw = (sFrame.fBandRmsRaw[BAND_HIGH_FIRST_INDEX] +
                             sFrame.fBandRmsRaw[BAND_HIGH_FIRST_INDEX + 1] +
                             sFrame.fBandRmsRaw[BAND_HIGH_LAST_INDEX]) / 3.0f;
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

VOID CAudioAnalyzer::ComputeNormalizedMetrics(_Inout_ Frame_t &sFrame, _In_ ULONGLONG ullNow) noexcept
{
    float fMaxBandRms[BAND_COUNT];
    float fMaxLowEnergy;
    float fMaxMidEnergy;
    float fMaxHighEnergy;
    UINT32 i;

    for (i = 0; i < BAND_COUNT; i++)
    {
        fMaxBandRms[i] = sFrame.fBandRmsRaw[i];
    }
    fMaxLowEnergy = sFrame.fLowEnergyRaw;
    fMaxMidEnergy = sFrame.fMidEnergyRaw;
    fMaxHighEnergy = sFrame.fHighEnergyRaw;

    for (i = 0; i < MAX_HISTORICAL_ANALYSIS_FRAMES; i++)
    {
        const HistoricalAnalysisFrame_t &sHistoricalFrame = m_aHistoricalFrames[i];
        UINT32 nBand;

        if (sHistoricalFrame.ullTimestamp == 0 || sHistoricalFrame.ullTimestamp + MAX_ANALYSIS_HISTORY_AGE_MS < ullNow)
        {
            continue;
        }

        for (nBand = 0; nBand < BAND_COUNT; nBand++)
        {
            if (sHistoricalFrame.fBandRmsRaw[nBand] > fMaxBandRms[nBand])
            {
                fMaxBandRms[nBand] = sHistoricalFrame.fBandRmsRaw[nBand];
            }
        }

        if (sHistoricalFrame.fLowEnergyRaw > fMaxLowEnergy)
        {
            fMaxLowEnergy = sHistoricalFrame.fLowEnergyRaw;
        }
        if (sHistoricalFrame.fMidEnergyRaw > fMaxMidEnergy)
        {
            fMaxMidEnergy = sHistoricalFrame.fMidEnergyRaw;
        }
        if (sHistoricalFrame.fHighEnergyRaw > fMaxHighEnergy)
        {
            fMaxHighEnergy = sHistoricalFrame.fHighEnergyRaw;
        }
    }

    for (i = 0; i < BAND_COUNT; i++)
    {
        sFrame.fBandRmsNorm[i] = sFrame.fBandRmsRaw[i] / MathHelpers::Max(fMaxBandRms[i], MATH_EPSILON);
    }
    sFrame.fLowEnergyNorm = sFrame.fLowEnergyRaw / MathHelpers::Max(fMaxLowEnergy, MATH_EPSILON);
    sFrame.fMidEnergyNorm = sFrame.fMidEnergyRaw / MathHelpers::Max(fMaxMidEnergy, MATH_EPSILON);
    sFrame.fHighEnergyNorm = sFrame.fHighEnergyRaw / MathHelpers::Max(fMaxHighEnergy, MATH_EPSILON);
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

VOID CAudioAnalyzer::AppendHistory(_In_ const Frame_t &sFrame, _In_ ULONGLONG ullNow) noexcept
{
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
}
