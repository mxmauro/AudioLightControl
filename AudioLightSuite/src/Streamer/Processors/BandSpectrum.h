#pragma once

#include "BaseAudioProcessor.h"
#include "Utils\RwMutex.h"

// -----------------------------------------------------------------------------

class CBandSpectrumAudioProcessor : public CBaseAudioProcessor
{
public:
    CBandSpectrumAudioProcessor() noexcept = default;
    CBandSpectrumAudioProcessor(_In_ const CBandSpectrumAudioProcessor &) = delete;
    CBandSpectrumAudioProcessor(_Inout_ CBandSpectrumAudioProcessor &&) = delete;
    ~CBandSpectrumAudioProcessor() noexcept = default;

    CBandSpectrumAudioProcessor &operator=(_In_ const CBandSpectrumAudioProcessor &) = delete;
    CBandSpectrumAudioProcessor &operator=(_Inout_ CBandSpectrumAudioProcessor &&) = delete;

    LPCSTR GetType() const noexcept final
    {
        return "BandSpectrum";
    }

    VOID FirstRender() noexcept final;
    VOID Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept final;
    VOID TimerTick() noexcept final;

    BOOL HasRGBStripColors() const noexcept final
    {
        return TRUE;
    }

    VOID GetRGBStripColors(_Out_writes_(nColorCount) CBaseAudioProcessor::LPRGB lpsColors,
                           _In_ SIZE_T nColorCount) const noexcept final;

    HRESULT OnPostAttachEvent() noexcept final;

    INT GetDefaultWidth() const noexcept final
    {
        return 42;
    }

    INT GetDefaultHeight() const noexcept final
    {
        return 3;
    }

private:
    typedef struct CurrentState_s {
        float afBandRmsNorm[CAudioAnalyzer::BAND_COUNT]{};
    } CurrentState_t;

private:
    static CBaseAudioProcessor::RGB GetStripColor(_In_ SIZE_T nColorIndex, _In_ SIZE_T nColorCount,
                                                   _In_reads_(CAudioAnalyzer::BAND_COUNT) const float afBandRmsNorm[]) noexcept;
    static CBaseAudioProcessor::RGB GetBandColor(_In_ UINT32 ulBandIndex, _In_ float fLevel) noexcept;
    static VOID HueToRGB(_In_ float fHueDegrees, _Out_ float &fRed, _Out_ float &fGreen, _Out_ float &fBlue) noexcept;

    VOID GetBandLevels(_Out_writes_(CAudioAnalyzer::BAND_COUNT) float afBandRmsNorm[]) const noexcept;
    VOID RenderStripPreview() noexcept;

private:
    mutable CRwMutex m_cCurrentStateRwMutex;
    CurrentState_t m_sCurrentState{};
};
