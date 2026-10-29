#pragma once

#include "BaseMonitor.h"
#include "Utils\Mutex.h"
#include <memory>

// -----------------------------------------------------------------------------

struct SPECTROGRAM_STATE;

class CAudioAnalysisMonitor : public CBaseMonitor
{
public:
    CAudioAnalysisMonitor() noexcept = default;
    CAudioAnalysisMonitor(_In_ const CAudioAnalysisMonitor &) = delete;
    CAudioAnalysisMonitor(_Inout_ CAudioAnalysisMonitor &&) = delete;
    ~CAudioAnalysisMonitor() noexcept = default;

    CAudioAnalysisMonitor &operator=(_In_ const CAudioAnalysisMonitor &) = delete;
    CAudioAnalysisMonitor &operator=(_Inout_ CAudioAnalysisMonitor &&) = delete;

    LPCSTR GetType() const noexcept final
    {
        return "AudioAnalysisMonitor";
    }

    VOID FirstRender() noexcept final;

    HRESULT Initialize(_In_ UINT32 ulFftMagnitudeCount, _In_ float fFftMagnitudeFullScale, _In_ float fSampleRate,
                       _In_ float fSpectralHopDurationSec) noexcept;
    VOID Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept final;
    VOID TimerTick() noexcept final;

    HRESULT OnPostAttachEvent() noexcept final;
    VOID OnPreDetachEvent() noexcept final;

    INT GetDefaultWidth() const noexcept final
    {
        return 82;
    }

    INT GetDefaultHeight() const noexcept final
    {
        return 40;
    }

private:
    HRESULT CreateSubRegions() noexcept;
    VOID DestroySubRegions() noexcept;
    VOID RenderStaticSections() noexcept;
    VOID RenderFrame(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept;

private:
    GuiTerminal::RegionHandle m_hSpectrogramRegion{ nullptr };
    GuiTerminal::RegionHandle m_hBandChartRegion{ nullptr };
    CMutex m_cFrameMutex;
    std::shared_ptr<SPECTROGRAM_STATE> m_spSpectrogramState;
    CAudioAnalyzer::Frame_t m_sLatestFrame{};
    BOOL m_bHasLatestFrame{ FALSE };
};
