#include "TrebleStrobe.h"
#include "TUI\TUiFrameWindow.h"
#include "Utils\Exception2HR.h"
#include <algorithm>
#include <cmath>
#include <new>

// -----------------------------------------------------------------------------

static BOOL IsPowerOfTwo(_In_ UINT32 ulValue) noexcept;
static UINT32 MillisecondsToSamples(_In_ UINT32 ulMilliseconds, _In_ UINT32 ulSampleRate) noexcept;
static float RangeEnergy(_In_ const std::vector<float> &vecMagnitudes, _In_ UINT32 ulWindowSamples, _In_ UINT32 ulSampleRate,
                         _In_ float fStartHz, _In_ float fEndHz) noexcept;
static float RangeFlux(_In_ const std::vector<float> &vecMagnitudes, _In_ const std::vector<float> &vecPreviousMagnitudes,
                       _In_ BOOL bPreviousValid, _In_ UINT32 ulWindowSamples, _In_ UINT32 ulSampleRate,
                       _In_ float fStartHz, _In_ float fEndHz) noexcept;
static LPCWSTR GetEventReasonText(_In_ CTrebleStrobeAudioProcessor::EventReason_t eReason) noexcept;
static LPCWSTR GetEventReasonColor(_In_ CTrebleStrobeAudioProcessor::EventReason_t eReason) noexcept;

// -----------------------------------------------------------------------------

HRESULT CTrebleStrobeAudioProcessor::Initialize(_In_ const Config &sConfig, _In_ UINT32 ulSampleRate) noexcept
{
    LARGE_INTEGER qpcFrequency;
    HRESULT hRes;

    if (ulSampleRate == 0 || sConfig.ulStftWindowSamples < 128 || IsPowerOfTwo(sConfig.ulStftWindowSamples) == FALSE ||
        sConfig.ulAnalysisHopMs == 0 || sConfig.ulBackgroundDurationMs == 0 || sConfig.ulHistoryDurationMs == 0 ||
        sConfig.fConfidenceThreshold <= 0.0f || sConfig.fConfidenceThreshold > 1.0f || sConfig.fLowBandStartHz < 0.0f ||
        sConfig.fLowBandStartHz >= sConfig.fLowBandEndHz || sConfig.fLowBandEndHz > sConfig.fMidBandStartHz ||
        sConfig.fMidBandStartHz >= sConfig.fMidBandEndHz || sConfig.fMidBandEndHz > sConfig.fHighBandStartHz ||
        sConfig.fHighBandStartHz >= sConfig.fHighBandSplit1Hz || sConfig.fHighBandSplit1Hz >= sConfig.fHighBandSplit2Hz ||
        sConfig.fHighBandSplit2Hz >= sConfig.fHighBandEndHz)
    {
        return E_INVALIDARG;
    }

    hRes = TranslateToHresult([&]()
    {
        const UINT32 ulMagnitudeCount = sConfig.ulStftWindowSamples / 2 + 1;
        const UINT32 ulBackgroundCount = (sConfig.ulBackgroundDurationMs + sConfig.ulAnalysisHopMs - 1) / sConfig.ulAnalysisHopMs + 1;
        const UINT32 ulHistoryCount = (sConfig.ulHistoryDurationMs + sConfig.ulAnalysisHopMs - 1) / sConfig.ulAnalysisHopMs + 1;

        m_vecPcmRing.assign(sConfig.ulStftWindowSamples, 0.0f);
        m_vecHannWindow.resize(sConfig.ulStftWindowSamples);
        m_vecCurrentMagnitudes.assign(ulMagnitudeCount, 0.0f);
        m_vecPreviousMagnitudes.assign(ulMagnitudeCount, 0.0f);
        m_vecBackground.assign(ulBackgroundCount, 0.0f);
        m_vecBackgroundScratch.assign(ulBackgroundCount, 0.0f);
        m_vecFftWork.assign(sConfig.ulStftWindowSamples, std::complex<float>(0.0f, 0.0f));
        m_vecEventHistory.assign(ulHistoryCount, Event_t{});

        for (UINT32 i = 0; i < sConfig.ulStftWindowSamples; i++)
        {
            m_vecHannWindow[i] = 0.5f - 0.5f * cosf(2.0f * MATH_PI * (float)i / (float)(sConfig.ulStftWindowSamples - 1));
        }
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    m_sConfig = sConfig;
    m_ulSampleRate = ulSampleRate;
    m_ulHopSamples = MillisecondsToSamples(sConfig.ulAnalysisHopMs, ulSampleRate);
    m_ulConfirmationLookAheadSamples = MillisecondsToSamples(sConfig.ulConfirmationLookAheadMs, ulSampleRate);
    m_ulCooldownSamples = MillisecondsToSamples(sConfig.ulCooldownMs, ulSampleRate);
    m_ulFlashSamples = MillisecondsToSamples(sConfig.ulFlashDurationMs, ulSampleRate);
    if (m_ulHopSamples == 0)
    {
        return E_INVALIDARG;
    }

    qpcFrequency = {};
    if (::QueryPerformanceFrequency(&qpcFrequency) == FALSE || qpcFrequency.QuadPart <= 0)
    {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    m_ullQpcFrequency = static_cast<UINT64>(qpcFrequency.QuadPart);
    m_ulSamplesSinceHop = 0;
    m_ulRingWriteIndex = 0;
    m_ulRingSamplesAvailable = 0;
    m_ulBackgroundWriteIndex = 0;
    m_ulBackgroundCount = 0;
    m_ulEventWriteIndex = 0;
    m_ulEventCount = 0;
    m_bPreviousMagnitudesValid = FALSE;
    m_bCandidateActive = FALSE;
    m_ullLastAcceptedSample = 0;
    m_ullFlashEndSample = 0;
    m_fLastCandidateSignal = 0.0f;
    m_fLastAdaptiveThreshold = 0.0f;
    m_sCandidate = {};
    m_sLastEvent = {};
    m_sCurrentRGB = {};
    return S_OK;
}

VOID CTrebleStrobeAudioProcessor::FirstRender() noexcept
{
    _ASSERT(m_lpTerminalControl);
    RenderStaticLabels();
    RenderCurrentState();
    RenderOutputView();
}

HRESULT CTrebleStrobeAudioProcessor::OnPostAttachEvent() noexcept
{
    CTUiFrameWindow *lpFrameWindow;

    lpFrameWindow = dynamic_cast<CTUiFrameWindow *>(ParentWindow());
    if (lpFrameWindow)
    {
        return lpFrameWindow->SetCaption(L"Treble Strobe Processor");
    }

    return S_OK;
}

VOID CTrebleStrobeAudioProcessor::Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept
{
    if (!sFrame.lpfPCM || sFrame.ulSamplesCount == 0 || m_ulSampleRate == 0)
    {
        return;
    }

    if (m_ullFlashEndSample != 0 && sFrame.ullFirstSampleIndex >= m_ullFlashEndSample)
    {
        m_sCurrentRGB = {};
        m_ullFlashEndSample = 0;
    }

    for (UINT32 i = 0; i < sFrame.ulSamplesCount; i++)
    {
        const ULONGLONG ullHopEndSample = sFrame.ullFirstSampleIndex + i + 1;

        m_vecPcmRing[m_ulRingWriteIndex] = sFrame.lpfPCM[i];
        m_ulRingWriteIndex = (m_ulRingWriteIndex + 1) % m_sConfig.ulStftWindowSamples;
        if (m_ulRingSamplesAvailable < m_sConfig.ulStftWindowSamples)
        {
            m_ulRingSamplesAvailable++;
        }
        m_ulSamplesSinceHop++;

        if (m_ulRingSamplesAvailable >= m_sConfig.ulStftWindowSamples && m_ulSamplesSinceHop >= m_ulHopSamples)
        {
            Feature_t sFeature;
            float fBackgroundMedian, fBackgroundMad;
            float fAdaptiveThreshold;

            m_ulSamplesSinceHop = 0;
            if (ComputeSpectralHop(ullHopEndSample, sFrame.ullCaptureQpcPosition, sFrame.ullCallbackQpcPosition, sFeature) == FALSE)
            {
                continue;
            }

            if (m_bCandidateActive != FALSE)
            {
                UpdateCandidate(sFeature);
                if (sFeature.ullHopEndSample >= m_sCandidate.ullConfirmAfterSample)
                {
                    ResolveCandidate(sFrame.ullCallbackQpcPosition);
                }
            }

            if (m_bCandidateActive == FALSE)
            {
                GetBackground(fBackgroundMedian, fBackgroundMad);
                fAdaptiveThreshold = MathHelpers::Max(m_sConfig.fMinimumCandidateSignal,
                                                       fBackgroundMedian + m_sConfig.fBackgroundDeviationMultiplier * fBackgroundMad);
                m_fLastCandidateSignal = sFeature.fCandidateSignal;
                m_fLastAdaptiveThreshold = fAdaptiveThreshold;
                if (sFeature.fCandidateSignal >= fAdaptiveThreshold)
                {
                    BeginCandidate(sFeature, fAdaptiveThreshold, sFrame.ullCaptureQpcPosition, sFrame.ullCallbackQpcPosition);
                }
            }

            // Keep the generic gate adapting even while a candidate is under evaluation.
            UpdateBackground(sFeature.fCandidateSignal);
        }
    }

    {
        CRwMutex::CAutoExclusiveLock lock(m_cCurrentStateMutex);

        m_sCurrentState.sLastEvent = m_sLastEvent;
        m_sCurrentState.ulEventCount = m_ulEventCount;
        m_sCurrentState.fLastCandidateSignal = m_fLastCandidateSignal;
        m_sCurrentState.fLastAdaptiveThreshold = m_fLastAdaptiveThreshold;
        m_sCurrentState.sOutputRGB = m_sCurrentRGB;
    }
}

VOID CTrebleStrobeAudioProcessor::TimerTick() noexcept
{
    if (!m_lpTerminalControl)
    {
        return;
    }

    RenderCurrentState();
    RenderOutputView();
}

BOOL CTrebleStrobeAudioProcessor::ComputeSpectralHop(_In_ ULONGLONG ullHopEndSample, _In_ UINT64 ullCaptureQpcPosition,
                                                     _In_ UINT64 ullCallbackQpcPosition, _Out_ Feature_t &sFeature) noexcept
{
    double lfWindowMean = 0.0;
    double lfLogMagnitude = 0.0;
    double lfHighMagnitude = 0.0;
    UINT32 ulHighBinCount = 0;

    UNREFERENCED_PARAMETER(ullCaptureQpcPosition);
    UNREFERENCED_PARAMETER(ullCallbackQpcPosition);

    sFeature = {};
    if (m_vecFftWork.size() != m_sConfig.ulStftWindowSamples || m_vecCurrentMagnitudes.empty())
    {
        return FALSE;
    }

    for (UINT32 i = 0; i < m_sConfig.ulStftWindowSamples; i++)
    {
        lfWindowMean += m_vecPcmRing[i];
    }
    const float fWindowMean = static_cast<float>(lfWindowMean / (double)m_sConfig.ulStftWindowSamples);

    for (UINT32 i = 0; i < m_sConfig.ulStftWindowSamples; i++)
    {
        const UINT32 ulRingIndex = (m_ulRingWriteIndex + i) % m_sConfig.ulStftWindowSamples;
        m_vecFftWork[i] = std::complex<float>((m_vecPcmRing[ulRingIndex] - fWindowMean) * m_vecHannWindow[i], 0.0f);
    }

    for (UINT32 i = 1; i < m_sConfig.ulStftWindowSamples; i++)
    {
        UINT32 ulBit = m_sConfig.ulStftWindowSamples >> 1;
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

    for (UINT32 ulLength = 2; ulLength <= m_sConfig.ulStftWindowSamples; ulLength <<= 1)
    {
        const float fAngle = -2.0f * MATH_PI / static_cast<float>(ulLength);
        const std::complex<float> cStep(cosf(fAngle), sinf(fAngle));

        for (UINT32 ulOffset = 0; ulOffset < m_sConfig.ulStftWindowSamples; ulOffset += ulLength)
        {
            std::complex<float> cTwiddle(1.0f, 0.0f);
            const UINT32 ulHalfLength = ulLength / 2;

            for (UINT32 ulIndex = 0; ulIndex < ulHalfLength; ulIndex++)
            {
                const std::complex<float> cEven = m_vecFftWork[ulOffset + ulIndex];
                const std::complex<float> cOdd = cTwiddle * m_vecFftWork[ulOffset + ulIndex + ulHalfLength];

                m_vecFftWork[ulOffset + ulIndex] = cEven + cOdd;
                m_vecFftWork[ulOffset + ulIndex + ulHalfLength] = cEven - cOdd;
                cTwiddle *= cStep;
            }
        }
    }

    for (UINT32 i = 0; i < m_vecCurrentMagnitudes.size(); i++)
    {
        const float fFrequency = static_cast<float>(i) * static_cast<float>(m_ulSampleRate) /
                                 static_cast<float>(m_sConfig.ulStftWindowSamples);
        const float fMagnitude = std::abs(m_vecFftWork[i]) / static_cast<float>(m_sConfig.ulStftWindowSamples);

        m_vecCurrentMagnitudes[i] = fMagnitude;
        if (fFrequency >= m_sConfig.fHighBandStartHz && fFrequency <= m_sConfig.fHighBandEndHz)
        {
            lfLogMagnitude += logf(fMagnitude + MATH_EPSILON);
            lfHighMagnitude += fMagnitude;
            ulHighBinCount++;
        }
    }

    sFeature.ullHopEndSample = ullHopEndSample;
    sFeature.ullHopStartSample = ullHopEndSample - m_ulHopSamples;
    sFeature.fLowEnergy = RangeEnergy(m_vecCurrentMagnitudes, m_sConfig.ulStftWindowSamples, m_ulSampleRate,
                                      m_sConfig.fLowBandStartHz, m_sConfig.fLowBandEndHz);
    sFeature.fMidEnergy = RangeEnergy(m_vecCurrentMagnitudes, m_sConfig.ulStftWindowSamples, m_ulSampleRate,
                                      m_sConfig.fMidBandStartHz, m_sConfig.fMidBandEndHz);
    sFeature.fHighEnergy = RangeEnergy(m_vecCurrentMagnitudes, m_sConfig.ulStftWindowSamples, m_ulSampleRate,
                                       m_sConfig.fHighBandStartHz, m_sConfig.fHighBandEndHz);
    sFeature.fLowFlux = RangeFlux(m_vecCurrentMagnitudes, m_vecPreviousMagnitudes, m_bPreviousMagnitudesValid,
                                  m_sConfig.ulStftWindowSamples, m_ulSampleRate, m_sConfig.fLowBandStartHz, m_sConfig.fLowBandEndHz);
    sFeature.fMidFlux = RangeFlux(m_vecCurrentMagnitudes, m_vecPreviousMagnitudes, m_bPreviousMagnitudesValid,
                                  m_sConfig.ulStftWindowSamples, m_ulSampleRate, m_sConfig.fMidBandStartHz, m_sConfig.fMidBandEndHz);
    sFeature.fHighFlux = RangeFlux(m_vecCurrentMagnitudes, m_vecPreviousMagnitudes, m_bPreviousMagnitudesValid,
                                   m_sConfig.ulStftWindowSamples, m_ulSampleRate, m_sConfig.fHighBandStartHz, m_sConfig.fHighBandEndHz);
    sFeature.fHighFluxLower = RangeFlux(m_vecCurrentMagnitudes, m_vecPreviousMagnitudes, m_bPreviousMagnitudesValid,
                                        m_sConfig.ulStftWindowSamples, m_ulSampleRate, m_sConfig.fHighBandStartHz,
                                        m_sConfig.fHighBandSplit1Hz);
    sFeature.fHighFluxMiddle = RangeFlux(m_vecCurrentMagnitudes, m_vecPreviousMagnitudes, m_bPreviousMagnitudesValid,
                                         m_sConfig.ulStftWindowSamples, m_ulSampleRate, m_sConfig.fHighBandSplit1Hz,
                                         m_sConfig.fHighBandSplit2Hz);
    sFeature.fHighFluxUpper = RangeFlux(m_vecCurrentMagnitudes, m_vecPreviousMagnitudes, m_bPreviousMagnitudesValid,
                                        m_sConfig.ulStftWindowSamples, m_ulSampleRate, m_sConfig.fHighBandSplit2Hz,
                                        m_sConfig.fHighBandEndHz);
    sFeature.ulHighBroadbandCoverage = (sFeature.fHighFluxLower >= 0.10f ? 1U : 0U) +
                                        (sFeature.fHighFluxMiddle >= 0.10f ? 1U : 0U) +
                                        (sFeature.fHighFluxUpper >= 0.10f ? 1U : 0U);
    sFeature.fHighFlatness = (ulHighBinCount > 0) ? MathHelpers::Clamp(expf(static_cast<float>(lfLogMagnitude / ulHighBinCount)) /
                                                                           (static_cast<float>(lfHighMagnitude / ulHighBinCount) +
                                                                           MATH_EPSILON))
                                                   : 0.0f;
    sFeature.fHighToMidRatio = MathHelpers::Clamp(sFeature.fHighEnergy / (sFeature.fMidEnergy + MATH_EPSILON));
    sFeature.fCandidateSignal = sFeature.fHighFlux * sqrtf(sFeature.fHighEnergy);

    memcpy(m_vecPreviousMagnitudes.data(), m_vecCurrentMagnitudes.data(), m_vecCurrentMagnitudes.size() * sizeof(float));
    m_bPreviousMagnitudesValid = TRUE;
    return TRUE;
}

VOID CTrebleStrobeAudioProcessor::UpdateBackground(_In_ float fCandidateSignal) noexcept
{
    if (m_vecBackground.empty())
    {
        return;
    }

    m_vecBackground[m_ulBackgroundWriteIndex] = fCandidateSignal;
    m_ulBackgroundWriteIndex = (m_ulBackgroundWriteIndex + 1) % static_cast<UINT32>(m_vecBackground.size());
    if (m_ulBackgroundCount < m_vecBackground.size())
    {
        m_ulBackgroundCount++;
    }
}

VOID CTrebleStrobeAudioProcessor::GetBackground(_Out_ float &fMedian, _Out_ float &fMad) noexcept
{
    fMedian = 0.0f;
    fMad = 0.0f;
    if (m_ulBackgroundCount == 0)
    {
        return;
    }

    memcpy(m_vecBackgroundScratch.data(), m_vecBackground.data(), m_ulBackgroundCount * sizeof(float));
    float *lpfMedian = m_vecBackgroundScratch.data() + m_ulBackgroundCount / 2;
    std::nth_element(m_vecBackgroundScratch.data(), lpfMedian, m_vecBackgroundScratch.data() + m_ulBackgroundCount);
    fMedian = *lpfMedian;

    for (UINT32 i = 0; i < m_ulBackgroundCount; i++)
    {
        m_vecBackgroundScratch[i] = fabsf(m_vecBackground[i] - fMedian);
    }
    std::nth_element(m_vecBackgroundScratch.data(), lpfMedian, m_vecBackgroundScratch.data() + m_ulBackgroundCount);
    fMad = MathHelpers::Max(*lpfMedian, MATH_EPSILON);
}

VOID CTrebleStrobeAudioProcessor::BeginCandidate(_In_ const Feature_t &sFeature, _In_ float fCandidateThreshold,
                                                 _In_ UINT64 ullCaptureQpcPosition, _In_ UINT64 ullCallbackQpcPosition) noexcept
{
    m_sCandidate = {};
    m_sCandidate.sFeature = sFeature;
    m_sCandidate.ullCaptureQpcPosition = ullCaptureQpcPosition;
    m_sCandidate.ullCallbackQpcPosition = ullCallbackQpcPosition;
    m_sCandidate.ullConfirmAfterSample = sFeature.ullHopEndSample + m_ulConfirmationLookAheadSamples;
    m_sCandidate.fCandidateThreshold = fCandidateThreshold;
    m_sCandidate.fPeakSignal = sFeature.fCandidateSignal;
    m_sCandidate.fPeakHighEnergy = sFeature.fHighEnergy;
    m_bCandidateActive = TRUE;
}

VOID CTrebleStrobeAudioProcessor::UpdateCandidate(_In_ const Feature_t &sFeature) noexcept
{
    if (sFeature.fCandidateSignal > m_sCandidate.fPeakSignal)
    {
        m_sCandidate.fPeakSignal = sFeature.fCandidateSignal;
        m_sCandidate.sFeature = sFeature;
    }
    m_sCandidate.fPeakHighEnergy = MathHelpers::Max(m_sCandidate.fPeakHighEnergy, sFeature.fHighEnergy);
    m_sCandidate.fPostHighEnergy += sFeature.fHighEnergy;
    m_sCandidate.fPostMidEnergy += sFeature.fMidEnergy;
    m_sCandidate.ulPostFeatureCount++;
}

VOID CTrebleStrobeAudioProcessor::ResolveCandidate(_In_ UINT64 ullCallbackQpcPosition) noexcept
{
    const Feature_t &sFeature = m_sCandidate.sFeature;
    const float fPostHighEnergy = (m_sCandidate.ulPostFeatureCount > 0) ?
                                  m_sCandidate.fPostHighEnergy / static_cast<float>(m_sCandidate.ulPostFeatureCount) : 0.0f;
    const float fPostMidEnergy = (m_sCandidate.ulPostFeatureCount > 0) ?
                                 m_sCandidate.fPostMidEnergy / static_cast<float>(m_sCandidate.ulPostFeatureCount) : 0.0f;
    const float fAttack = MathHelpers::Clamp((m_sCandidate.fPeakSignal - m_sCandidate.fCandidateThreshold) /
                                             MathHelpers::Max(m_sCandidate.fCandidateThreshold, MATH_EPSILON));
    const float fHighRatio = MathHelpers::Clamp((sFeature.fHighToMidRatio - 0.40f) / 1.60f);
    const float fSustain = MathHelpers::Clamp(fPostHighEnergy / (m_sCandidate.fPeakHighEnergy + MATH_EPSILON));
    const float fMidDominance = MathHelpers::Clamp(fPostMidEnergy / (fPostHighEnergy + MATH_EPSILON));
    const float fBroadbandCoverage = static_cast<float>(sFeature.ulHighBroadbandCoverage) / 3.0f;
    LARGE_INTEGER qpcNow;
    Event_t sEvent;
    float fCombinedConfidence;
    float fPenalty;

    ::QueryPerformanceCounter(&qpcNow);
    sEvent.ullSampleIndex = sFeature.ullHopStartSample;
    sEvent.ullCaptureQpcPosition = m_sCandidate.ullCaptureQpcPosition;
    sEvent.ullEmissionQpcPosition = static_cast<UINT64>(qpcNow.QuadPart);
    sEvent.fHiHatConfidence = MathHelpers::Clamp(0.65f * fAttack + 0.15f * sFeature.fHighFlatness +
                                                 0.10f * fHighRatio + 0.10f * fBroadbandCoverage);
    sEvent.fCymbalConfidence = MathHelpers::Clamp(0.55f * fAttack + 0.20f * sFeature.fHighFlatness +
                                                  0.10f * fSustain + 0.10f * fBroadbandCoverage + 0.05f * fHighRatio);
    sEvent.fVocalEvidence = MathHelpers::Clamp(0.45f * (1.0f - fAttack) * fMidDominance +
                                               0.35f * (1.0f - sFeature.fHighFlux) + 0.20f * (1.0f - fHighRatio));
    sEvent.fSnareEvidence = MathHelpers::Clamp(0.50f * sFeature.fLowFlux + 0.40f * sFeature.fMidFlux +
                                               0.10f * (1.0f - fHighRatio));
    sEvent.fGuitarEvidence = MathHelpers::Clamp(0.55f * (1.0f - sFeature.fHighFlatness) * (1.0f - fAttack) +
                                                0.30f * fMidDominance + 0.15f * (1.0f - sFeature.fHighFlux));
    sEvent.fHighFlatness = sFeature.fHighFlatness;
    sEvent.fHighToMidRatio = sFeature.fHighToMidRatio;

    fCombinedConfidence = MathHelpers::Max(sEvent.fHiHatConfidence, sEvent.fCymbalConfidence);
    fPenalty = 0.12f * sEvent.fVocalEvidence + 0.10f * sEvent.fGuitarEvidence;
    fPenalty += (fCombinedConfidence >= 0.90f) ? 0.05f * sEvent.fSnareEvidence : 0.12f * sEvent.fSnareEvidence;
    sEvent.fFlashConfidence = MathHelpers::Clamp(fCombinedConfidence - fPenalty + GetHistoryReinforcement(sFeature));
    sEvent.fLookAheadMs = static_cast<float>(m_ulConfirmationLookAheadSamples) * 1000.0f / static_cast<float>(m_ulSampleRate);
    if (m_ullQpcFrequency != 0 && sEvent.ullEmissionQpcPosition >= ullCallbackQpcPosition)
    {
        sEvent.fComputationMs = static_cast<float>((sEvent.ullEmissionQpcPosition - ullCallbackQpcPosition) * 1000.0 /
                                                    static_cast<double>(m_ullQpcFrequency));
    }
    if (m_ullQpcFrequency != 0 && ullCallbackQpcPosition >= sEvent.ullCaptureQpcPosition && sEvent.ullCaptureQpcPosition != 0)
    {
        sEvent.fCaptureBlockingMs = static_cast<float>((ullCallbackQpcPosition - sEvent.ullCaptureQpcPosition) * 1000.0 /
                                                        static_cast<double>(m_ullQpcFrequency));
    }

    if (m_ullLastAcceptedSample != 0 && sEvent.ullSampleIndex - m_ullLastAcceptedSample < m_ulCooldownSamples)
    {
        sEvent.eReason = EventReason_t::Cooldown;
    }
    else if (sEvent.fFlashConfidence < m_sConfig.fConfidenceThreshold)
    {
        sEvent.eReason = EventReason_t::BelowThreshold;
    }
    else
    {
        sEvent.eReason = EventReason_t::Accepted;
        sEvent.bAccepted = TRUE;
        m_ullLastAcceptedSample = sEvent.ullSampleIndex;
        m_ullFlashEndSample = sFeature.ullHopEndSample + m_ulFlashSamples;
        m_sCurrentRGB.r = 255;
        m_sCurrentRGB.g = 255;
        m_sCurrentRGB.b = 255;
    }

    AppendEvent(sEvent);
    m_sLastEvent = sEvent;
    m_sCandidate = {};
    m_bCandidateActive = FALSE;
}

VOID CTrebleStrobeAudioProcessor::AppendEvent(_In_ const Event_t &sEvent) noexcept
{
    if (m_vecEventHistory.empty())
    {
        return;
    }

    m_vecEventHistory[m_ulEventWriteIndex] = sEvent;
    m_ulEventWriteIndex = (m_ulEventWriteIndex + 1) % static_cast<UINT32>(m_vecEventHistory.size());
    if (m_ulEventCount < m_vecEventHistory.size())
    {
        m_ulEventCount++;
    }
}

float CTrebleStrobeAudioProcessor::GetHistoryReinforcement(_In_ const Feature_t &sFeature) const noexcept
{
    UINT32 ulSimilarAcceptedCount = 0;

    for (UINT32 i = 0; i < m_ulEventCount; i++)
    {
        const Event_t &sEvent = m_vecEventHistory[i];

        if (sEvent.bAccepted == FALSE || sFeature.ullHopStartSample < sEvent.ullSampleIndex ||
            sFeature.ullHopStartSample - sEvent.ullSampleIndex > static_cast<ULONGLONG>(m_ulSampleRate) * 2000 / 1000)
        {
            continue;
        }
        if (fabsf(sFeature.fHighFlatness - sEvent.fHighFlatness) <= 0.20f &&
            fabsf(sFeature.fHighToMidRatio - sEvent.fHighToMidRatio) <= 0.30f)
        {
            ulSimilarAcceptedCount++;
        }
    }

    return (ulSimilarAcceptedCount >= 2) ? 0.05f : 0.0f;
}

VOID CTrebleStrobeAudioProcessor::RenderStaticLabels() noexcept
{
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;1H\x1b[97mEvent:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(),
                                     L"\x1b[2;1H\x1b[97mHat:\x1b[0m\x1b[2;14H\x1b[97mCymbal:\x1b[0m\x1b[2;28H\x1b[97mFlash:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(),
                                     L"\x1b[3;1H\x1b[97mVoice:\x1b[0m\x1b[3;14H\x1b[97mSnare:\x1b[0m\x1b[3;28H\x1b[97mGuitar:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[4;1H\x1b[97mFlat:\x1b[0m\x1b[4;14H\x1b[97mH/M:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[5;1H\x1b[97mGate:\x1b[0m\x1b[5;17H\x1b[97mSignal:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;1H\x1b[97mLatency:\x1b[0m\x1b[6;19H\x1b[97mLook:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;1H\x1b[97mCalc:\x1b[0m");
}

VOID CTrebleStrobeAudioProcessor::RenderCurrentState() noexcept
{
    CurrentState_t sCurrentState;

    {
        CRwMutex::CAutoSharedLock lock(m_cCurrentStateMutex);

        sCurrentState = m_sCurrentState;
    }

    const Event_t &sEvent = sCurrentState.sLastEvent;

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;8H%-10llu  %ls%ls\x1b[0m",
                                     sEvent.ullSampleIndex, GetEventReasonColor(sEvent.eReason), GetEventReasonText(sEvent.eReason));

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;8H%4.2f\x1b[2;22H%4.2f\x1b[2;36H%4.2f",
                                     sEvent.fHiHatConfidence, sEvent.fCymbalConfidence, sEvent.fFlashConfidence);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;8H%4.2f\x1b[3;22H%4.2f\x1b[3;36H%4.2f",
                                     sEvent.fVocalEvidence, sEvent.fSnareEvidence, sEvent.fGuitarEvidence);

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[4;8H%4.2f\x1b[4;22H%4.2f",
                                     sEvent.fHighFlatness, sEvent.fHighToMidRatio);

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[5;7H%8.5f\x1b[5;25H%8.5f",
                                     sCurrentState.fLastAdaptiveThreshold, sCurrentState.fLastCandidateSignal);

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;10H%5.2fms\x1b[6;25H%5.2fms",
                                     sEvent.fCaptureBlockingMs, sEvent.fLookAheadMs);

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;7H%5.2fms", sEvent.fComputationMs);
}

// -----------------------------------------------------------------------------

static BOOL IsPowerOfTwo(_In_ UINT32 ulValue) noexcept
{
    return (ulValue != 0 && (ulValue & (ulValue - 1)) == 0) ? TRUE : FALSE;
}

static UINT32 MillisecondsToSamples(_In_ UINT32 ulMilliseconds, _In_ UINT32 ulSampleRate) noexcept
{
    const ULONGLONG ullSamples = (static_cast<ULONGLONG>(ulMilliseconds) * ulSampleRate + 500) / 1000;

    return (ullSamples > MAXUINT) ? MAXUINT : static_cast<UINT32>(ullSamples);
}

static float RangeEnergy(_In_ const std::vector<float> &vecMagnitudes, _In_ UINT32 ulWindowSamples, _In_ UINT32 ulSampleRate,
                         _In_ float fStartHz, _In_ float fEndHz) noexcept
{
    double lfEnergy = 0.0;
    UINT32 ulCount = 0;

    for (UINT32 i = 1; i < vecMagnitudes.size(); i++)
    {
        const float fFrequency = static_cast<float>(i) * static_cast<float>(ulSampleRate) / static_cast<float>(ulWindowSamples);

        if (fFrequency >= fStartHz && fFrequency < fEndHz)
        {
            lfEnergy += static_cast<double>(vecMagnitudes[i]) * vecMagnitudes[i];
            ulCount++;
        }
    }
    return (ulCount > 0) ? sqrtf(static_cast<float>(lfEnergy / ulCount)) : 0.0f;
}

static float RangeFlux(_In_ const std::vector<float> &vecMagnitudes, _In_ const std::vector<float> &vecPreviousMagnitudes,
                       _In_ BOOL bPreviousValid, _In_ UINT32 ulWindowSamples, _In_ UINT32 ulSampleRate,
                       _In_ float fStartHz, _In_ float fEndHz) noexcept
{
    double lfPositiveDifference = 0.0;
    double lfMagnitude = 0.0;

    if (bPreviousValid == FALSE)
    {
        return 0.0f;
    }

    for (UINT32 i = 1; i < vecMagnitudes.size(); i++)
    {
        const float fFrequency = static_cast<float>(i) * static_cast<float>(ulSampleRate) / static_cast<float>(ulWindowSamples);

        if (fFrequency >= fStartHz && fFrequency < fEndHz)
        {
            lfPositiveDifference += MathHelpers::Max(0.0f, vecMagnitudes[i] - vecPreviousMagnitudes[i]);
            lfMagnitude += vecMagnitudes[i];
        }
    }
    return MathHelpers::Clamp(static_cast<float>(lfPositiveDifference / (lfMagnitude + MATH_EPSILON)));
}

static LPCWSTR GetEventReasonText(_In_ CTrebleStrobeAudioProcessor::EventReason_t eReason) noexcept
{
    switch (eReason)
    {
        case CTrebleStrobeAudioProcessor::EventReason_t::BelowThreshold:
            return L"Reject  ";

        case CTrebleStrobeAudioProcessor::EventReason_t::Cooldown:
            return L"Cooldown";

        case CTrebleStrobeAudioProcessor::EventReason_t::Accepted:
            return L"Flash   ";

        default:
            return L"Waiting ";
    }
}

static LPCWSTR GetEventReasonColor(_In_ CTrebleStrobeAudioProcessor::EventReason_t eReason) noexcept
{
    switch (eReason)
    {
        case CTrebleStrobeAudioProcessor::EventReason_t::BelowThreshold:
            return L"\x1b[91m";

        case CTrebleStrobeAudioProcessor::EventReason_t::Cooldown:
            return L"\x1b[93m";

        case CTrebleStrobeAudioProcessor::EventReason_t::Accepted:
            return L"\x1b[96m";

        default:
            return L"\x1b[90m";
    }
}
