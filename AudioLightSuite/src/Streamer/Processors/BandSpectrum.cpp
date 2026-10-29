#include "BandSpectrum.h"
#include "TUI\TUiFrameWindow.h"
#include <algorithm>
#include <cmath>
#include <crtdbg.h>
#include <cstring>

// -----------------------------------------------------------------------------

static USHORT InterpolateColorComponent(_In_ USHORT uFirst, _In_ USHORT uSecond, _In_ SIZE_T nNumerator,
                                        _In_ SIZE_T nDenominator) noexcept;

// -----------------------------------------------------------------------------

VOID CBandSpectrumAudioProcessor::FirstRender() noexcept
{
    _ASSERT(m_lpTerminalControl);
    RenderStripPreview();
}

VOID CBandSpectrumAudioProcessor::Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept
{
    CRwMutex::CAutoExclusiveLock lock(m_cCurrentStateRwMutex);

    memcpy(m_sCurrentState.afBandRmsNorm, sFrame.fBandRmsNorm, sizeof(m_sCurrentState.afBandRmsNorm));
}

VOID CBandSpectrumAudioProcessor::TimerTick() noexcept
{
    if (!m_lpTerminalControl)
    {
        return;
    }

    RenderStripPreview();
}

VOID CBandSpectrumAudioProcessor::GetRGBStripColors(_Out_writes_(nColorCount) CBaseAudioProcessor::LPRGB lpsColors,
                                                     _In_ SIZE_T nColorCount) const noexcept
{
    float afBandRmsNorm[CAudioAnalyzer::BAND_COUNT];

    if (!lpsColors || nColorCount == 0)
    {
        return;
    }

    GetBandLevels(afBandRmsNorm);

    for (SIZE_T nColorIndex = 0; nColorIndex < nColorCount; nColorIndex++)
    {
        lpsColors[nColorIndex] = GetStripColor(nColorIndex, nColorCount, afBandRmsNorm);
    }
}

HRESULT CBandSpectrumAudioProcessor::OnPostAttachEvent() noexcept
{
    CTUiFrameWindow *lpFrameWindow = dynamic_cast<CTUiFrameWindow *>(ParentWindow());

    if (lpFrameWindow)
    {
        return lpFrameWindow->SetCaption(L"Band Spectrum Strip Processor");
    }

    return S_OK;
}

CBaseAudioProcessor::RGB CBandSpectrumAudioProcessor::GetStripColor(
    _In_ SIZE_T nColorIndex, _In_ SIZE_T nColorCount,
    _In_reads_(CAudioAnalyzer::BAND_COUNT) const float afBandRmsNorm[]) noexcept
{
    CBaseAudioProcessor::RGB sFirstColor;
    CBaseAudioProcessor::RGB sSecondColor;
    CBaseAudioProcessor::RGB sColor;
    SIZE_T nScaledBandIndex;
    SIZE_T nFirstBandIndex;
    SIZE_T nSecondBandIndex;
    SIZE_T nRemainder;
    const SIZE_T nDenominator = nColorCount - 1;

    if (nColorCount == 1)
    {
        return GetBandColor(0, afBandRmsNorm[0]);
    }

    nScaledBandIndex = nColorIndex * static_cast<SIZE_T>(CAudioAnalyzer::BAND_COUNT - 1);
    nFirstBandIndex = nScaledBandIndex / nDenominator;
    nSecondBandIndex = (std::min)(nFirstBandIndex + 1, static_cast<SIZE_T>(CAudioAnalyzer::BAND_COUNT - 1));
    nRemainder = nScaledBandIndex % nDenominator;

    sFirstColor = GetBandColor(static_cast<UINT32>(nFirstBandIndex), afBandRmsNorm[nFirstBandIndex]);
    sSecondColor = GetBandColor(static_cast<UINT32>(nSecondBandIndex), afBandRmsNorm[nSecondBandIndex]);

    sColor.uRed = InterpolateColorComponent(sFirstColor.uRed, sSecondColor.uRed, nRemainder, nDenominator);
    sColor.uGreen = InterpolateColorComponent(sFirstColor.uGreen, sSecondColor.uGreen, nRemainder, nDenominator);
    sColor.uBlue = InterpolateColorComponent(sFirstColor.uBlue, sSecondColor.uBlue, nRemainder, nDenominator);
    return sColor;
}

// -----------------------------------------------------------------------------

static USHORT InterpolateColorComponent(_In_ USHORT uFirst, _In_ USHORT uSecond, _In_ SIZE_T nNumerator,
                                        _In_ SIZE_T nDenominator) noexcept
{
    const UINT64 ullFirstWeight = nDenominator - nNumerator;
    const UINT64 ullComponent = static_cast<UINT64>(uFirst) * ullFirstWeight +
                              static_cast<UINT64>(uSecond) * nNumerator + nDenominator / 2;

    return static_cast<USHORT>(ullComponent / nDenominator);
}

CBaseAudioProcessor::RGB CBandSpectrumAudioProcessor::GetBandColor(_In_ UINT32 ulBandIndex, _In_ float fLevel) noexcept
{
    CBaseAudioProcessor::RGB sColor;
    float fRed, fGreen, fBlue;
    const float fHueDegrees = static_cast<float>(ulBandIndex) * 280.0f / static_cast<float>(CAudioAnalyzer::BAND_COUNT - 1);
    const float fBrightness = (std::min)(1.0f, (std::max)(0.0f, fLevel));

    HueToRGB(fHueDegrees, fRed, fGreen, fBlue);
    sColor.uRed = static_cast<USHORT>(fRed * fBrightness * static_cast<float>(USHRT_MAX) + 0.5f);
    sColor.uGreen = static_cast<USHORT>(fGreen * fBrightness * static_cast<float>(USHRT_MAX) + 0.5f);
    sColor.uBlue = static_cast<USHORT>(fBlue * fBrightness * static_cast<float>(USHRT_MAX) + 0.5f);
    return sColor;
}

VOID CBandSpectrumAudioProcessor::HueToRGB(_In_ float fHueDegrees, _Out_ float &fRed, _Out_ float &fGreen, _Out_ float &fBlue) noexcept
{
    const float fHueSector = fHueDegrees / 60.0f;
    const UINT32 ulSector = static_cast<UINT32>(floorf(fHueSector)) % 6;
    const float fFraction = fHueSector - floorf(fHueSector);

    switch (ulSector)
    {
        case 0:
            fRed = 1.0f;
            fGreen = fFraction;
            fBlue = 0.0f;
            break;

        case 1:
            fRed = 1.0f - fFraction;
            fGreen = 1.0f;
            fBlue = 0.0f;
            break;

        case 2:
            fRed = 0.0f;
            fGreen = 1.0f;
            fBlue = fFraction;
            break;

        case 3:
            fRed = 0.0f;
            fGreen = 1.0f - fFraction;
            fBlue = 1.0f;
            break;

        case 4:
            fRed = fFraction;
            fGreen = 0.0f;
            fBlue = 1.0f;
            break;

        default:
            fRed = 1.0f;
            fGreen = 0.0f;
            fBlue = 1.0f - fFraction;
            break;
    }
}

VOID CBandSpectrumAudioProcessor::GetBandLevels(_Out_writes_(CAudioAnalyzer::BAND_COUNT) float afBandRmsNorm[]) const noexcept
{
    CRwMutex::CAutoSharedLock lock(m_cCurrentStateRwMutex);

    memcpy(afBandRmsNorm, m_sCurrentState.afBandRmsNorm, sizeof(m_sCurrentState.afBandRmsNorm));
}

VOID CBandSpectrumAudioProcessor::RenderStripPreview() noexcept
{
    float afBandRmsNorm[CAudioAnalyzer::BAND_COUNT];
    const INT iWidth = GetClientWidth();

    if ((!m_lpTerminalControl) || (!GetClientRegionHandle()) || iWidth < 1)
    {
        return;
    }

    GetBandLevels(afBandRmsNorm);
    for (INT iColumn = 0; iColumn < iWidth; iColumn++)
    {
        const CBaseAudioProcessor::RGB sColor = GetStripColor(static_cast<SIZE_T>(iColumn), static_cast<SIZE_T>(iWidth),
                                                               afBandRmsNorm);

        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;%dH\x1b[48;2;%u;%u;%um \x1b[0m", iColumn + 1,
                                         sColor.uRed >> 8, sColor.uGreen >> 8, sColor.uBlue >> 8);
    }
}
