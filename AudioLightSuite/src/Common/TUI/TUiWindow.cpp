#include "TUiWindow.h"

// -----------------------------------------------------------------------------

CTUiWindow::~CTUiWindow() noexcept
{
    if (m_lpWindowManager)
    {
        m_lpWindowManager->DetachWindow(this);
    }
}

HRESULT CTUiWindow::OnPostAttachEvent() noexcept
{
    return S_OK;
}

INT CTUiWindow::GetWidth() const noexcept
{
    if (m_iAttachedWidth > 0)
    {
        return m_iAttachedWidth;
    }
    if (m_iPendingAttachWidth > 0)
    {
        return m_iPendingAttachWidth;
    }
    return GetDefaultWidth();
}

INT CTUiWindow::GetHeight() const noexcept
{
    if (m_iAttachedHeight > 0)
    {
        return m_iAttachedHeight;
    }
    if (m_iPendingAttachHeight > 0)
    {
        return m_iPendingAttachHeight;
    }
    return GetDefaultHeight();
}

VOID CTUiWindow::OnPreDetachEvent() noexcept
{
}

VOID CTUiWindow::OnMouseEvent(_In_ CTUiWindowManager::MouseEvent_t &sEvent) noexcept
{
}

VOID CTUiWindow::OnReleaseCaptureEvent() noexcept
{
}

HRESULT CTUiWindow::ValidateAttachSize(_In_ INT iWidth, _In_ INT iHeight) const noexcept
{
    if (iWidth < 1 || iHeight < 1)
    {
        return E_INVALIDARG;
    }
    return S_OK;
}

VOID CTUiWindow::SetPendingAttachSize(_In_ INT iWidth, _In_ INT iHeight) noexcept
{
    m_iPendingAttachWidth = iWidth;
    m_iPendingAttachHeight = iHeight;
}

VOID CTUiWindow::ClearPendingAttachSize() noexcept
{
    m_iPendingAttachWidth = 0;
    m_iPendingAttachHeight = 0;
}

BOOL CTUiWindow::HitTest(_In_ INT iCol, _In_ INT iRow, _Out_opt_ INT *lpiLocalCol, _Out_opt_ INT *lpiLocalRow) const noexcept
{
    INT iLocalCol, iLocalRow;

    if ((!m_lpTerminalControl) || (!m_regionH))
    {
        if (lpiLocalCol)
        {
            *lpiLocalCol = INT_MIN;
        }
        if (lpiLocalRow)
        {
            *lpiLocalRow = INT_MIN;
        }
        return FALSE;
    }

    m_lpTerminalControl->ConvertToRegionCoordinates(m_regionH, iCol, iRow, &iLocalCol, &iLocalRow);

    if (lpiLocalCol)
    {
        *lpiLocalCol = iLocalCol;
    }
    if (lpiLocalRow)
    {
        *lpiLocalRow = iLocalRow;
    }

    if (iLocalCol < 0 || iLocalRow < 0 || iLocalCol >= GetWidth() || iLocalRow >= GetHeight())
    {
        return FALSE;
    }
    return TRUE;
}
