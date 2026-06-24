#include "AudioAnalysisMonitor.h"
#include "TUI\TUiFrameWindow.h"

// -----------------------------------------------------------------------------

static constexpr INT RAW_BANDS_TITLE_ROW = 9;
static constexpr INT RAW_BANDS_FIRST_ROW = 10;
static constexpr INT NORM_BANDS_TITLE_ROW = 14;
static constexpr INT NORM_BANDS_FIRST_ROW = 15;
static constexpr INT SUMMARY_TITLE_ROW = 19;
static constexpr INT SUMMARY_REGION_TOP = 19;
static constexpr INT SUMMARY_REGION_HEIGHT = 1;
static constexpr INT CHART_TITLE_ROW = 23;
static constexpr INT CHART_REGION_TOP = 23;
static constexpr INT CHART_BAR_WIDTH = 5;
static constexpr INT CHART_BAR_LEFT_COL = 6;

static constexpr COLORREF SUMMARY_FOREGROUND = RGB(204U, 204U, 204U);
static constexpr COLORREF SUMMARY_BACKGROUND = RGB(12U, 12U, 12U);
static constexpr COLORREF CHART_EMPTY_BACKGROUND = RGB(24U, 24U, 24U);

static const COLORREF aBandColors[CAudioAnalyzer::BAND_COUNT] = {
    RGB(255, 64, 64),
    RGB(255, 112, 48),
    RGB(255, 160, 32),
    RGB(255, 208, 0),
    RGB(196, 224, 0),
    RGB(112, 224, 64),
    RGB(32, 208, 128),
    RGB(32, 192, 192),
    RGB(32, 144, 255),
    RGB(80, 112, 255),
    RGB(144, 96, 255),
    RGB(208, 80, 255),
    RGB(255, 80, 208)
};

// -----------------------------------------------------------------------------

static INT GetChartRegionHeight(_In_ INT iClientHeight) noexcept
{
    INT iChartRegionHeight = iClientHeight - CHART_REGION_TOP;

    return (iChartRegionHeight > 2) ? iChartRegionHeight : 2;
}

static INT GetChartBarHeight(_In_ INT iChartRegionHeight) noexcept
{
    INT iChartBarHeight = iChartRegionHeight - 1;

    return (iChartBarHeight > 1) ? iChartBarHeight : 1;
}

// -----------------------------------------------------------------------------

VOID CAudioAnalysisMonitor::FirstRender() noexcept
{
    RenderStaticSections();
    RenderChartSkeleton();
    RenderBandLabels();
}

HRESULT CAudioAnalysisMonitor::OnPostAttachEvent() noexcept
{
    CTUiFrameWindow *lpFrameWindow;
    HRESULT hRes;

    lpFrameWindow = dynamic_cast<CTUiFrameWindow *>(ParentWindow());
    if (lpFrameWindow)
    {
        hRes = lpFrameWindow->SetCaption(L"Audio Analysis Monitor");
        if (FAILED(hRes))
        {
            return hRes;
        }
    }

    return CreateSubRegions();
}

VOID CAudioAnalysisMonitor::OnPreDetachEvent() noexcept
{
    DestroySubRegions();
}

VOID CAudioAnalysisMonitor::Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept
{
    const INT iChartRegionHeight = GetChartRegionHeight(GetClientHeight());
    const INT iChartBarHeight = GetChartBarHeight(iChartRegionHeight);

    if (sFrame.ulSamplesCount == 0)
    {
        return;
    }

    for (UINT32 ulBandIndex = 0; ulBandIndex < CAudioAnalyzer::BAND_COUNT; ulBandIndex++)
    {
        INT iBarLeftCol = CHART_BAR_LEFT_COL + static_cast<INT>(ulBandIndex) * CHART_BAR_WIDTH;
        INT iFilledRows;
        COLORREF crBandColor;
        BYTE uRed, uGreen, uBlue;

        iFilledRows = static_cast<INT>(sFrame.fBandRmsNorm[ulBandIndex] * static_cast<float>(iChartBarHeight) + 0.999f);
        if (iFilledRows < 0)
        {
            iFilledRows = 0;
        }
        if (iFilledRows > iChartBarHeight)
        {
            iFilledRows = iChartBarHeight;
        }

        crBandColor = GetBandColor(ulBandIndex);
        uRed = GetRValue(crBandColor);
        uGreen = GetGValue(crBandColor);
        uBlue = GetBValue(crBandColor);

        for (INT iRow = 0; iRow < iChartBarHeight; iRow++)
        {
            INT iScreenRow = iChartBarHeight - iRow;

            if (iRow < iFilledRows)
            {
                m_lpTerminalControl->PrintRegion(m_hChartRegion, L"\x1b[%d;%dH\x1b[48;2;%u;%u;%um    \x1b[0m",
                                                 iScreenRow, iBarLeftCol, uRed, uGreen, uBlue);
            }
            else
            {
                m_lpTerminalControl->PrintRegion(m_hChartRegion, L"\x1b[%d;%dH\x1b[48;2;24;24;24m    \x1b[0m",
                                                 iScreenRow, iBarLeftCol);
            }
        }
    }

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;12H%8u", sFrame.ulSamplesCount);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;39H%8.5f", sFrame.fBlockTimeSec);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;62H%10.6f", sFrame.fPeak);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;12H%10.6f", sFrame.fBlockRms);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;39H%8.5f", sFrame.fOnsetStrength);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;62H%s\x1b[0m",
                                     (sFrame.bClippingDetected != FALSE) ? L"\x1b[91mYES " : L"\x1b[92mNO  ");

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;12H%10.6f", sFrame.fLowEnergyRaw);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;38H%10.6f", sFrame.fMidEnergyRaw);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;63H%10.6f", sFrame.fHighEnergyRaw);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;12H%10.6f", sFrame.fLowEnergyNorm);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;38H%10.6f", sFrame.fMidEnergyNorm);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;63H%10.6f", sFrame.fHighEnergyNorm);

    for (UINT32 ulBandIndex = 0; ulBandIndex < CAudioAnalyzer::BAND_COUNT; ulBandIndex++)
    {
        INT iBaseCol = 7 + static_cast<INT>(ulBandIndex % 5) * 15;
        INT iRawRow = RAW_BANDS_FIRST_ROW + static_cast<INT>(ulBandIndex / 5);
        INT iNormRow = NORM_BANDS_FIRST_ROW + static_cast<INT>(ulBandIndex / 5);

        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;%dH%8.6f", iRawRow, iBaseCol, sFrame.fBandRmsRaw[ulBandIndex]);
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;%dH%8.6f", iNormRow, iBaseCol, sFrame.fBandRmsNorm[ulBandIndex]);
    }

    AppendSummaryLine(sFrame);
}

HRESULT CAudioAnalysisMonitor::CreateSubRegions() noexcept
{
    HRESULT hRes;
    INT iChartRegionHeight;

    DestroySubRegions();

    hRes = m_lpTerminalControl->CreateRegion(0, SUMMARY_REGION_TOP, GetClientWidth(), SUMMARY_REGION_HEIGHT, &m_hSummaryRegion,
                                             GetClientRegionHandle());
    if (FAILED(hRes))
    {
        return hRes;
    }

    iChartRegionHeight = GetChartRegionHeight(GetClientHeight());
    hRes = m_lpTerminalControl->CreateRegion(0, CHART_REGION_TOP, GetClientWidth(), iChartRegionHeight, &m_hChartRegion,
                                             GetClientRegionHandle());
    if (FAILED(hRes))
    {
        DestroySubRegions();
        return hRes;
    }

    return S_OK;
}

VOID CAudioAnalysisMonitor::DestroySubRegions() noexcept
{
    if (m_lpTerminalControl)
    {
        if (m_hChartRegion)
        {
            m_lpTerminalControl->DestroyRegion(m_hChartRegion);
            m_hChartRegion = nullptr;
        }
        if (m_hSummaryRegion)
        {
            m_lpTerminalControl->DestroyRegion(m_hSummaryRegion);
            m_hSummaryRegion = nullptr;
        }
    }
}

VOID CAudioAnalysisMonitor::RenderStaticSections() noexcept
{
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;1H\x1b[97mFrame\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;1HSamples:      ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;27HBlock Time:   ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;50HPeak:         ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;1HBlock RMS:    ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;27HOnset:        ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;50HClipping:     ");

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[5;1H\x1b[97mGrouped Energy\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;1HLow Raw:      ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;27HMid Raw:      ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;51HHigh Raw:     ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;1HLow Norm:     ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;27HMid Norm:     ");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;51HHigh Norm:    ");

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H\x1b[97mRaw RMS Bands\x1b[0m", RAW_BANDS_TITLE_ROW);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H\x1b[97mNormalized RMS Bands\x1b[0m", NORM_BANDS_TITLE_ROW);

    for (UINT32 ulBandIndex = 0; ulBandIndex < CAudioAnalyzer::BAND_COUNT; ulBandIndex++)
    {
        WCHAR szLabelW[4];
        INT iBaseCol = 2 + static_cast<INT>(ulBandIndex % 5) * 15;
        INT iRawRow = RAW_BANDS_FIRST_ROW + static_cast<INT>(ulBandIndex / 5);
        INT iNormRow = NORM_BANDS_FIRST_ROW + static_cast<INT>(ulBandIndex / 5);

        FormatBandLabel(ulBandIndex, szLabelW);
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;%dH%3ls:", iRawRow, iBaseCol, szLabelW);
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;%dH%3ls:", iNormRow, iBaseCol, szLabelW);
    }

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H\x1b[97mRecent Frames\x1b[0m", SUMMARY_TITLE_ROW);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H\x1b[97mNormalized RMS Bands\x1b[0m", CHART_TITLE_ROW);
}

VOID CAudioAnalysisMonitor::RenderChartSkeleton() noexcept
{
    const INT iChartRegionHeight = GetChartRegionHeight(GetClientHeight());
    const INT iChartBarHeight = GetChartBarHeight(iChartRegionHeight);

    if (!m_hChartRegion)
    {
        return;
    }

    for (INT iRow = 0; iRow < iChartBarHeight; iRow++)
    {
        m_lpTerminalControl->PrintRegion(m_hChartRegion, L"\x1b[%d;1H\x1b[90m%4.2f\x1b[0m",
                                         1 + iRow, (iChartBarHeight > 1) ? 1.0f - (float)iRow / (float)(iChartBarHeight - 1) : 1.0f);
    }
}

VOID CAudioAnalysisMonitor::RenderBandLabels() noexcept
{
    const INT iChartRegionHeight = GetChartRegionHeight(GetClientHeight());

    if (!m_hChartRegion)
    {
        return;
    }

    for (UINT32 ulBandIndex = 0; ulBandIndex < CAudioAnalyzer::BAND_COUNT; ulBandIndex++)
    {
        WCHAR szLabelW[4];
        INT iLabelCol = CHART_BAR_LEFT_COL + static_cast<INT>(ulBandIndex) * CHART_BAR_WIDTH;

        FormatBandLabel(ulBandIndex, szLabelW);
        m_lpTerminalControl->PrintRegion(m_hChartRegion, L"\x1b[%d;%dH%-3ls", iChartRegionHeight, iLabelCol, szLabelW);
    }
}

VOID CAudioAnalysisMonitor::AppendSummaryLine(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept
{
    WCHAR szLineW[128];

    if (!m_hSummaryRegion)
    {
        return;
    }

    swprintf_s(szLineW, _countof(szLineW),
               L"Peak=%0.5f RMS=%0.5f Onset=%0.5f L/M/H=%0.4f/%0.4f/%0.4f Clip=%ls",
               sFrame.fPeak, sFrame.fBlockRms, sFrame.fOnsetStrength,
               sFrame.fLowEnergyNorm, sFrame.fMidEnergyNorm, sFrame.fHighEnergyNorm,
               (sFrame.bClippingDetected != FALSE) ? L"YES" : L"NO");

    m_lpTerminalControl->MoveRegion(m_hSummaryRegion, 0, 1, GetClientWidth(), SUMMARY_REGION_HEIGHT - 1, 0, 0, L' ',
                                    SUMMARY_FOREGROUND, SUMMARY_BACKGROUND, GuiTerminal::Control::StyleNone);
    m_lpTerminalControl->PrintRegion(m_hSummaryRegion, L"\x1b[%d;1H%-*ls", SUMMARY_REGION_HEIGHT, GetClientWidth(), szLineW);
}

VOID CAudioAnalysisMonitor::FormatBandLabel(_In_ UINT32 ulBandIndex, _Out_writes_z_(4) LPWSTR szLabelW) const noexcept
{
    const CAudioAnalyzer::BandDefinition_t *lpBandDefinitions = CAudioAnalyzer::GetBandDefinitions();
    float fCenterHz = lpBandDefinitions[ulBandIndex].fCenterHz;

    if (fCenterHz >= 1000.0f)
    {
        INT iWholeKHz = static_cast<INT>(fCenterHz / 1000.0f);
        INT iTenthKHz = static_cast<INT>(fCenterHz / 100.0f) % 10;

        if (iTenthKHz == 0)
        {
            swprintf_s(szLabelW, 4, L"%dk", iWholeKHz);
        }
        else
        {
            swprintf_s(szLabelW, 4, L"%dk%d", iWholeKHz, iTenthKHz);
        }
    }
    else
    {
        swprintf_s(szLabelW, 4, L"%3d", static_cast<INT>(fCenterHz + 0.5f));
    }
}

COLORREF CAudioAnalysisMonitor::GetBandColor(_In_ UINT32 ulBandIndex) const noexcept
{
    return aBandColors[ulBandIndex];
}
