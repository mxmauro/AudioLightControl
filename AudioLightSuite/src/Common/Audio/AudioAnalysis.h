#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Utils\MathHelpers.h"

// -----------------------------------------------------------------------------

class CAudioAnalyzer
{
public:
    static constexpr UINT32 BAND_COUNT = 13;

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

    static constexpr UINT32 BAND_LOW_FIRST_INDEX = BAND_SUB_KICK_THUMP_INDEX;
    static constexpr UINT32 BAND_LOW_LAST_INDEX = BAND_LOW_MID_PUNCH_INDEX;
    static constexpr UINT32 BAND_MID_FIRST_INDEX = BAND_MID_BODY_INDEX;
    static constexpr UINT32 BAND_MID_LAST_INDEX = BAND_PRESENCE_INDEX;
    static constexpr UINT32 BAND_HIGH_FIRST_INDEX = BAND_SNAP_STICK_INDEX;
    static constexpr UINT32 BAND_HIGH_LAST_INDEX = BAND_HI_HATS_SPARKLE_AIR_INDEX;

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

public:
    CAudioAnalyzer() noexcept = default;
    CAudioAnalyzer(_In_ const CAudioAnalyzer &) = default;
    CAudioAnalyzer(_Inout_ CAudioAnalyzer &&) = default;
    ~CAudioAnalyzer() = default;

    CAudioAnalyzer &operator=(_In_ const CAudioAnalyzer &) = default;
    CAudioAnalyzer &operator=(_Inout_ CAudioAnalyzer &&) = default;

    VOID SetFrequency(_In_ float fSampleRate) noexcept;

    VOID Analyze(_In_reads_(ulSamplesCount) const float *lpfPCM, _In_ UINT32 ulSamplesCount, _Out_ Frame_t &sFrame) noexcept;

    static const BandDefinition_t *GetBandDefinitions() noexcept;

private:
    VOID ComputeBandsRMS(_Inout_ Frame_t &sFrame) noexcept;
    VOID ComputeBlockMetrics(_Inout_ Frame_t &sFrame) noexcept;
    VOID ComputeGroupedEnergies(_Inout_ Frame_t &sFrame) noexcept;
    VOID ComputeNormalizedMetrics(_Inout_ Frame_t &sFrame, _In_ ULONGLONG ullNow) noexcept;
    VOID ComputeOnsetStrength(_Inout_ Frame_t &sFrame, _In_ ULONGLONG ullNow) noexcept;
    VOID AppendHistory(_In_ const Frame_t &sFrame, _In_ ULONGLONG ullNow) noexcept;

private:
    static constexpr UINT32 MAX_HISTORICAL_ANALYSIS_FRAMES = 256;

    MathHelpers::CBiQuad m_aBands[BAND_COUNT]{};
    float m_fSampleRate{ 0.0f };
    HistoricalAnalysisFrame_t m_aHistoricalFrames[MAX_HISTORICAL_ANALYSIS_FRAMES]{};
    UINT32 m_ulNextHistoricalFrame{ 0 };
};
