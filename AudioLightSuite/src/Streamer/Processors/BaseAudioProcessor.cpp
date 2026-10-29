#include "BaseAudioProcessor.h"
#include "Utils\Exception2HR.h"
#include "Utils\FNV.h"
#include <crtdbg.h>
#include <cwchar>

// -----------------------------------------------------------------------------

static BYTE ScaleOutputLevelToByte(_In_ USHORT uValue) noexcept;
static INT ScalePositionToDegrees(_In_ SHORT iValue) noexcept;
static INT ScaleDimmerToPercent(_In_ USHORT uValue) noexcept;
static VOID RenderSolidOutputBar(_In_ GuiTerminal::Control *lpTerminalControl, _In_ GuiTerminal::RegionHandle hRegion,
                                 _In_ INT iWidth, _In_ INT iRow, _In_z_ LPCWSTR szLabelW, _In_ COLORREF crColor) noexcept;
static VOID RenderCompactWhiteStrobeOutput(_In_ GuiTerminal::Control *lpTerminalControl, _In_ GuiTerminal::RegionHandle hRegion,
                                           _In_ INT iWidth, _In_ INT iRow, _In_ USHORT uWhite, _In_ BOOL bStrobe) noexcept;
static VOID RenderDimmerOutputBar(_In_ GuiTerminal::Control *lpTerminalControl, _In_ GuiTerminal::RegionHandle hRegion,
                                  _In_ INT iWidth, _In_ INT iRow, _In_ USHORT uDimmer) noexcept;

// -----------------------------------------------------------------------------

ULONGLONG CBaseAudioProcessor::GetID() const noexcept
{
    LPCSTR szTypeA = GetType();

    return Hash::Fnv1A64(szTypeA, strlen(szTypeA));
}

VOID CBaseAudioProcessor::TimerTick() noexcept
{
}

INT CBaseAudioProcessor::GetOutputViewHeight() const noexcept
{
    INT iHeight = 0;

    if (HasRGBColor() != FALSE)
    {
        iHeight++;
    }
    if (HasWhiteColor() != FALSE)
    {
        iHeight++;
    }
    if (HasUVColor() != FALSE)
    {
        iHeight++;
    }
    if (HasDimmer() != FALSE)
    {
        iHeight++;
    }
    if (HasPan() != FALSE || HasTilt() != FALSE)
    {
        iHeight++;
    }
    if (HasStrobe() != FALSE && (HasCompactWhiteStrobeOutput() == FALSE || HasWhiteColor() == FALSE))
    {
        iHeight++;
    }

    return iHeight;
}

VOID CBaseAudioProcessor::RenderOutputView() noexcept
{
    const INT iOutputViewHeight = GetOutputViewHeight();
    const INT iWidth = GetClientWidth();
    INT iRow;

    if ((!m_lpTerminalControl) || (!GetClientRegionHandle()) || iWidth < 1 || iOutputViewHeight < 1)
    {
        return;
    }

    iRow = GetClientHeight() - iOutputViewHeight + 1;
    if (iRow < 1)
    {
        return;
    }

    if (HasRGBColor() != FALSE)
    {
        const RGB sColor = GetRGBColor();

        RenderSolidOutputBar(m_lpTerminalControl, GetClientRegionHandle(), iWidth, iRow, L"RGB: ",
                             RGB(ScaleOutputLevelToByte(sColor.uRed), ScaleOutputLevelToByte(sColor.uGreen),
                                 ScaleOutputLevelToByte(sColor.uBlue)));
        iRow++;
    }
    if (HasWhiteColor() != FALSE)
    {
        if (HasCompactWhiteStrobeOutput() != FALSE && HasStrobe() != FALSE)
        {
            RenderCompactWhiteStrobeOutput(m_lpTerminalControl, GetClientRegionHandle(), iWidth, iRow, GetWhiteColor(), GetStrobe());
        }
        else
        {
            const BYTE byWhite = ScaleOutputLevelToByte(GetWhiteColor());

            RenderSolidOutputBar(m_lpTerminalControl, GetClientRegionHandle(), iWidth, iRow, L"White: ",
                                 RGB(byWhite, byWhite, byWhite));
        }
        iRow++;
    }
    if (HasUVColor() != FALSE)
    {
        const BYTE byUV = ScaleOutputLevelToByte(GetUVColor());
        const BYTE byRed = static_cast<BYTE>((static_cast<UINT>(byUV) * 192U + 127U) / 255U);
        const BYTE byGreen = static_cast<BYTE>((static_cast<UINT>(byUV) * 128U + 127U) / 255U);

        RenderSolidOutputBar(m_lpTerminalControl, GetClientRegionHandle(), iWidth, iRow, L"UV: ", RGB(byRed, byGreen, byUV));
        iRow++;
    }
    if (HasDimmer() != FALSE)
    {
        RenderDimmerOutputBar(m_lpTerminalControl, GetClientRegionHandle(), iWidth, iRow, GetDimmer());
        iRow++;
    }
    if (HasPan() != FALSE || HasTilt() != FALSE)
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H%*ls", iRow, iWidth, L"");
        if (HasPan() != FALSE && HasTilt() != FALSE)
        {
            m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1HPan: %4d\xb0  Tilt: %4d\xb0", iRow,
                                             ScalePositionToDegrees(GetPan()), ScalePositionToDegrees(GetTilt()));
        }
        else if (HasPan() != FALSE)
        {
            m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1HPan: %4d\xb0", iRow,
                                             ScalePositionToDegrees(GetPan()));
        }
        else
        {
            m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1HTilt: %4d\xb0", iRow,
                                             ScalePositionToDegrees(GetTilt()));
        }
        iRow++;
    }
    if (HasStrobe() != FALSE && (HasCompactWhiteStrobeOutput() == FALSE || HasWhiteColor() == FALSE))
    {
        const COLORREF crStrobe = (GetStrobe() != FALSE) ? RGB(255, 255, 255) : RGB(0, 0, 0);

        RenderSolidOutputBar(m_lpTerminalControl, GetClientRegionHandle(), iWidth, iRow, L"Strobe: ", crStrobe);
    }
}

HRESULT CAudioProcessors::Add(_In_ const std::shared_ptr<CBaseAudioProcessor> &cAudioProcessor) noexcept
{
    _ASSERT(cAudioProcessor);

    return TranslateToHresult([&]()
    {
        m_vecAll.push_back(cAudioProcessor);
    });
}

CBaseAudioProcessor *CAudioProcessors::FindByType(_In_z_ LPCSTR szTypeA) const noexcept
{
    if (!szTypeA)
    {
        return nullptr;
    }

    for (const std::shared_ptr<CBaseAudioProcessor> &cAudioProcessor : m_vecAll)
    {
        if (strcmp(cAudioProcessor->GetType(), szTypeA) == 0)
        {
            return cAudioProcessor.get();
        }
    }

    return nullptr;
}

VOID CAudioProcessors::Clear() noexcept
{
    m_vecAll.clear();
}

// -----------------------------------------------------------------------------

static BYTE ScaleOutputLevelToByte(_In_ USHORT uValue) noexcept
{
    return static_cast<BYTE>((static_cast<UINT32>(uValue) * 255U + 32767U) / 65535U);
}

static INT ScalePositionToDegrees(_In_ SHORT iValue) noexcept
{
    return ((static_cast<INT>(iValue) + 32768) * 360 + 32767) / 65535 - 180;
}

static INT ScaleDimmerToPercent(_In_ USHORT uValue) noexcept
{
    return static_cast<INT>((static_cast<UINT32>(uValue) * 100U + 32767U) / 65535U);
}

static VOID RenderSolidOutputBar(_In_ GuiTerminal::Control *lpTerminalControl, _In_ GuiTerminal::RegionHandle hRegion,
                                 _In_ INT iWidth, _In_ INT iRow, _In_z_ LPCWSTR szLabelW, _In_ COLORREF crColor) noexcept
{
    const INT iLabelWidth = static_cast<INT>(wcslen(szLabelW));

    lpTerminalControl->PrintRegion(hRegion, L"\x1b[%d;1H%*ls", iRow, iWidth, L"");
    if (iWidth <= iLabelWidth)
    {
        lpTerminalControl->PrintRegion(hRegion, L"\x1b[%d;1H\x1b[97m%.*ls\x1b[0m", iRow, iWidth, szLabelW);
        return;
    }

    lpTerminalControl->PrintRegion(hRegion, L"\x1b[%d;1H\x1b[97m%ls\x1b[48;2;%u;%u;%um%*ls\x1b[0m", iRow, szLabelW,
                                    GetRValue(crColor), GetGValue(crColor), GetBValue(crColor), iWidth - iLabelWidth, L"");
}

static VOID RenderCompactWhiteStrobeOutput(_In_ GuiTerminal::Control *lpTerminalControl, _In_ GuiTerminal::RegionHandle hRegion,
                                           _In_ INT iWidth, _In_ INT iRow, _In_ USHORT uWhite, _In_ BOOL bStrobe) noexcept
{
    const BYTE byWhite = ScaleOutputLevelToByte(uWhite);
    const BYTE byStrobe = (bStrobe != FALSE) ? 255 : 0;
    const INT iLabelsWidth = 17;
    const INT iAvailableBarWidth = (iWidth > iLabelsWidth) ? iWidth - iLabelsWidth : 0;
    const INT iWhiteBarWidth = iAvailableBarWidth / 2;
    const INT iStrobeBarWidth = iAvailableBarWidth - iWhiteBarWidth;

    lpTerminalControl->PrintRegion(hRegion, L"\x1b[%d;1H%*ls", iRow, iWidth, L"");
    lpTerminalControl->PrintRegion(hRegion,
                                   L"\x1b[%d;1H\x1b[97mWhite: \x1b[48;2;%u;%u;%um%*ls\x1b[0m  "
                                   L"\x1b[97mStrobe: \x1b[48;2;%u;%u;%um%*ls\x1b[0m",
                                   iRow, byWhite, byWhite, byWhite, iWhiteBarWidth, L"", byStrobe, byStrobe, byStrobe,
                                   iStrobeBarWidth, L"");
}

static VOID RenderDimmerOutputBar(_In_ GuiTerminal::Control *lpTerminalControl, _In_ GuiTerminal::RegionHandle hRegion,
                                  _In_ INT iWidth, _In_ INT iRow, _In_ USHORT uDimmer) noexcept
{
    const INT iPercent = ScaleDimmerToPercent(uDimmer);
    const INT iLabelWidth = 13;
    INT iBarWidth;
    INT iFilledWidth;

    lpTerminalControl->PrintRegion(hRegion, L"\x1b[%d;1H%*ls", iRow, iWidth, L"");
    if (iWidth <= iLabelWidth)
    {
        lpTerminalControl->PrintRegion(hRegion, L"\x1b[%d;1H\x1b[97mDimmer: %3d%%\x1b[0m", iRow, iPercent);
        return;
    }

    iBarWidth = iWidth - iLabelWidth;
    iFilledWidth = (iBarWidth * iPercent + 50) / 100;
    lpTerminalControl->PrintRegion(hRegion,
                                   L"\x1b[%d;1H\x1b[97mDimmer: %3d%% \x1b[48;2;255;215;0m%*ls\x1b[48;2;48;48;48m%*ls\x1b[0m",
                                   iRow, iPercent, iFilledWidth, L"", iBarWidth - iFilledWidth, L"");
}
