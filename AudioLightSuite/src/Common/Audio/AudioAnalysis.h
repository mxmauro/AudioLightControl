#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Utils\MathHelpers.h"
#include <complex>
#include <vector>

// -----------------------------------------------------------------------------

class CAudioAnalyzer
{
public:
    static constexpr UINT32 BAND_COUNT = 14;
    static constexpr UINT32 SPECTRAL_MEDIAN_HISTORY_COUNT = 5;
    static constexpr UINT32 TEMPO_HISTORY_SAMPLE_COUNT = 600;

    static constexpr UINT32 BAND_SUB_KICK_THUMP_INDEX = 0;
    static constexpr UINT32 BAND_KICK_BASS_INDEX = 1;
    static constexpr UINT32 BAND_BASS_NOTE_BODY_INDEX = 2;
    static constexpr UINT32 BAND_BASS_WARMTH_INDEX = 3;
    static constexpr UINT32 BAND_LOW_MID_PUNCH_INDEX = 4;
    static constexpr UINT32 BAND_MID_BODY_INDEX = 5;
    static constexpr UINT32 BAND_BODY_GROWL_INDEX = 6;
    static constexpr UINT32 BAND_EDGE_INDEX = 7;
    static constexpr UINT32 BAND_ATTACK_DEFINITION_INDEX = 8;
    static constexpr UINT32 BAND_PRESENCE_INDEX = 9;
    static constexpr UINT32 BAND_SNAP_STICK_INDEX = 10;
    static constexpr UINT32 BAND_CYMBAL_ONSET_INDEX = 11;
    static constexpr UINT32 BAND_HI_HATS_SPARKLE_AIR_INDEX = 12;
    static constexpr UINT32 BAND_TIGHT_TREBLE_INDEX = 13;

    typedef enum BandGroup_e {
        BandGroupLow = 0,
        BandGroupMid = 1,
        BandGroupHigh = 2
    } BandGroup_t;

    typedef struct BandDefinition_s {
        BandGroup_t eGroup;
        float fCenterHz;
        float fWidthHz;
    } BandDefinition_t;

    typedef struct Frame_s {
        const float *lpfPCM;
        UINT32 ulSamplesCount;
        ULONGLONG ullFirstSampleIndex;
        ULONGLONG ullEndSampleIndex;
        UINT64 ullCaptureDevicePosition;
        UINT64 ullCaptureQpcPosition;
        UINT64 ullCallbackQpcPosition;

        float fBlockTimeSec;
        float fPeak;
        float fBlockRms;

        float fBandRmsRaw[BAND_COUNT];
        float fBandRmsNorm[BAND_COUNT];

        float fLowEnergyRaw;
        float fMidEnergyRaw;
        float fHighEnergyRaw;

        float fLowEnergyNorm;
        float fMidEnergyNorm;
        float fHighEnergyNorm;

        float fOnsetStrength;

        BOOL bBpmReady;
        float fBpm;
        float fBpmConfidence;

        BOOL bSpectralReady;
        float fSpectralFlux;
        float fSpectralFlux5To8;
        float fSpectralFlux8To12;
        float fSpectralFlux12To18;
        UINT32 ulSpectralBroadbandCoverage;
        float fSpectralEnergy5To8;
        float fSpectralEnergy8To12;
        float fSpectralEnergyUpper;
        float fSpectralFlatness;
        float fSpectralPercussiveRatio;
        float fHighFastEnvelope;
        float fHighSlowEnvelope;
        float fHighTransientStrength;
        float fSpectralAirRatio;
        float fSpectralHighToMidRatio;
        float fTrebleScore;
        // Valid only while synchronous Frame_t consumers are processing this frame.
        // Windowed absolute FFT magnitudes; GetFftMagnitudeFullScale() provides the 0 dBFS reference.
        const float *lpfFftMagnitudeRaw;
        // Per-frame, peak-relative display values retained for consumers that need contrast normalization.
        const float *lpfFftMagnitudeNorm;
        UINT32 ulFftMagnitudeCount;

        BOOL bClippingDetected;
    } Frame_t;

private:
    typedef struct HistoricalAnalysisFrame_s {
        ULONGLONG ullTimestamp;
        float fBandRmsRaw[BAND_COUNT];
        float fLowEnergyRaw;
        float fMidEnergyRaw;
        float fHighEnergyRaw;
        float fBlockRms;
    } HistoricalAnalysisFrame_t;

    typedef struct SpectralMetrics_s {
        float fFlux{ 0.0f };
        float fFlux5To8{ 0.0f };
        float fFlux8To12{ 0.0f };
        float fFlux12To18{ 0.0f };
        UINT32 ulBroadbandCoverage{ 0 };
        float fEnergy5To8{ 0.0f };
        float fEnergy8To12{ 0.0f };
        float fEnergyUpper{ 0.0f };
        float fVoiceMidEnergy{ 0.0f };
        float fFlatness{ 0.0f };
        float fPercussiveRatio{ 0.0f };
        float fFastEnvelope{ 0.0f };
        float fSlowEnvelope{ 0.0f };
        float fTransientStrength{ 0.0f };
    } SpectralMetrics_t;

public:
    CAudioAnalyzer() noexcept = default;
    CAudioAnalyzer(_In_ const CAudioAnalyzer &) = default;
    CAudioAnalyzer(_Inout_ CAudioAnalyzer &&) = default;
    ~CAudioAnalyzer() = default;

    CAudioAnalyzer &operator=(_In_ const CAudioAnalyzer &) = default;
    CAudioAnalyzer &operator=(_Inout_ CAudioAnalyzer &&) = default;

    HRESULT Initialize(_In_ float fSampleRate) noexcept;

    VOID Analyze(_In_reads_(ulSamplesCount) const float *lpfPCM, _In_ UINT32 ulSamplesCount, _Out_ Frame_t &sFrame) noexcept;

    static const BandDefinition_t *GetBandDefinitions() noexcept;

    UINT32 GetFftMagnitudeCount() const noexcept
    {
        return static_cast<UINT32>(m_vecFftMagnitudeNorm.size());
    }

    float GetFftMagnitudeFullScale() const noexcept
    {
        // A bin-centred 0 dBFS sine through the Hann window has this one-sided FFT magnitude.
        return (m_ulSpectralWindowSize > 1) ? static_cast<float>(m_ulSpectralWindowSize - 1) * 0.25f : 0.0f;
    }

    float GetSpectralHopDurationSec() const noexcept
    {
        return (m_fSampleRate > MATH_EPSILON) ? static_cast<float>(m_ulSpectralHopSize) / m_fSampleRate : 0.0f;
    }

private:
    VOID Reset() noexcept;

    VOID ComputeBandsRMS(_Inout_ Frame_t &sFrame) noexcept;
    VOID ComputeBlockMetrics(_Inout_ Frame_t &sFrame) noexcept;
    VOID ComputeGroupedEnergies(_Inout_ Frame_t &sFrame) noexcept;

    UINT32 AppendHistory(_In_ const Frame_t &sFrame, _In_ ULONGLONG ullNow) noexcept;

    VOID NormalizeMetrics(_Inout_ Frame_t &sFrame, _In_ UINT32 ulHistoricalFrameInsertIndex, _In_ ULONGLONG ullNow) noexcept;

    VOID ComputeOnsetStrength(_Inout_ Frame_t &sFrame, _In_ ULONGLONG ullNow) noexcept;
    VOID ComputeSpectralMetrics(_Inout_ Frame_t &sFrame) noexcept;
    BOOL ComputeSpectralHop(_Out_ SpectralMetrics_t &sMetrics) noexcept;
    VOID UpdateTempo(_Inout_ Frame_t &sFrame) noexcept;
    BOOL EstimateTempo(_Out_ float &fBpm, _Out_ float &fConfidence) const noexcept;

private:
    static constexpr UINT32 MAX_HISTORICAL_ANALYSIS_FRAMES = 256;

    MathHelpers::CBiQuad m_aBands[BAND_COUNT]{};
    float m_fSampleRate{ 0.0f };
    ULONGLONG m_ullNextSampleIndex{ 0 };
    HistoricalAnalysisFrame_t m_aHistoricalFrames[MAX_HISTORICAL_ANALYSIS_FRAMES]{};
    UINT32 m_ulNextHistoricalFrame{ 0 };

    UINT32 m_ulSpectralWindowSize{ 0 };
    UINT32 m_ulSpectralHopSize{ 0 };
    UINT32 m_ulSpectralWriteIndex{ 0 };
    UINT32 m_ulSpectralSamplesAvailable{ 0 };
    UINT32 m_ulSamplesSinceSpectralHop{ 0 };
    BOOL m_bPreviousMagnitudesValid{ FALSE };
    float m_fHighFastEnvelope{ 0.0f };
    float m_fHighSlowEnvelope{ 0.0f };
    float m_afTempoHistory[TEMPO_HISTORY_SAMPLE_COUNT]{};
    float m_fTempoAccumulatedSignal{ 0.0f };
    float m_fTempoAccumulatedDurationSec{ 0.0f };
    float m_fPreviousLowEnergy{ 0.0f };
    float m_fBpm{ 0.0f };
    float m_fBpmConfidence{ 0.0f };
    UINT32 m_ulNextTempoHistorySample{ 0 };
    UINT32 m_ulTempoHistorySampleCount{ 0 };
    UINT32 m_ulTempoSamplesSinceEstimate{ 0 };
    std::vector<float> m_vecSpectralRing;
    std::vector<float> m_vecHannWindow;
    std::vector<float> m_vecCurrentMagnitudes;
    std::vector<float> m_vecFftMagnitudeNorm;
    std::vector<float> m_vecPreviousMagnitudes;
    std::vector<float> m_vecSpectralMagnitudeHistory;
    UINT32 m_ulSpectralMagnitudeHistoryCount{ 0 };
    UINT32 m_ulNextSpectralMagnitudeHistory{ 0 };
    std::vector<std::complex<float>> m_vecFftWork;
};
