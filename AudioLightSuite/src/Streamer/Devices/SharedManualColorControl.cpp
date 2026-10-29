#include "SharedManualColorControl.h"
#include "TUI\TUiFrameWindow.h"
#include "Utils\FNV.h"
#include <algorithm>

// -----------------------------------------------------------------------------

static const BYTE aFixedColorButtonsPalette[6][4][3] = {
    { { 0xFF, 0x00, 0x00 }, { 0x00, 0xFF, 0x00 }, { 0x00, 0x00, 0xFF }, { 0xFF, 0xFF, 0xFF } },
    { { 0xFF, 0x5A, 0x00 }, { 0x7F, 0xFF, 0x00 }, { 0x00, 0x80, 0xFF }, { 0xFF, 0x66, 0xCC } },
    { { 0xFF, 0xAA, 0x00 }, { 0xCC, 0xFF, 0x00 }, { 0x00, 0xD5, 0xFF }, { 0xCC, 0x00, 0xFF } },
    { { 0xFF, 0xD5, 0x00 }, { 0x00, 0xFF, 0xAA }, { 0x00, 0x66, 0xFF }, { 0x80, 0x00, 0xFF } },
    { { 0xAA, 0x55, 0x00 }, { 0x00, 0xAA, 0x55 }, { 0x00, 0x40, 0xAA }, { 0xAA, 0x00, 0xAA } },
    { { 0x55, 0x22, 0x00 }, { 0x00, 0x55, 0x22 }, { 0x00, 0x11, 0x44 }, { 0x44, 0x00, 0x44 } },
};

// -----------------------------------------------------------------------------

static constexpr INT FIXED_BUTTONS_ROWS = 6;
static constexpr INT FIXED_BUTTONS_COLS = 4;
static constexpr INT PALETTE_BUTTON_HEIGHT = 2;
static constexpr INT PALETTE_BUTTON_WIDTH = 5;
static constexpr INT PALETTE_ROW_STEP = 3;
static constexpr INT PALETTE_COL_STEP = 6;
static constexpr INT PALETTE_TOP_ROW = 2;
static constexpr INT PALETTE_LEFT_COL = 1;
static constexpr INT BRIGHTNESS_PLUS_TOP_ROW = 2;
static constexpr INT BRIGHTNESS_MINUS_TOP_ROW = 6;
static constexpr INT AUTOMATIC_TOP_ROW = 16;
static constexpr INT SIDE_BUTTON_LEFT_COL = 25;
static constexpr INT SIDE_BUTTON_WIDTH = 5;
static constexpr INT SIDE_BUTTON_HEIGHT = 3;
static constexpr INT DEVICE_HEADER_ROW = 20;
static constexpr INT DEVICE_LIST_TOP_ROW = 21;
static constexpr INT DEFAULT_WINDOW_WIDTH = 31;
static constexpr INT MAX_WINDOW_WIDTH = 60;

// -----------------------------------------------------------------------------

CSharedManualColorControlWindow::CSharedManualColorControlWindow(_In_ const std::vector<CBaseIotDevice *> &vecDevices)
{
    m_vecDevices.reserve(vecDevices.size());
    for (CBaseIotDevice *lpDevice : vecDevices)
    {
        if (lpDevice)
        {
            m_vecDevices.push_back({ lpDevice, TRUE });
        }
    }
}

ULONGLONG CSharedManualColorControlWindow::GetID() const noexcept
{
    static constexpr CHAR WINDOW_ID_A[] = "SharedManualColorControl";

    return Hash::Fnv1A64(WINDOW_ID_A, ARRAYSIZE(WINDOW_ID_A) - 1);
}

VOID CSharedManualColorControlWindow::FirstRender() noexcept
{
    RenderPalette();
    RenderDeviceHeader();

    for (INT iDevice = 0; iDevice < static_cast<INT>(m_vecDevices.size()); iDevice++)
    {
        RenderDeviceLine(iDevice);
    }
}

INT CSharedManualColorControlWindow::GetDefaultWidth() const noexcept
{
    INT iWidth = DEFAULT_WINDOW_WIDTH;

    for (const DeviceEntry_t &sDeviceEntry : m_vecDevices)
    {
        if (sDeviceEntry.lpDevice && sDeviceEntry.lpDevice->GetDeviceName())
        {
            INT iCandidateWidth = 5 + static_cast<INT>(wcslen(sDeviceEntry.lpDevice->GetDeviceName()));

            if (iCandidateWidth > iWidth)
            {
                iWidth = iCandidateWidth;
            }
        }
    }

    if (iWidth > MAX_WINDOW_WIDTH)
    {
        iWidth = MAX_WINDOW_WIDTH;
    }

    return iWidth;
}

INT CSharedManualColorControlWindow::GetDefaultHeight() const noexcept
{
    return DEVICE_LIST_TOP_ROW - 1 + std::max<INT>(static_cast<INT>(m_vecDevices.size()), 1) + 2;
}

HRESULT CSharedManualColorControlWindow::OnPostAttachEvent() noexcept
{
    CTUiFrameWindow *lpFrameWindow;
    HRESULT hRes;

    lpFrameWindow = dynamic_cast<CTUiFrameWindow *>(ParentWindow());
    if (lpFrameWindow)
    {
        hRes = lpFrameWindow->SetCaption(L"Color Control Override");
        if (FAILED(hRes))
        {
            return hRes;
        }
    }

    return S_OK;
}

VOID CSharedManualColorControlWindow::OnMouseEvent(_In_ CTUiWindowManager::MouseEvent_t &sEvent) noexcept
{
    ManualColorAction_t sAction;
    INT iLocalCol, iLocalRow;
    INT iHitCol, iHitRow;

    if (sEvent.nType != CTUiWindowManager::MouseEventType_t::LeftButtonDown)
    {
        return;
    }
    if (!HitTest(sEvent.iCol, sEvent.iRow, &iLocalCol, &iLocalRow))
    {
        return;
    }

    iHitCol = iLocalCol + 1;
    iHitRow = iLocalRow + 1;

    for (INT iRow = 0; iRow < FIXED_BUTTONS_ROWS; iRow++)
    {
        for (INT iCol = 0; iCol < FIXED_BUTTONS_COLS; iCol++)
        {
            if (iHitRow >= PALETTE_TOP_ROW + iRow * PALETTE_ROW_STEP &&
                iHitRow < PALETTE_TOP_ROW + iRow * PALETTE_ROW_STEP + PALETTE_BUTTON_HEIGHT &&
                iHitCol >= PALETTE_LEFT_COL + iCol * PALETTE_COL_STEP &&
                iHitCol < PALETTE_LEFT_COL + iCol * PALETTE_COL_STEP + PALETTE_BUTTON_WIDTH)
            {
                sAction.eType = ManualColorActionType_t::SetFixedPaletteColor;
                sAction.uRed = aFixedColorButtonsPalette[iRow][iCol][0];
                sAction.uGreen = aFixedColorButtonsPalette[iRow][iCol][1];
                sAction.uBlue = aFixedColorButtonsPalette[iRow][iCol][2];

                BroadcastAction(sAction);
                return;
            }
        }
    }

    if (iHitRow >= BRIGHTNESS_PLUS_TOP_ROW && iHitRow < BRIGHTNESS_PLUS_TOP_ROW + SIDE_BUTTON_HEIGHT &&
        iHitCol >= SIDE_BUTTON_LEFT_COL && iHitCol < SIDE_BUTTON_LEFT_COL + SIDE_BUTTON_WIDTH)
    {
        sAction.eType = ManualColorActionType_t::IncreaseBrightness;
        BroadcastAction(sAction);
        return;
    }
    if (iHitRow >= BRIGHTNESS_MINUS_TOP_ROW && iHitRow < BRIGHTNESS_MINUS_TOP_ROW + SIDE_BUTTON_HEIGHT &&
        iHitCol >= SIDE_BUTTON_LEFT_COL && iHitCol < SIDE_BUTTON_LEFT_COL + SIDE_BUTTON_WIDTH)
    {
        sAction.eType = ManualColorActionType_t::DecreaseBrightness;
        BroadcastAction(sAction);
        return;
    }
    if (iHitRow >= AUTOMATIC_TOP_ROW && iHitRow < AUTOMATIC_TOP_ROW + SIDE_BUTTON_HEIGHT &&
        iHitCol >= SIDE_BUTTON_LEFT_COL && iHitCol < SIDE_BUTTON_LEFT_COL + SIDE_BUTTON_WIDTH)
    {
        sAction.eType = ManualColorActionType_t::ReturnToAutomaticMode;
        BroadcastAction(sAction);
        return;
    }

    if (iHitRow >= DEVICE_LIST_TOP_ROW)
    {
        INT iDeviceIndex = iHitRow - DEVICE_LIST_TOP_ROW;

        if (iDeviceIndex >= 0 && iDeviceIndex < static_cast<INT>(m_vecDevices.size()) && iHitCol >= 1 && iHitCol <= GetClientWidth())
        {
            m_vecDevices[iDeviceIndex].bChecked = (m_vecDevices[iDeviceIndex].bChecked == FALSE) ? TRUE : FALSE;
            RenderDeviceLine(iDeviceIndex);
        }
    }
}

VOID CSharedManualColorControlWindow::RenderPalette() noexcept
{
    for (INT iRow = 0; iRow < FIXED_BUTTONS_ROWS; iRow++)
    {
        for (INT h = 0; h < PALETTE_BUTTON_HEIGHT; h++)
        {
            for (INT iCol = 0; iCol < FIXED_BUTTONS_COLS; iCol++)
            {
                m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;%dH\x1b[48;2;%u;%u;%um     \x1b[0m",
                                                 PALETTE_TOP_ROW + iRow * PALETTE_ROW_STEP + h, PALETTE_LEFT_COL + PALETTE_COL_STEP * iCol,
                                                 aFixedColorButtonsPalette[iRow][iCol][0], aFixedColorButtonsPalette[iRow][iCol][1],
                                                 aFixedColorButtonsPalette[iRow][iCol][2]);
            }
        }
    }

    for (INT h = 0; h < 2; h++)
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;25H\x1b[48;2;192;192;192m     \x1b[0m", 2 + h * 2);
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;25H\x1b[48;2;192;192;192m     \x1b[0m", 6 + h * 2);
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;25H\x1b[48;2;192;192;192m     \x1b[0m", 16 + h * 2);
    }
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[3;25H\x1b[48;2;192;192;192m\x1b[38;2;0;0;0m  +  \x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[7;25H\x1b[48;2;192;192;192m\x1b[38;2;0;0;0m  -  \x1b[0m");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[17;25H\x1b[48;2;192;192;192m\x1b[38;2;0;0;0m Aut \x1b[0m");
}

VOID CSharedManualColorControlWindow::RenderDeviceHeader() noexcept
{
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H%*ls", DEVICE_HEADER_ROW, GetClientWidth(), L"");
    if (m_vecDevices.empty())
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H\x1b[94mDevices:\x1b[0m none", DEVICE_HEADER_ROW);
    }
    else
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H\x1b[94mDevices:\x1b[0m", DEVICE_HEADER_ROW);
    }
}

VOID CSharedManualColorControlWindow::RenderDeviceLine(_In_ INT iDeviceIndex) noexcept
{
    const DeviceEntry_t &sDeviceEntry = m_vecDevices[iDeviceIndex];
    INT iRow = DEVICE_LIST_TOP_ROW + iDeviceIndex;
    LPCWSTR szDeviceNameW = (sDeviceEntry.lpDevice && sDeviceEntry.lpDevice->GetDeviceName()) ? sDeviceEntry.lpDevice->GetDeviceName()
                                                                                              : L"(Unnamed)";
    INT iMaxChars = GetClientWidth() - 5;

    if (iMaxChars < 0)
    {
        iMaxChars = 0;
    }

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H%*ls", iRow, GetClientWidth(), L"");
    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[%d;1H[%lc] %.*ls", iRow,
                                     ((sDeviceEntry.bChecked != FALSE) ? L'X' : L' '), iMaxChars, szDeviceNameW);
}

VOID CSharedManualColorControlWindow::BroadcastAction(_In_ const ManualColorAction_t &sAction) noexcept
{
    for (const DeviceEntry_t &sDeviceEntry : m_vecDevices)
    {
        if (sDeviceEntry.bChecked != FALSE && sDeviceEntry.lpDevice)
        {
            sDeviceEntry.lpDevice->OnManualColorAction(sAction);
        }
    }
}
