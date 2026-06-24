#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "TUiWindow.h"

// -----------------------------------------------------------------------------

class CTUiFrameWindow : public CTUiWindow
{
public:
    explicit CTUiFrameWindow(_In_ CTUiWindow *lpContentWindow) noexcept;
    CTUiFrameWindow(_In_ const CTUiFrameWindow &) = delete;
    CTUiFrameWindow(_Inout_ CTUiFrameWindow &&) = delete;
    ~CTUiFrameWindow() noexcept = default;

    CTUiFrameWindow &operator=(_In_ const CTUiFrameWindow &) = delete;
    CTUiFrameWindow &operator=(_Inout_ CTUiFrameWindow &&) = delete;

    INT GetDefaultWidth() const noexcept final;
    INT GetDefaultHeight() const noexcept final;

    HRESULT OnPostAttachEvent() noexcept final;
    VOID OnPreDetachEvent() noexcept final;

    HRESULT SetCaption(_In_opt_z_ LPCWSTR szCaptionW) noexcept;
    HRESULT SetBorderStroke(_In_ GuiTerminal::Control::StrokeType strokeType) noexcept;
    HRESULT SetBorderColors(_In_ COLORREF crForeground, _In_ COLORREF crBackground) noexcept;
    HRESULT SetBorderTextStyle(_In_ DWORD dwStyleFlags) noexcept;
    VOID OnMouseEvent(_In_ CTUiWindowManager::MouseEvent_t &sEvent) noexcept final;
    VOID OnReleaseCaptureEvent() noexcept final;

protected:
    HRESULT ValidateAttachSize(_In_ INT iWidth, _In_ INT iHeight) const noexcept final;

private:
    typedef struct DragState_s {
        BOOL bActive{ FALSE };
        INT iOriginX{ 0 };
        INT iOriginY{ 0 };
        INT iCurrentX{ 0 };
        INT iCurrentY{ 0 };
        INT iMouseDownCol{ 0 };
        INT iMouseDownRow{ 0 };
    } DragState_t;

private:
    BOOL IsTopLevelBorderHit(_In_ INT iCol, _In_ INT iRow) const noexcept;
    VOID CancelDrag() noexcept;
    VOID ResetDragState() noexcept;
    VOID RenderFrame() noexcept;

private:
    CTUiWindow *m_lpContentWindow{ nullptr };
    std::wstring m_strCaptionW;
    GuiTerminal::Control::StrokeType m_strokeType{ GuiTerminal::Control::StrokeDoubleLine };
    COLORREF m_crBorderForeground{ RGB(255, 255, 0) };
    COLORREF m_crBorderBackground{ RGB(0, 0, 0) };
    DWORD m_dwBorderTextStyle{ GuiTerminal::Control::StyleNone };
    DragState_t m_sDragState;
};
