#pragma once

#include "BaseMonitor.h"

// -----------------------------------------------------------------------------

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

    VOID Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept final;

    HRESULT OnPostAttachEvent() noexcept final;
    VOID OnPreDetachEvent() noexcept final;

    INT GetDefaultWidth() const noexcept final
    {
        return 82;
    }

    INT GetDefaultHeight() const noexcept final
    {
        return 32;
    }

private:
    HRESULT CreateSubRegions() noexcept;
    VOID DestroySubRegions() noexcept;
    VOID RenderStaticSections() noexcept;
    VOID RenderChartSkeleton() noexcept;
    VOID RenderBandLabels() noexcept;
    VOID AppendSummaryLine(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept;
    VOID FormatBandLabel(_In_ UINT32 ulBandIndex, _Out_writes_z_(4) LPWSTR szLabelW) const noexcept;
    COLORREF GetBandColor(_In_ UINT32 ulBandIndex) const noexcept;

private:
    GuiTerminal::RegionHandle m_hSummaryRegion{ nullptr };
    GuiTerminal::RegionHandle m_hChartRegion{ nullptr };
};
