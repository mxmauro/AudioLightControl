#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GuiTerminalControl.h>
#include "TUiWindowManager.h"

// -----------------------------------------------------------------------------

class CTUiWindow
{
    friend class CTUiWindowManager;
    friend class CTUiFrameWindow;

protected:
    CTUiWindow() noexcept = default;
    CTUiWindow(_In_ const CTUiWindow &) = delete;
    CTUiWindow(_Inout_ CTUiWindow &&) = delete;
public:
    ~CTUiWindow() noexcept;

    CTUiWindow &operator=(_In_ const CTUiWindow &) = delete;
    CTUiWindow &operator=(_Inout_ CTUiWindow &&) = delete;

    explicit operator GuiTerminal::RegionHandle() const noexcept
    {
        return m_regionH;
    };

    GuiTerminal::RegionHandle RegionHandle() const noexcept
    {
        return m_regionH;
    };

    BOOL IsAttached() const noexcept
    {
        return (m_regionH != nullptr) ? TRUE : FALSE;
    };

    explicit operator GuiTerminal::Control *() const noexcept
    {
        return m_lpTerminalControl;
    };

    GuiTerminal::Control* Terminal() const noexcept
    {
        return m_lpTerminalControl;
    };

    explicit operator CTUiWindowManager *() const noexcept
    {
        return m_lpWindowManager;
    };

    CTUiWindowManager *Manager() const noexcept
    {
        return m_lpWindowManager;
    };

    CTUiWindow *ParentWindow() const noexcept
    {
        return m_lpParentWindow;
    };

    GuiTerminal::RegionHandle ParentRegionHandle() const noexcept
    {
        return m_hParentRegion;
    };

    INT GetWidth() const noexcept;
    INT GetHeight() const noexcept;

    virtual GuiTerminal::RegionHandle GetClientRegionHandle() const noexcept
    {
        return m_regionH;
    };

    virtual INT GetClientWidth() const noexcept
    {
        return GetWidth();
    };

    virtual INT GetClientHeight() const noexcept
    {
        return GetHeight();
    };

    virtual HRESULT OnPostAttachEvent() noexcept;
    virtual VOID OnPreDetachEvent() noexcept;

    virtual VOID OnMouseEvent(_In_ CTUiWindowManager::MouseEvent_t &sEvent) noexcept;
    virtual VOID OnReleaseCaptureEvent() noexcept;

    virtual INT GetDefaultWidth() const noexcept = 0;
    virtual INT GetDefaultHeight() const noexcept = 0;

protected:
    virtual HRESULT ValidateAttachSize(_In_ INT iWidth, _In_ INT iHeight) const noexcept;
    VOID SetPendingAttachSize(_In_ INT iWidth, _In_ INT iHeight) noexcept;
    VOID ClearPendingAttachSize() noexcept;
    BOOL HitTest(_In_ INT iCol, _In_ INT iRow, _Out_opt_ INT *lpiLocalCol = nullptr, _Out_opt_ INT *lpiLocalRow = nullptr) const noexcept;

protected:
    CTUiWindowManager *m_lpWindowManager{ nullptr };
    GuiTerminal::Control *m_lpTerminalControl{ nullptr };
    GuiTerminal::RegionHandle m_regionH{ nullptr };
    GuiTerminal::RegionHandle m_hParentRegion{ nullptr };
    CTUiWindow *m_lpParentWindow{ nullptr };
    INT m_iAttachedWidth{ 0 };
    INT m_iAttachedHeight{ 0 };
    INT m_iPendingAttachWidth{ 0 };
    INT m_iPendingAttachHeight{ 0 };
};
