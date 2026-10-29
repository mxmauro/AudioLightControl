#pragma once

#include "BaseAudioProcessor.h"
#include "Utils\RwMutex.h"
#include <complex>
#include <vector>

// -----------------------------------------------------------------------------

class CTrebleStrobeAudioProcessor : public CBaseAudioProcessor
{
public:
    typedef struct RGB_s {
        BYTE r, g, b;
    } RGB;

    typedef struct Config_s {
        UINT32 ulStftWindowSamples{ 1024 };
        UINT32 ulAnalysisHopMs{ 10 };
        UINT32 ulBackgroundDurationMs{ 3000 };
        UINT32 ulConfirmationLookAheadMs{ 20 };
        UINT32 ulHistoryDurationMs{ 24000 };
        UINT32 ulCooldownMs{ 70 };
        UINT32 ulFlashDurationMs{ 30 };
        float fConfidenceThreshold{ 0.45f };
        float fMinimumCandidateSignal{ 0.0005f };
        float fBackgroundDeviationMultiplier{ 2.5f };
        float fLowBandStartHz{ 150.0f };
        float fLowBandEndHz{ 2000.0f };
        float fMidBandStartHz{ 2000.0f };
        float fMidBandEndHz{ 5000.0f };
        float fHighBandStartHz{ 5000.0f };
        float fHighBandSplit1Hz{ 8000.0f };
        float fHighBandSplit2Hz{ 12000.0f };
        float fHighBandEndHz{ 18000.0f };
    } Config;

    typedef enum class EventReason_e {
        None = 0,
        BelowThreshold,
        Cooldown,
        Accepted
    } EventReason_t;

    typedef struct Event_s {
        ULONGLONG ullSampleIndex{ 0 };
        UINT64 ullCaptureQpcPosition{ 0 };
        UINT64 ullEmissionQpcPosition{ 0 };
        float fHiHatConfidence{ 0.0f };
        float fCymbalConfidence{ 0.0f };
        float fFlashConfidence{ 0.0f };
        float fVocalEvidence{ 0.0f };
        float fSnareEvidence{ 0.0f };
        float fGuitarEvidence{ 0.0f };
        float fHighFlatness{ 0.0f };
        float fHighToMidRatio{ 0.0f };
        float fCaptureBlockingMs{ 0.0f };
        float fLookAheadMs{ 0.0f };
        float fComputationMs{ 0.0f };
        EventReason_t eReason{ EventReason_t::None };
        BOOL bAccepted{ FALSE };
    } Event_t;

public:
    CTrebleStrobeAudioProcessor() noexcept = default;
    CTrebleStrobeAudioProcessor(_In_ const CTrebleStrobeAudioProcessor &) = delete;
    CTrebleStrobeAudioProcessor(_Inout_ CTrebleStrobeAudioProcessor &&) = delete;
    ~CTrebleStrobeAudioProcessor() noexcept = default;

    CTrebleStrobeAudioProcessor &operator=(_In_ const CTrebleStrobeAudioProcessor &) = delete;
    CTrebleStrobeAudioProcessor &operator=(_Inout_ CTrebleStrobeAudioProcessor &&) = delete;

    HRESULT Initialize(_In_ const Config &sConfig, _In_ UINT32 ulSampleRate) noexcept;

    LPCSTR GetType() const noexcept final
    {
        return "TrebleStrobe";
    }

    VOID FirstRender() noexcept final;
    VOID Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept final;
    VOID TimerTick() noexcept final;

    BOOL HasWhiteColor() const noexcept final
    {
        return TRUE;
    }

    USHORT GetWhiteColor() const noexcept final
    {
        CRwMutex::CAutoSharedLock lock(m_cCurrentStateMutex);

        return static_cast<USHORT>(m_sCurrentState.sOutputRGB.r) | (static_cast<USHORT>(m_sCurrentState.sOutputRGB.r) << 8);
    }

    BOOL HasStrobe() const noexcept final
    {
        return TRUE;
    }

    BOOL GetStrobe() const noexcept final
    {
        CRwMutex::CAutoSharedLock lock(m_cCurrentStateMutex);

        return (m_sCurrentState.sOutputRGB.r >= 128) ? TRUE : FALSE;
    }

    BOOL HasCompactWhiteStrobeOutput() const noexcept final
    {
        return TRUE;
    }

    HRESULT OnPostAttachEvent() noexcept final;

    static Config GetDefaultConfig() noexcept
    {
        Config sDefaultConfig;

        return sDefaultConfig;
    }

    const Event_t &GetLastEvent() const noexcept
    {
        return m_sLastEvent;
    }

    RGB GetCurrentColor() const noexcept
    {
        CRwMutex::CAutoSharedLock lock(m_cCurrentStateMutex);

        return m_sCurrentState.sOutputRGB;
    }

    INT GetDefaultWidth() const noexcept final
    {
        return 42;
    }

    INT GetDefaultHeight() const noexcept final
    {
        return 9 + GetOutputViewHeight();
    }

private:
    typedef struct Feature_s {
        ULONGLONG ullHopStartSample{ 0 };
        ULONGLONG ullHopEndSample{ 0 };
        float fCandidateSignal{ 0.0f };
        float fLowEnergy{ 0.0f };
        float fMidEnergy{ 0.0f };
        float fHighEnergy{ 0.0f };
        float fLowFlux{ 0.0f };
        float fMidFlux{ 0.0f };
        float fHighFlux{ 0.0f };
        float fHighFluxLower{ 0.0f };
        float fHighFluxMiddle{ 0.0f };
        float fHighFluxUpper{ 0.0f };
        UINT32 ulHighBroadbandCoverage{ 0 };
        float fHighFlatness{ 0.0f };
        float fHighToMidRatio{ 0.0f };
    } Feature_t;

    typedef struct Candidate_s {
        Feature_t sFeature{};
        UINT64 ullCaptureQpcPosition{ 0 };
        UINT64 ullCallbackQpcPosition{ 0 };
        ULONGLONG ullConfirmAfterSample{ 0 };
        float fCandidateThreshold{ 0.0f };
        float fPeakSignal{ 0.0f };
        float fPeakHighEnergy{ 0.0f };
        float fPostHighEnergy{ 0.0f };
        float fPostMidEnergy{ 0.0f };
        UINT32 ulPostFeatureCount{ 0 };
    } Candidate_t;

    typedef struct CurrentState_s {
        Event_t sLastEvent{};
        UINT32 ulEventCount{ 0 };
        float fLastCandidateSignal{ 0.0f };
        float fLastAdaptiveThreshold{ 0.0f };
        RGB sOutputRGB{};
    } CurrentState_t;

private:
    BOOL ComputeSpectralHop(_In_ ULONGLONG ullHopEndSample, _In_ UINT64 ullCaptureQpcPosition, _In_ UINT64 ullCallbackQpcPosition,
                            _Out_ Feature_t &sFeature) noexcept;
    VOID UpdateBackground(_In_ float fCandidateSignal) noexcept;
    VOID GetBackground(_Out_ float &fMedian, _Out_ float &fMad) noexcept;
    VOID BeginCandidate(_In_ const Feature_t &sFeature, _In_ float fCandidateThreshold, _In_ UINT64 ullCaptureQpcPosition,
                        _In_ UINT64 ullCallbackQpcPosition) noexcept;
    VOID UpdateCandidate(_In_ const Feature_t &sFeature) noexcept;
    VOID ResolveCandidate(_In_ UINT64 ullCallbackQpcPosition) noexcept;
    VOID AppendEvent(_In_ const Event_t &sEvent) noexcept;
    float GetHistoryReinforcement(_In_ const Feature_t &sFeature) const noexcept;

    VOID RenderStaticLabels() noexcept;
    VOID RenderCurrentState() noexcept;

private:
    Config m_sConfig{};
    UINT32 m_ulSampleRate{ 0 };
    UINT64 m_ullQpcFrequency{ 0 };
    UINT32 m_ulHopSamples{ 0 };
    UINT32 m_ulConfirmationLookAheadSamples{ 0 };
    UINT32 m_ulCooldownSamples{ 0 };
    UINT32 m_ulFlashSamples{ 0 };
    UINT32 m_ulSamplesSinceHop{ 0 };
    UINT32 m_ulRingWriteIndex{ 0 };
    UINT32 m_ulRingSamplesAvailable{ 0 };
    UINT32 m_ulBackgroundWriteIndex{ 0 };
    UINT32 m_ulBackgroundCount{ 0 };
    UINT32 m_ulEventWriteIndex{ 0 };
    UINT32 m_ulEventCount{ 0 };
    BOOL m_bPreviousMagnitudesValid{ FALSE };
    BOOL m_bCandidateActive{ FALSE };
    ULONGLONG m_ullLastAcceptedSample{ 0 };
    ULONGLONG m_ullFlashEndSample{ 0 };
    float m_fLastCandidateSignal{ 0.0f };
    float m_fLastAdaptiveThreshold{ 0.0f };
    Candidate_t m_sCandidate{};
    Event_t m_sLastEvent{};
    RGB m_sCurrentRGB{};

    std::vector<float> m_vecPcmRing;
    std::vector<float> m_vecHannWindow;
    std::vector<float> m_vecCurrentMagnitudes;
    std::vector<float> m_vecPreviousMagnitudes;
    std::vector<float> m_vecBackground;
    std::vector<float> m_vecBackgroundScratch;
    std::vector<std::complex<float>> m_vecFftWork;
    std::vector<Event_t> m_vecEventHistory;

    mutable CRwMutex m_cCurrentStateMutex;
    CurrentState_t m_sCurrentState{};
};
