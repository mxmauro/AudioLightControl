#include "TUiFrameWindow.h"
#include "..\Utils\PersistantSettings.h"

// -----------------------------------------------------------------------------

CTUiFrameWindow::CTUiFrameWindow(_In_ CTUiWindow *lpContentWindow) noexcept :m_lpContentWindow(lpContentWindow)
{
}

INT CTUiFrameWindow::GetDefaultWidth() const noexcept
{
    return (m_lpContentWindow) ? m_lpContentWindow->GetDefaultWidth() : 0;
}

INT CTUiFrameWindow::GetDefaultHeight() const noexcept
{
    return (m_lpContentWindow) ? m_lpContentWindow->GetDefaultHeight() : 0;
}

HRESULT CTUiFrameWindow::OnPostAttachEvent() noexcept
{
    HRESULT hRes;

    if (!m_lpContentWindow)
    {
        return E_UNEXPECTED;
    }

    m_lpContentWindow->SetPendingAttachSize(GetWidth() - 2, GetHeight() - 2);
    hRes = m_lpWindowManager->AttachWindow(m_lpContentWindow, 1, 1, this);
    m_lpContentWindow->ClearPendingAttachSize();
    if (FAILED(hRes))
    {
        return hRes;
    }

    RenderFrame();
    return S_OK;
}

VOID CTUiFrameWindow::OnPreDetachEvent() noexcept
{
    if (m_lpContentWindow && m_lpContentWindow->Manager() == m_lpWindowManager && m_lpContentWindow->IsAttached())
    {
        m_lpWindowManager->DetachWindow(m_lpContentWindow);
    }
}

HRESULT CTUiFrameWindow::SetCaption(_In_opt_z_ LPCWSTR szCaptionW) noexcept
{
    try
    {
        m_strCaptionW = (szCaptionW) ? szCaptionW : L"";
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }

    RenderFrame();
    return S_OK;
}

HRESULT CTUiFrameWindow::SetBorderStroke(_In_ GuiTerminal::Control::StrokeType strokeType) noexcept
{
    m_strokeType = strokeType;
    RenderFrame();
    return S_OK;
}

HRESULT CTUiFrameWindow::SetBorderColors(_In_ COLORREF crForeground, _In_ COLORREF crBackground) noexcept
{
    m_crBorderForeground = crForeground;
    m_crBorderBackground = crBackground;
    RenderFrame();
    return S_OK;
}

HRESULT CTUiFrameWindow::SetBorderTextStyle(_In_ DWORD dwStyleFlags) noexcept
{
    m_dwBorderTextStyle = dwStyleFlags;
    RenderFrame();
    return S_OK;
}

HRESULT CTUiFrameWindow::ValidateAttachSize(_In_ INT iWidth, _In_ INT iHeight) const noexcept
{
    if (iWidth < 3 || iHeight < 3)
    {
        return E_INVALIDARG;
    }
    return S_OK;
}

VOID CTUiFrameWindow::OnMouseEvent(_In_ CTUiWindowManager::MouseEvent_t &sEvent) noexcept
{
    HRESULT hRes;

    switch (sEvent.nType)
    {
        case CTUiWindowManager::MouseEventType_t::LeftButtonDown:
            if (!IsTopLevelBorderHit(sEvent.iCol, sEvent.iRow))
            {
                return;
            }

            hRes = m_lpWindowManager->GetWindowRegionLocation(this, &m_sDragState.iOriginX, &m_sDragState.iOriginY, nullptr, nullptr);
            if (FAILED(hRes))
            {
                return;
            }

            m_sDragState.bActive = TRUE;
            m_sDragState.iCurrentX = m_sDragState.iOriginX;
            m_sDragState.iCurrentY = m_sDragState.iOriginY;
            m_sDragState.iMouseDownCol = sEvent.iCol;
            m_sDragState.iMouseDownRow = sEvent.iRow;

            m_lpWindowManager->BringWindowToFront(this);
            if (FAILED(m_lpWindowManager->SetMouseCapture(m_regionH)))
            {
                ResetDragState();
            }
            return;

        case CTUiWindowManager::MouseEventType_t::Move:
            if (m_sDragState.bActive == FALSE)
            {
                return;
            }

            m_sDragState.iCurrentX = m_sDragState.iOriginX + (sEvent.iCol - m_sDragState.iMouseDownCol);
            m_sDragState.iCurrentY = m_sDragState.iOriginY + (sEvent.iRow - m_sDragState.iMouseDownRow);
            if (FAILED(m_lpWindowManager->RelocateWindow(this, m_sDragState.iCurrentX, m_sDragState.iCurrentY)))
            {
                CancelDrag();
            }
            return;

        case CTUiWindowManager::MouseEventType_t::LeftButtonUp:
            if (m_sDragState.bActive == FALSE)
            {
                return;
            }

            m_lpWindowManager->ReleaseMouseCapture();

            {
                INT iTotalCols, iTotalRows;
                const INT iCurrentX = m_sDragState.iCurrentX;
                const INT iCurrentY = m_sDragState.iCurrentY;
                const INT iOriginX = m_sDragState.iOriginX;
                const INT iOriginY = m_sDragState.iOriginY;
                const INT iWidth = GetWidth();
                const INT iHeight = GetHeight();

                m_lpTerminalControl->GetTerminalSize(&iTotalCols, &iTotalRows);

                if (iCurrentX < 0 || iCurrentY < 0 || (iCurrentX + iWidth) > iTotalCols || (iCurrentY + iHeight) > iTotalRows)
                {
                    m_lpWindowManager->RelocateWindow(this, iOriginX, iOriginY);
                }
                else
                {
                    CPersistantSettings *lpPersistantSettings = dynamic_cast<CPersistantSettings *>(m_lpContentWindow);

                    if (lpPersistantSettings)
                    {
                        lpPersistantSettings->SaveWindowPosition(iCurrentX, iCurrentY);
                    }
                }
            }

            ResetDragState();
            return;
    }
}

VOID CTUiFrameWindow::OnReleaseCaptureEvent() noexcept
{
    CancelDrag();
}

BOOL CTUiFrameWindow::IsTopLevelBorderHit(_In_ INT iCol, _In_ INT iRow) const noexcept
{
    INT iLocalCol, iLocalRow;

    if (ParentWindow())
    {
        return FALSE;
    }

    if (!HitTest(iCol, iRow, &iLocalCol, &iLocalRow))
    {
        return FALSE;
    }

    if (iLocalCol > 0 && iLocalCol < (GetWidth() - 1) && iLocalRow > 0 && iLocalRow < (GetHeight() - 1))
    {
        return FALSE;
    }
    return TRUE;
}

VOID CTUiFrameWindow::CancelDrag() noexcept
{
    if (m_sDragState.bActive != FALSE && m_lpWindowManager && IsAttached())
    {
        m_lpWindowManager->RelocateWindow(this, m_sDragState.iOriginX, m_sDragState.iOriginY);
    }
    ResetDragState();
}

VOID CTUiFrameWindow::ResetDragState() noexcept
{
    m_sDragState = DragState_t{};
}

VOID CTUiFrameWindow::RenderFrame() noexcept
{
    size_t nCaptionLen;

    if ((!m_lpTerminalControl) || (!m_regionH))
    {
        return;
    }

    m_lpTerminalControl->DrawRegionBox(m_regionH, 0, 0, GetWidth(), GetHeight(), m_strokeType, m_crBorderForeground, m_crBorderBackground,
                                       m_dwBorderTextStyle);

    nCaptionLen = m_strCaptionW.length();
    if (nCaptionLen == 0 || GetWidth() < 3)
    {
        return;
    }

    if (nCaptionLen <= static_cast<size_t>(GetWidth() - 2))
    {
        m_lpTerminalControl->PrintRegion(m_regionH, L"\x1b[1;%dH\x1b[97;44m%s\x1b[0m",
                                         2 + (GetWidth() - 2 - static_cast<INT>(nCaptionLen)) / 2,
                                         m_strCaptionW.c_str());
    }
    else
    {
        m_lpTerminalControl->PrintRegion(m_regionH, L"\x1b[1;2H\x1b[97;44m%.*s\x1b[0m", GetWidth() - 2, m_strCaptionW.c_str());
    }
}
