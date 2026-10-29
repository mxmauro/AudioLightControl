#include "AudioAnalysisMonitor.h"
#include "TUI\TUiFrameWindow.h"
#include "Utils\Exception2HR.h"
#include <d2d1.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <vector>

// -----------------------------------------------------------------------------

static constexpr INT SPECTROGRAM_TITLE_ROW = 9;
static constexpr INT SPECTROGRAM_REGION_TOP = 9;
static constexpr INT SPECTROGRAM_REGION_HEIGHT = 18;
static constexpr float SPECTROGRAM_LABEL_GUTTER_WIDTH_DIP = 42.0f;
static constexpr float SPECTROGRAM_HISTORY_DURATION_SEC = 10.0f;
static constexpr float SPECTROGRAM_DYNAMIC_RANGE_DB = 80.0f;
static constexpr float SPECTROGRAM_MAGNITUDE_EPSILON = 1.0e-12f;
static constexpr UINT32 SPECTROGRAM_HISTORY_EXTRA_FRAMES = 2;
static constexpr UINT32 SPECTROGRAM_BLACK_PIXEL = 0xFF000000U;
static constexpr INT CHART_TITLE_ROW = 29;
static constexpr INT CHART_REGION_TOP = 29;
static constexpr float CHART_LABEL_HEIGHT_DIP = 11.0f;
static constexpr float CHART_BAR_WIDTH_FRACTION = 0.8f;

static const COLORREF aBandColors[CAudioAnalyzer::BAND_COUNT] = {
    RGB(255, 64, 64), RGB(255, 112, 48), RGB(255, 160, 32), RGB(255, 208, 0),
    RGB(196, 224, 0), RGB(112, 224, 64), RGB(32, 208, 128), RGB(32, 192, 192),
    RGB(32, 144, 255), RGB(80, 112, 255), RGB(144, 96, 255), RGB(208, 80, 255),
    RGB(255, 80, 208), RGB(255, 255, 255)
};

// -----------------------------------------------------------------------------

struct SPECTROGRAM_STATE {
    CMutex cHistoryMutex;
    UINT32 ulFftMagnitudeCount{};
    UINT32 ulHistoryCapacity{};
    UINT32 ulHistoryCount{};
    UINT32 ulNextHistoryFrame{};
    float fSampleRate{};
    float fFftMagnitudeFullScale{};
    UINT64 ullHistoryVersion{};
    float afBandRmsNorm[CAudioAnalyzer::BAND_COUNT]{};
    BOOL bHasBandFrame{FALSE};
    std::vector<float> vecHistory;
    std::vector<double> vecHistoryEndTimeSec;

    std::vector<float> vecSnapshot;
    std::vector<double> vecSnapshotEndTimeSec;
    UINT32 ulSnapshotCount{};
    UINT64 ullSnapshotVersion{};

    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    UINT32 uiBitmapWidth{};
    UINT32 uiBitmapHeight{};
    UINT64 ullRenderedHistoryVersion{UINT64_MAX};
    std::vector<UINT32> vecPixels;
};

// -----------------------------------------------------------------------------

static INT GetChartRegionHeight(_In_ INT iClientHeight) noexcept;
static VOID DrawBandChart(_In_ GuiTerminal::DrawContext &drawContext, _In_ GuiTerminal::RegionHandle hRegion,
                          _In_ const std::shared_ptr<SPECTROGRAM_STATE> &spState) noexcept;
static VOID DrawSpectrogram(_In_ GuiTerminal::DrawContext &drawContext, _In_ GuiTerminal::RegionHandle hRegion,
                            _In_ const std::shared_ptr<SPECTROGRAM_STATE> &spState) noexcept;
static VOID DrawSpectrogramFrequencyLabels(_In_ GuiTerminal::DrawContext &drawContext,
                                           _In_ const SPECTROGRAM_STATE &sState) noexcept;
static VOID FormatBandLabel(_In_ UINT32 ulBandIndex, _Out_writes_z_(4) LPWSTR szLabelW) noexcept;
static VOID ReleaseSpectrogramResources(_In_ const std::shared_ptr<SPECTROGRAM_STATE> &spState) noexcept;
static VOID SnapshotSpectrogramHistory(_In_ const std::shared_ptr<SPECTROGRAM_STATE> &spState) noexcept;
static VOID UpdateSpectrogramPixels(_In_ SPECTROGRAM_STATE &sState, _In_ UINT32 uiWidth, _In_ UINT32 uiHeight);
static float GetInterpolatedSpectrumIntensity(_In_reads_(ulFftMagnitudeCount) const float *lpfSpectrum,
                                              _In_ UINT32 ulFftMagnitudeCount, _In_ float fBinIndex) noexcept;
static float GetSpectrogramIntensity(_In_ float fMagnitude, _In_ float fFullScaleMagnitude) noexcept;
static UINT32 GetSpectrogramPixel(_In_ float fIntensity) noexcept;
static float MelFromHz(_In_ float fHz) noexcept;
static float HzFromMel(_In_ float fMel) noexcept;

// -----------------------------------------------------------------------------

HRESULT CAudioAnalysisMonitor::Initialize(_In_ UINT32 ulFftMagnitudeCount, _In_ float fFftMagnitudeFullScale,
                                          _In_ float fSampleRate, _In_ float fSpectralHopDurationSec) noexcept
{
    std::shared_ptr<SPECTROGRAM_STATE> spState;
    HRESULT hRes;

    if (ulFftMagnitudeCount < 2 || fFftMagnitudeFullScale <= 0.0f || fSampleRate <= 0.0f ||
        fSpectralHopDurationSec <= 0.0f)
    {
        return E_INVALIDARG;
    }

    hRes = TranslateToHresult([&]()
    {
        const UINT32 ulHistoryCapacity = static_cast<UINT32>(ceilf(SPECTROGRAM_HISTORY_DURATION_SEC / fSpectralHopDurationSec)) +
                                         SPECTROGRAM_HISTORY_EXTRA_FRAMES;

        spState = std::make_shared<SPECTROGRAM_STATE>();
        spState->ulFftMagnitudeCount = ulFftMagnitudeCount;
        spState->ulHistoryCapacity = ulHistoryCapacity;
        spState->fSampleRate = fSampleRate;
        spState->fFftMagnitudeFullScale = fFftMagnitudeFullScale;
        spState->vecHistory.resize(static_cast<SIZE_T>(ulHistoryCapacity) * ulFftMagnitudeCount);
        spState->vecHistoryEndTimeSec.resize(ulHistoryCapacity);
        spState->vecSnapshot.resize(static_cast<SIZE_T>(ulHistoryCapacity) * ulFftMagnitudeCount);
        spState->vecSnapshotEndTimeSec.resize(ulHistoryCapacity);
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    m_spSpectrogramState = spState;
    return S_OK;
}

VOID CAudioAnalysisMonitor::FirstRender() noexcept
{
    RenderStaticSections();
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
    std::shared_ptr<SPECTROGRAM_STATE> spState = m_spSpectrogramState;

    if (sFrame.ulSamplesCount == 0)
    {
        return;
    }

    {
        CMutex::CAutoLock lock(m_cFrameMutex);

        m_sLatestFrame = sFrame;
        m_sLatestFrame.lpfPCM = nullptr;
        m_sLatestFrame.lpfFftMagnitudeRaw = nullptr;
        m_sLatestFrame.lpfFftMagnitudeNorm = nullptr;
        m_bHasLatestFrame = TRUE;
    }

    if (!spState)
    {
        return;
    }

    {
        CMutex::CAutoLock lock(spState->cHistoryMutex);
        memcpy(spState->afBandRmsNorm, sFrame.fBandRmsNorm, sizeof(spState->afBandRmsNorm));
        spState->bHasBandFrame = TRUE;

        if (sFrame.bSpectralReady != FALSE && sFrame.lpfFftMagnitudeRaw &&
            sFrame.ulFftMagnitudeCount == spState->ulFftMagnitudeCount)
        {
            const UINT32 ulHistoryIndex = spState->ulNextHistoryFrame;
            float *lpfHistory = spState->vecHistory.data() +
                                static_cast<SIZE_T>(ulHistoryIndex) * spState->ulFftMagnitudeCount;

            memcpy(lpfHistory, sFrame.lpfFftMagnitudeRaw,
                   static_cast<SIZE_T>(spState->ulFftMagnitudeCount) * sizeof(float));
            spState->vecHistoryEndTimeSec[ulHistoryIndex] = static_cast<double>(sFrame.ullEndSampleIndex) / spState->fSampleRate;
            spState->ulNextHistoryFrame = (ulHistoryIndex + 1) % spState->ulHistoryCapacity;
            if (spState->ulHistoryCount < spState->ulHistoryCapacity)
            {
                spState->ulHistoryCount += 1;
            }
            spState->ullHistoryVersion += 1;
        }
    }
}

VOID CAudioAnalysisMonitor::TimerTick() noexcept
{
    CAudioAnalyzer::Frame_t sFrame;

    {
        CMutex::CAutoLock lock(m_cFrameMutex);

        if (m_bHasLatestFrame == FALSE)
        {
            return;
        }
        sFrame = m_sLatestFrame;
    }

    RenderFrame(sFrame);
    if (m_hSpectrogramRegion)
    {
        m_lpTerminalControl->InvalidateRegion(m_hSpectrogramRegion);
    }
    if (m_hBandChartRegion)
    {
        m_lpTerminalControl->InvalidateRegion(m_hBandChartRegion);
    }
}

VOID CAudioAnalysisMonitor::RenderFrame(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept
{
    if (sFrame.ulSamplesCount == 0)
    {
        return;
    }

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;12H%8u", sFrame.ulSamplesCount);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;33H%8.5f", sFrame.fBlockTimeSec);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;53H%8.6f", sFrame.fPeak);
    if (sFrame.bBpmReady != FALSE)
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;69H%6.1f", sFrame.fBpm);
    }
    else
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;69H   -- ");
    }
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;12H%8.6f", sFrame.fBlockRms);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;33H%8.5f", sFrame.fOnsetStrength);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;53H%s\x1b[0m",
                                     (sFrame.bClippingDetected != FALSE) ? L"\x1b[91mYES" : L"\x1b[92mNO ");

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;11H%8.6f", sFrame.fLowEnergyRaw);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;31H%8.6f", sFrame.fMidEnergyRaw);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;52H%8.6f", sFrame.fHighEnergyRaw);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;11H%8.6f", sFrame.fLowEnergyNorm);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;31H%8.6f", sFrame.fMidEnergyNorm);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;52H%8.6f", sFrame.fHighEnergyNorm);
}

HRESULT CAudioAnalysisMonitor::CreateSubRegions() noexcept
{
    const std::shared_ptr<SPECTROGRAM_STATE> spState = m_spSpectrogramState;
    HRESULT hRes;
    INT iChartRegionHeight;

    if (!spState)
    {
        return E_UNEXPECTED;
    }

    DestroySubRegions();

    hRes = m_lpTerminalControl->CreateCustomDrawRegion(0, SPECTROGRAM_REGION_TOP, GetClientWidth(), SPECTROGRAM_REGION_HEIGHT,
                                                        &m_hSpectrogramRegion,
                                                        [spState](GuiTerminal::DrawContext &drawContext, GuiTerminal::RegionHandle hRegion)
    {
        DrawSpectrogram(drawContext, hRegion, spState);
    }, GetClientRegionHandle());
    if (FAILED(hRes))
    {
        DestroySubRegions();
        return hRes;
    }

    hRes = m_lpTerminalControl->SetCustomDrawRegionResourceCleanup(m_hSpectrogramRegion,
                                                                     [spState](GuiTerminal::CustomDrawResourceCleanupReason)
    {
        ReleaseSpectrogramResources(spState);
    });
    if (FAILED(hRes))
    {
        DestroySubRegions();
        return hRes;
    }

    iChartRegionHeight = GetChartRegionHeight(GetClientHeight());
    hRes = m_lpTerminalControl->CreateCustomDrawRegion(0, CHART_REGION_TOP, GetClientWidth(), iChartRegionHeight,
                                                        &m_hBandChartRegion,
                                                        [spState](GuiTerminal::DrawContext &drawContext, GuiTerminal::RegionHandle hRegion)
    {
        DrawBandChart(drawContext, hRegion, spState);
    }, GetClientRegionHandle());
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
        if (m_hBandChartRegion)
        {
            m_lpTerminalControl->DestroyRegion(m_hBandChartRegion);
            m_hBandChartRegion = nullptr;
        }
        if (m_hSpectrogramRegion)
        {
            m_lpTerminalControl->DestroyRegion(m_hSpectrogramRegion);
            m_hSpectrogramRegion = nullptr;
        }
    }
}

VOID CAudioAnalysisMonitor::RenderStaticSections() noexcept
{
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;1H\x1b[96mFrame\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;1H\x1b[97mSamples:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;22H\x1b[97mBlock Time:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;43H\x1b[97mPeak:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[2;64H\x1b[97mBPM:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;1H\x1b[97mBlock RMS:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;22H\x1b[97mOnset:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;43H\x1b[97mClipping:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[5;1H\x1b[96mGrouped Energy\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;1H\x1b[97mLow Raw:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;21H\x1b[97mMid Raw:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[6;41H\x1b[97mHigh Raw:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;1H\x1b[97mLow Norm:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;21H\x1b[97mMid Norm:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;41H\x1b[97mHigh Norm:\x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H\x1b[96mSpectrogram\x1b[0m", SPECTROGRAM_TITLE_ROW);
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H\x1b[96mBands\x1b[0m", CHART_TITLE_ROW);
}

// -----------------------------------------------------------------------------

static INT GetChartRegionHeight(_In_ INT iClientHeight) noexcept
{
    const INT iChartRegionHeight = iClientHeight - CHART_REGION_TOP;

    return (iChartRegionHeight > 2) ? iChartRegionHeight : 2;
}

static VOID DrawBandChart(_In_ GuiTerminal::DrawContext &drawContext, _In_ GuiTerminal::RegionHandle hRegion,
                          _In_ const std::shared_ptr<SPECTROGRAM_STATE> &spState) noexcept
{
    float afBandRmsNorm[CAudioAnalyzer::BAND_COUNT];
    BOOL bHasBandFrame = FALSE;
    const INT iWidth = drawContext.GetWidth();
    const INT iHeight = drawContext.GetHeight();

    UNREFERENCED_PARAMETER(hRegion);

    drawContext.Clear(RGB(0U, 0U, 0U));
    if ((!spState) || iWidth <= 0 || iHeight <= 0)
    {
        return;
    }

    {
        CMutex::CAutoLock lock(spState->cHistoryMutex);

        bHasBandFrame = spState->bHasBandFrame;
        memcpy(afBandRmsNorm, spState->afBandRmsNorm, sizeof(afBandRmsNorm));
    }
    if (bHasBandFrame == FALSE)
    {
        return;
    }

    const float fBarBottom = static_cast<float>(iHeight) - CHART_LABEL_HEIGHT_DIP;
    const float fBarTop = 2.0f;
    const float fSlotWidth = static_cast<float>(iWidth) / CAudioAnalyzer::BAND_COUNT;
    const float fBarWidth = fSlotWidth * CHART_BAR_WIDTH_FRACTION;

    if (fBarBottom <= fBarTop || fSlotWidth <= 0.0f)
    {
        return;
    }

    for (UINT32 ulBandIndex = 0; ulBandIndex < CAudioAnalyzer::BAND_COUNT; ulBandIndex++)
    {
        WCHAR szLabelW[4];
        float fPower = afBandRmsNorm[ulBandIndex];
        const float fSlotLeft = static_cast<float>(ulBandIndex) * fSlotWidth;
        const float fBarLeft = fSlotLeft + (fSlotWidth - fBarWidth) * 0.5f;
        float fBarTopPosition;

        if (fPower < 0.0f)
        {
            fPower = 0.0f;
        }
        else if (fPower > 1.0f)
        {
            fPower = 1.0f;
        }

        fBarTopPosition = fBarBottom - (fBarBottom - fBarTop) * fPower;
        drawContext.FillRectangle(D2D1::RectF(fBarLeft, fBarTopPosition, fBarLeft + fBarWidth, fBarBottom),
                                  aBandColors[ulBandIndex]);

        FormatBandLabel(ulBandIndex, szLabelW);
        drawContext.Write(szLabelW, D2D1::Point2F(fSlotLeft + fSlotWidth * 0.5f, fBarBottom + 1.0f), RGB(224U, 224U, 224U),
                          L"Arial", 8.0f, 0U,
                          static_cast<GuiTerminal::DrawContext::TextAlignment>(GuiTerminal::DrawContext::AlignCenter |
                                                                                GuiTerminal::DrawContext::AlignTop));
    }
}

static VOID DrawSpectrogram(_In_ GuiTerminal::DrawContext &drawContext, _In_ GuiTerminal::RegionHandle hRegion,
                            _In_ const std::shared_ptr<SPECTROGRAM_STATE> &spState) noexcept
{
    const INT iWidth = drawContext.GetWidth();
    const INT iHeight = drawContext.GetHeight();
    const INT iPlotLeft = static_cast<INT>(SPECTROGRAM_LABEL_GUTTER_WIDTH_DIP);
    const INT iPlotWidth = iWidth - iPlotLeft;

    UNREFERENCED_PARAMETER(hRegion);

    drawContext.Clear(RGB(0U, 0U, 0U));
    if ((!spState) || iPlotWidth <= 0 || iHeight <= 0)
    {
        return;
    }

    try
    {
        ID2D1RenderTarget *lpRenderTarget = drawContext.GetDirect2DRenderTarget();

        if (!lpRenderTarget)
        {
            return;
        }

        DrawSpectrogramFrequencyLabels(drawContext, *spState);
        SnapshotSpectrogramHistory(spState);
        if (spState->ulSnapshotCount == 0)
        {
            return;
        }

        if (spState->ullRenderedHistoryVersion != spState->ullSnapshotVersion ||
            spState->uiBitmapWidth != static_cast<UINT32>(iPlotWidth) || spState->uiBitmapHeight != static_cast<UINT32>(iHeight))
        {
            D2D1_BITMAP_PROPERTIES bitmapProperties;
            HRESULT hRes;

            UpdateSpectrogramPixels(*spState, static_cast<UINT32>(iPlotWidth), static_cast<UINT32>(iHeight));
            if (!spState->bitmap || spState->uiBitmapWidth != static_cast<UINT32>(iPlotWidth) ||
                spState->uiBitmapHeight != static_cast<UINT32>(iHeight))
            {
                spState->bitmap.Reset();
                bitmapProperties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
                hRes = lpRenderTarget->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(iPlotWidth), static_cast<UINT32>(iHeight)), nullptr, 0,
                                                     &bitmapProperties, spState->bitmap.GetAddressOf());
                if (FAILED(hRes))
                {
                    ReleaseSpectrogramResources(spState);
                    return;
                }
                spState->uiBitmapWidth = static_cast<UINT32>(iPlotWidth);
                spState->uiBitmapHeight = static_cast<UINT32>(iHeight);
            }

            hRes = spState->bitmap->CopyFromMemory(nullptr, spState->vecPixels.data(),
                                                    static_cast<UINT32>(iPlotWidth) * sizeof(UINT32));
            if (FAILED(hRes))
            {
                ReleaseSpectrogramResources(spState);
                return;
            }
            spState->ullRenderedHistoryVersion = spState->ullSnapshotVersion;
        }

        if (spState->bitmap)
        {
            lpRenderTarget->DrawBitmap(spState->bitmap.Get(),
                                       D2D1::RectF(static_cast<float>(iPlotLeft), 0.0f, static_cast<float>(iWidth),
                                                   static_cast<float>(iHeight)),
                                       1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        }
    }
    catch (...)
    {
        drawContext.Clear(RGB(0U, 0U, 0U));
    }
}

static VOID DrawSpectrogramFrequencyLabels(_In_ GuiTerminal::DrawContext &drawContext,
                                           _In_ const SPECTROGRAM_STATE &sState) noexcept
{
    static constexpr float aReferenceFrequenciesHz[] = { 0.0f, 500.0f, 2000.0f, 8000.0f };
    const INT iHeight = drawContext.GetHeight();
    const float fNyquistHz = sState.fSampleRate * 0.5f;
    const float fMaximumMel = MelFromHz(fNyquistHz);
    float fPreviousY = -1000.0f;

    if (iHeight <= 0 || fNyquistHz <= 0.0f || fMaximumMel <= 0.0f)
    {
        return;
    }

    for (UINT32 ulLabelIndex = 0; ulLabelIndex < _countof(aReferenceFrequenciesHz) + 1; ulLabelIndex++)
    {
        const float fFrequencyHz = (ulLabelIndex < _countof(aReferenceFrequenciesHz)) ?
                                       aReferenceFrequenciesHz[ulLabelIndex] : fNyquistHz;
        const float fMel = MelFromHz(fFrequencyHz);
        float fY;
        WCHAR szLabelW[16];

        if (fFrequencyHz > fNyquistHz || (ulLabelIndex == _countof(aReferenceFrequenciesHz) &&
                                          fFrequencyHz <= aReferenceFrequenciesHz[_countof(aReferenceFrequenciesHz) - 1]))
        {
            continue;
        }

        fY = static_cast<float>(iHeight) * (1.0f - fMel / fMaximumMel);
        if (fY < 4.0f)
        {
            fY = 4.0f;
        }
        else if (fY > static_cast<float>(iHeight) - 4.0f)
        {
            fY = static_cast<float>(iHeight) - 4.0f;
        }
        if (ulLabelIndex == _countof(aReferenceFrequenciesHz))
        {
            fY += 2.0f;
        }
        if (fabsf(fY - fPreviousY) < 11.0f)
        {
            continue;
        }

        if (fFrequencyHz >= 1000.0f)
        {
            const float fKHz = fFrequencyHz / 1000.0f;
            const INT iWholeKHz = static_cast<INT>(fKHz + 0.5f);

            if (fabsf(fKHz - static_cast<float>(iWholeKHz)) < 0.05f)
            {
                swprintf_s(szLabelW, _countof(szLabelW), L"%d kHz", iWholeKHz);
            }
            else
            {
                swprintf_s(szLabelW, _countof(szLabelW), L"%.1f kHz", fKHz);
            }
        }
        else
        {
            swprintf_s(szLabelW, _countof(szLabelW), L"%d Hz", static_cast<INT>(fFrequencyHz + 0.5f));
        }

        drawContext.Write(szLabelW, D2D1::Point2F(SPECTROGRAM_LABEL_GUTTER_WIDTH_DIP - 2.0f, fY), RGB(160U, 160U, 160U),
                          L"Arial", 9.0f, 0U,
                          static_cast<GuiTerminal::DrawContext::TextAlignment>(GuiTerminal::DrawContext::AlignRight |
                                                                                GuiTerminal::DrawContext::AlignMiddle));
        fPreviousY = fY;
    }
}

static VOID FormatBandLabel(_In_ UINT32 ulBandIndex, _Out_writes_z_(4) LPWSTR szLabelW) noexcept
{
    const CAudioAnalyzer::BandDefinition_t *lpBandDefinitions = CAudioAnalyzer::GetBandDefinitions();
    const float fCenterHz = lpBandDefinitions[ulBandIndex].fCenterHz;

    if (fCenterHz >= 1000.0f)
    {
        const INT iWholeKHz = static_cast<INT>(fCenterHz / 1000.0f);
        const INT iTenthKHz = static_cast<INT>(fCenterHz / 100.0f) % 10;

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

static VOID ReleaseSpectrogramResources(_In_ const std::shared_ptr<SPECTROGRAM_STATE> &spState) noexcept
{
    if (!spState)
    {
        return;
    }

    spState->bitmap.Reset();
    spState->uiBitmapWidth = 0;
    spState->uiBitmapHeight = 0;
    spState->ullRenderedHistoryVersion = UINT64_MAX;
}

static VOID SnapshotSpectrogramHistory(_In_ const std::shared_ptr<SPECTROGRAM_STATE> &spState) noexcept
{
    CMutex::CAutoLock lock(spState->cHistoryMutex);
    const UINT32 ulFirstHistoryFrame = (spState->ulNextHistoryFrame + spState->ulHistoryCapacity - spState->ulHistoryCount) %
                                       spState->ulHistoryCapacity;

    for (UINT32 i = 0; i < spState->ulHistoryCount; i++)
    {
        const UINT32 ulHistoryIndex = (ulFirstHistoryFrame + i) % spState->ulHistoryCapacity;

        memcpy(spState->vecSnapshot.data() + static_cast<SIZE_T>(i) * spState->ulFftMagnitudeCount,
               spState->vecHistory.data() + static_cast<SIZE_T>(ulHistoryIndex) * spState->ulFftMagnitudeCount,
               static_cast<SIZE_T>(spState->ulFftMagnitudeCount) * sizeof(float));
        spState->vecSnapshotEndTimeSec[i] = spState->vecHistoryEndTimeSec[ulHistoryIndex];
    }

    spState->ulSnapshotCount = spState->ulHistoryCount;
    spState->ullSnapshotVersion = spState->ullHistoryVersion;
}

static VOID UpdateSpectrogramPixels(_In_ SPECTROGRAM_STATE &sState, _In_ UINT32 uiWidth, _In_ UINT32 uiHeight)
{
    const double dLatestTimeSec = sState.vecSnapshotEndTimeSec[sState.ulSnapshotCount - 1];
    const double dFirstTimeSec = dLatestTimeSec - SPECTROGRAM_HISTORY_DURATION_SEC;
    const float fNyquistHz = sState.fSampleRate * 0.5f;
    const float fMaximumMel = MelFromHz(fNyquistHz);
    const float fBinWidthHz = fNyquistHz / static_cast<float>(sState.ulFftMagnitudeCount - 1);
    UINT32 ulNextFrame = 0;
    INT iSourceFrame = -1;

    sState.vecPixels.assign(static_cast<SIZE_T>(uiWidth) * uiHeight, SPECTROGRAM_BLACK_PIXEL);

    for (UINT32 iX = 0; iX < uiWidth; iX++)
    {
        const double dColumnTimeSec = dFirstTimeSec + (static_cast<double>(iX + 1) * SPECTROGRAM_HISTORY_DURATION_SEC / uiWidth);

        while (ulNextFrame < sState.ulSnapshotCount && sState.vecSnapshotEndTimeSec[ulNextFrame] <= dColumnTimeSec)
        {
            iSourceFrame = static_cast<INT>(ulNextFrame);
            ulNextFrame += 1;
        }
        if (iSourceFrame < 0)
        {
            continue;
        }

        const float *lpfSpectrum = sState.vecSnapshot.data() + static_cast<SIZE_T>(iSourceFrame) * sState.ulFftMagnitudeCount;
        for (UINT32 iY = 0; iY < uiHeight; iY++)
        {
            const float fCenterMel = fMaximumMel * (static_cast<float>(uiHeight - iY) - 0.5f) / uiHeight;
            const float fBinIndex = HzFromMel(fCenterMel) / fBinWidthHz;
            const float fMagnitude = GetInterpolatedSpectrumIntensity(lpfSpectrum, sState.ulFftMagnitudeCount, fBinIndex);
            const float fIntensity = GetSpectrogramIntensity(fMagnitude, sState.fFftMagnitudeFullScale);

            sState.vecPixels[static_cast<SIZE_T>(iY) * uiWidth + iX] = GetSpectrogramPixel(fIntensity);
        }
    }
}

static float GetInterpolatedSpectrumIntensity(_In_reads_(ulFftMagnitudeCount) const float *lpfSpectrum,
                                              _In_ UINT32 ulFftMagnitudeCount, _In_ float fBinIndex) noexcept
{
    UINT32 ulLowerBin;
    float fFraction;

    if (fBinIndex <= 1.0f)
    {
        return lpfSpectrum[1];
    }
    if (fBinIndex >= static_cast<float>(ulFftMagnitudeCount - 1))
    {
        return lpfSpectrum[ulFftMagnitudeCount - 1];
    }

    ulLowerBin = static_cast<UINT32>(floorf(fBinIndex));
    fFraction = fBinIndex - static_cast<float>(ulLowerBin);
    return lpfSpectrum[ulLowerBin] + (lpfSpectrum[ulLowerBin + 1] - lpfSpectrum[ulLowerBin]) * fFraction;
}

static float GetSpectrogramIntensity(_In_ float fMagnitude, _In_ float fFullScaleMagnitude) noexcept
{
    float fDb;

    if (fMagnitude <= SPECTROGRAM_MAGNITUDE_EPSILON || fFullScaleMagnitude <= SPECTROGRAM_MAGNITUDE_EPSILON)
    {
        return 0.0f;
    }

    fDb = 20.0f * log10f(fMagnitude / fFullScaleMagnitude);
    if (fDb <= -SPECTROGRAM_DYNAMIC_RANGE_DB)
    {
        return 0.0f;
    }
    if (fDb >= 0.0f)
    {
        return 1.0f;
    }

    return (fDb + SPECTROGRAM_DYNAMIC_RANGE_DB) / SPECTROGRAM_DYNAMIC_RANGE_DB;
}

static UINT32 GetSpectrogramPixel(_In_ float fIntensity) noexcept
{
    static constexpr float aIntensityStops[] = { 0.0f, 0.33f, 0.66f, 1.0f };
    static constexpr BYTE aRedStops[] = { 0U, 112U, 240U, 255U };
    static constexpr BYTE aGreenStops[] = { 0U, 0U, 32U, 224U };
    static constexpr BYTE aBlueStops[] = { 0U, 160U, 16U, 0U };
    UINT32 ulStopIndex = 0;
    float fFraction;
    BYTE uRed, uGreen, uBlue;

    if (fIntensity <= 0.0f)
    {
        return SPECTROGRAM_BLACK_PIXEL;
    }
    if (fIntensity >= 1.0f)
    {
        return 0xFFFFFF00U;
    }

    while (ulStopIndex + 1 < _countof(aIntensityStops) - 1 && fIntensity > aIntensityStops[ulStopIndex + 1])
    {
        ulStopIndex += 1;
    }

    fFraction = (fIntensity - aIntensityStops[ulStopIndex]) /
                (aIntensityStops[ulStopIndex + 1] - aIntensityStops[ulStopIndex]);
    uRed = static_cast<BYTE>(aRedStops[ulStopIndex] + (aRedStops[ulStopIndex + 1] - aRedStops[ulStopIndex]) * fFraction + 0.5f);
    uGreen = static_cast<BYTE>(aGreenStops[ulStopIndex] + (aGreenStops[ulStopIndex + 1] - aGreenStops[ulStopIndex]) * fFraction + 0.5f);
    uBlue = static_cast<BYTE>(aBlueStops[ulStopIndex] + (aBlueStops[ulStopIndex + 1] - aBlueStops[ulStopIndex]) * fFraction + 0.5f);

    return 0xFF000000U | (static_cast<UINT32>(uRed) << 16) | (static_cast<UINT32>(uGreen) << 8) | uBlue;
}

static float MelFromHz(_In_ float fHz) noexcept
{
    return 2595.0f * log10f(1.0f + fHz / 700.0f);
}

static float HzFromMel(_In_ float fMel) noexcept
{
    return 700.0f * (powf(10.0f, fMel / 2595.0f) - 1.0f);
}
