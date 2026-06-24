#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <functional>
#include <GuiTerminalControl.h>
#include "..\ThirdParty\ankerl_unordered_dense\unordered_dense.h"

// -----------------------------------------------------------------------------

class CTUiWindow;

// -----------------------------------------------------------------------------

class CTUiWindowManager
{
public:
    typedef enum class MouseEventType_e {
        LeftButtonDown,
        LeftButtonUp,
        MiddleButtonDown,
        MiddleButtonUp,
        RightButtonDown,
        RightButtonUp,
        X1ButtonDown,
        X1ButtonUp,
        X2ButtonDown,
        X2ButtonUp,
        Move
    } MouseEventType_t;

    typedef struct MouseEvent_s {
        MouseEventType_t nType;
        INT xPos;
        INT yPos;
        INT iCol;
        INT iRow;
        BYTE bControlKeyPressed : 1;
        BYTE bShiftKeyPressed : 1;
        BYTE bMouseLeftButtonPressed : 1;
        BYTE bMouseMiddleButtonPressed : 1;
        BYTE bMouseRightButtonPressed : 1;
        BYTE bMouseX1ButtonPressed : 1;
        BYTE bMouseX2ButtonPressed : 1;
    } MouseEvent_t;

    typedef std::function<VOID (_Inout_ BOOL &/*bContinueClosing*/)> CloseEventCallback_t;
    typedef std::function<VOID ()> DestroyEventCallback_t;
    typedef std::function<VOID (_In_ INT /*iTotalCols*/, _In_ INT /*iTotalRows*/)> ResizeEventCallback_t;
    typedef std::function<VOID (_In_ MouseEvent_t &/*sEvent*/)> MouseEventCallback_t;
    typedef std::function<VOID ()> ReleaseCaptureEventCallback_t;

    typedef struct InitParams_s {
        HINSTANCE hInstance{ nullptr };
        LPCWSTR szCaptionW{ nullptr };
        HICON hIcon{ nullptr };
        HICON hSmallIcon{ nullptr };
        HCURSOR hCursor{ nullptr };
        HMENU hMenu{ nullptr };
        INT iRows{ 25 };
        INT iCols{ 80 };
    } InitParams_t;

public:
    CTUiWindowManager() noexcept = default;
    CTUiWindowManager(_In_ const CTUiWindowManager &) = delete;
    CTUiWindowManager(_Inout_ CTUiWindowManager &&) = delete;
    ~CTUiWindowManager() noexcept;

    CTUiWindowManager &operator=(_In_ const CTUiWindowManager &) = delete;
    CTUiWindowManager &operator=(_Inout_ CTUiWindowManager &&) = delete;

    HRESULT Initialize(_In_ InitParams_t &sInitParams) noexcept;
    VOID Finalize() noexcept;

    HRESULT ProcessMessages(_In_opt_ BOOL bWaitForMessage = TRUE) noexcept;

    HRESULT AttachWindow(_In_ CTUiWindow *lpWindow, _In_ INT iX, _In_ INT iY) noexcept;
    HRESULT AttachWindow(_In_ CTUiWindow *lpWindow, _In_ INT iX, _In_ INT iY, _In_ GuiTerminal::RegionHandle hParentRegion) noexcept;
    HRESULT AttachWindow(_In_ CTUiWindow *lpWindow, _In_ INT iX, _In_ INT iY, _In_ CTUiWindow *lpParentWindow) noexcept;
    HRESULT DetachWindow(_In_ CTUiWindow *lpWindow) noexcept;
    HRESULT GetWindowRegionLocation(_In_ CTUiWindow *lpWindow, _Out_opt_ LPINT lpiX, _Out_opt_ LPINT lpiY, _Out_opt_ LPINT lpiWidth,
                                    _Out_opt_ LPINT lpiHeight) const noexcept;
    HRESULT RelocateWindow(_In_ CTUiWindow *lpWindow, _In_ INT iX, _In_ INT iY) noexcept;
    HRESULT BringWindowToFront(_In_ CTUiWindow *lpWindow) noexcept;

    HRESULT SetMouseCapture(_In_opt_ GuiTerminal::RegionHandle hRegion) noexcept;
    HRESULT ReleaseMouseCapture() noexcept;

    VOID OnClose(_In_ CloseEventCallback_t cCloseEventCallback) noexcept;
    VOID OnDestroy(_In_ DestroyEventCallback_t cDestroyEventCallback) noexcept;
    VOID OnResize(_In_ ResizeEventCallback_t cResizeEventCallback) noexcept;
    VOID OnMouse(_In_ MouseEventCallback_t cMouseEventCallback) noexcept;
    VOID OnReleaseCapture(_In_ ReleaseCaptureEventCallback_t cReleaseCaptureEventCallback) noexcept;

    VOID Show(_In_opt_ WORD wShowWindow = SW_SHOWDEFAULT) noexcept;

    VOID RequestShutdown(_In_ HRESULT hRes) noexcept;

    HINSTANCE GetInstance() noexcept
    {
        return m_hInstance;
    }

    explicit operator HINSTANCE() noexcept
    {
        return m_hInstance;
    }

    HWND GetHwnd() noexcept
    {
        return m_hMainWnd;
    }

    explicit operator HWND() noexcept
    {
        return m_hMainWnd;
    }

    GuiTerminal::Control* Terminal() noexcept
    {
        return m_lpGuiTerminal;
    }

    static HRESULT EnablePerMonitorDpiAwareness() noexcept;

private:
    HRESULT AttachWindowInternal(_In_ CTUiWindow *lpWindow, _In_ INT iX, _In_ INT iY, _In_opt_ GuiTerminal::RegionHandle hParentRegion,
                                 _In_opt_ CTUiWindow *lpParentWindow) noexcept;
    VOID Cleanup() noexcept;
    CTUiWindow *FindWindowByRegion(_In_opt_ GuiTerminal::RegionHandle hRegion) const noexcept;
    static LRESULT CALLBACK StaticWndProc(_In_ HWND hWnd, _In_ UINT uMessage, _In_ WPARAM wParam, _In_ LPARAM lParam) noexcept;
    LRESULT WndProc(_In_ HWND hWnd, _In_ UINT uMessage, _In_ WPARAM wParam, _In_ LPARAM lParam) noexcept;
    static LRESULT HandleNcCreate(_In_ HWND hWnd, _In_ WPARAM wParam, _In_ LPARAM lParam) noexcept;
    LRESULT HandleCreate(_In_ HWND hWnd, _In_ WPARAM wParam, _In_ LPARAM lParam) noexcept;
    LRESULT HandleClose() noexcept;
    LRESULT HandleDestroy() noexcept;
    LRESULT HandleNcDestroy() noexcept;
    LRESULT HandleSize(_In_ WPARAM wParam, _In_ LPARAM lParam) noexcept;
    LRESULT HandleMouseEvent(_In_ UINT uMessage, _In_ WPARAM wParam, _In_ LPARAM lParam) noexcept;
    LRESULT HandleCaptureChanged(_In_ WPARAM wParam, _In_ LPARAM lParam) noexcept;

    HRESULT GetWorkAreaFromHwnd(_In_ HWND hWnd, _Out_ LPRECT lprcWorkArea) noexcept;

private:
    WCHAR m_szWndClassNameW[32]{ 0 };
    HINSTANCE m_hInstance{ nullptr };
    HWND m_hMainWnd{ nullptr };
    GuiTerminal::Control *m_lpGuiTerminal{ nullptr };
    HRESULT m_hrExitCode{ S_OK };
    BOOL m_bMouseCaptured{ FALSE };
    GuiTerminal::RegionHandle m_hMouseCaptureRegion{ nullptr };
    CloseEventCallback_t m_cCloseEventCallback{ nullptr };
    DestroyEventCallback_t m_cDestroyEventCallback{ nullptr };
    ResizeEventCallback_t m_cResizeEventCallback{ nullptr };
    MouseEventCallback_t m_cMouseEventCallback{ nullptr };
    ReleaseCaptureEventCallback_t m_cReleaseCaptureEventCallback{ nullptr };
    ankerl::unordered_dense::map<GuiTerminal::RegionHandle, CTUiWindow*> m_mapRegionWnd;
};
