#include "TUiWindowManager.h"
#include "TUiWindow.h"
#include <windowsx.h>

#define OFFSET_TO_CTUIWINDOWMANAGER 0

// -----------------------------------------------------------------------------

typedef struct _CREATE_MAIN_WINDOW_PARAMS {
    CTUiWindowManager *lpThis;
    CTUiWindowManager::InitParams_t *lpInitParams;
    HRESULT hRes;
} CREATE_MAIN_WINDOW_PARAMS;

// -----------------------------------------------------------------------------

CTUiWindowManager::~CTUiWindowManager() noexcept
{
    Finalize();
}

HRESULT CTUiWindowManager::Initialize(_In_ InitParams_t &sInitParams) noexcept
{
    WNDCLASSEXW sWcExW;
    CREATE_MAIN_WINDOW_PARAMS sCwParams;
    HRESULT hRes;

    if ((!sInitParams.hInstance) || (!sInitParams.szCaptionW) || sInitParams.iRows < 1 || sInitParams.iCols < 1)
    {
        return E_INVALIDARG;
    }

    Finalize();

    // Register window class
    memset(&sWcExW, 0, sizeof(sWcExW));
    sWcExW.cbSize = sizeof(sWcExW);
    sWcExW.style = CS_HREDRAW | CS_VREDRAW;
    sWcExW.lpfnWndProc = &CTUiWindowManager::StaticWndProc;
    sWcExW.hInstance = sInitParams.hInstance;
    sWcExW.hIcon = (sInitParams.hIcon) ? sInitParams.hIcon : (HICON)LoadImageW(nullptr, IDI_WINLOGO, IMAGE_ICON, 256, 256,
                                                                               LR_DEFAULTCOLOR);
    sWcExW.hIconSm = (sInitParams.hSmallIcon) ? sInitParams.hSmallIcon : (HICON)LoadImageW(nullptr, IDI_WINLOGO, IMAGE_ICON, 32, 32,
                                                                                           LR_DEFAULTCOLOR);
    sWcExW.hCursor = (sInitParams.hCursor) ? sInitParams.hCursor : LoadCursorW(nullptr, IDC_ARROW);
    swprintf_s(m_szWndClassNameW, _countof(m_szWndClassNameW), L"TUiWndMgrClass%08X", (DWORD)(::GetTickCount64() & 0xFFFFFFFFULL));
    sWcExW.lpszClassName = m_szWndClassNameW;
    sWcExW.cbWndExtra = static_cast<int>(sizeof(CTUiWindowManager *));
    if (RegisterClassExW(&sWcExW) == FALSE)
    {
        hRes = HRESULT_FROM_WIN32(GetLastError());
        Cleanup();
        return hRes;
    }

    m_hInstance = sInitParams.hInstance;

    // Create main window
    memset(&sCwParams, 0, sizeof(sCwParams));
    sCwParams.lpThis = this;
    sCwParams.lpInitParams = &sInitParams;
    m_hMainWnd = CreateWindowExW(WS_EX_APPWINDOW, m_szWndClassNameW, sInitParams.szCaptionW, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, sInitParams.hMenu, m_hInstance, &sCwParams);
    if (!m_hMainWnd)
    {
        hRes = sCwParams.hRes;
        if (SUCCEEDED(hRes))
        {
            hRes = HRESULT_FROM_WIN32(GetLastError());
            if (SUCCEEDED(hRes))
            {
                hRes = E_FAIL;
            }
        }
        Cleanup();
        return hRes;
    }

    // Done
    return S_OK;
}

VOID CTUiWindowManager::Finalize() noexcept
{
    while (!m_mapRegionWnd.empty())
    {
        auto it = m_mapRegionWnd.begin();

        DetachWindow(it->second);
    }

    if (m_hMainWnd)
    {
        if (IsWindow(m_hMainWnd) != FALSE)
        {
            DestroyWindow(m_hMainWnd);
        }
        m_hMainWnd = nullptr;
    }
    Cleanup();
}

HRESULT CTUiWindowManager::ProcessMessages(_In_opt_ BOOL bWaitForMessage) noexcept
{
    MSG sMsg;
    int res;

    if (!m_hInstance)
    {
        return E_FAIL;
    }

    if (bWaitForMessage)
    {
        if (!PeekMessageW(&sMsg, nullptr, 0, 0, PM_REMOVE))
        {
            return S_OK;
        }
        if (sMsg.message == WM_QUIT)
        {
            return static_cast<HRESULT>(sMsg.wParam);
        }
    }
    else
    {
        res = (int)GetMessageW(&sMsg, nullptr, 0, 0);
        if (res < 0)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (res == 0)
        {
            return static_cast<HRESULT>(sMsg.wParam);
        }
    }

    TranslateMessage(&sMsg);
    DispatchMessageW(&sMsg);

    // Done
    return S_OK;
}

HRESULT CTUiWindowManager::AttachWindow(_In_ CTUiWindow *lpWindow, _In_ INT iX, _In_ INT iY) noexcept
{
    return AttachWindowInternal(lpWindow, iX, iY, nullptr, nullptr);
}

HRESULT CTUiWindowManager::AttachWindow(_In_ CTUiWindow *lpWindow, _In_ INT iX, _In_ INT iY,
                                        _In_ GuiTerminal::RegionHandle hParentRegion) noexcept
{
    return AttachWindowInternal(lpWindow, iX, iY, hParentRegion, FindWindowByRegion(hParentRegion));
}

HRESULT CTUiWindowManager::AttachWindow(_In_ CTUiWindow *lpWindow, _In_ INT iX, _In_ INT iY, _In_ CTUiWindow *lpParentWindow) noexcept
{
    if (!lpParentWindow)
    {
        return E_POINTER;
    }

    if (lpParentWindow->Manager() != this || !lpParentWindow->RegionHandle())
    {
        return E_INVALIDARG;
    }

    return AttachWindowInternal(lpWindow, iX, iY, lpParentWindow->RegionHandle(), lpParentWindow);
}

HRESULT CTUiWindowManager::AttachWindowInternal(_In_ CTUiWindow *lpWindow, _In_ INT iX, _In_ INT iY,
                                                _In_opt_ GuiTerminal::RegionHandle hParentRegion,
                                                _In_opt_ CTUiWindow *lpParentWindow) noexcept
{
    INT iWidth, iHeight;
    HRESULT hRes;

    if (!lpWindow)
    {
        return E_POINTER;
    }
    if (!m_hInstance)
    {
        return E_FAIL;
    }
    if (lpWindow->IsAttached())
    {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    if (!m_lpGuiTerminal)
    {
        return E_UNEXPECTED;
    }

    iWidth = lpWindow->GetWidth();
    iHeight = lpWindow->GetHeight();

    hRes = lpWindow->ValidateAttachSize(iWidth, iHeight);
    if (FAILED(hRes))
    {
        lpWindow->ClearPendingAttachSize();
        return hRes;
    }

    hRes = m_lpGuiTerminal->CreateRegion(iX, iY, iWidth, iHeight, &lpWindow->m_regionH, hParentRegion);
    if (FAILED(hRes))
    {
        lpWindow->ClearPendingAttachSize();
        return hRes;
    }

    try
    {
        m_mapRegionWnd.emplace(lpWindow->m_regionH, lpWindow);
    }
    catch (const std::bad_alloc &)
    {
        m_lpGuiTerminal->DestroyRegion(lpWindow->m_regionH);
        lpWindow->m_regionH = nullptr;
        return E_OUTOFMEMORY;
    }

    lpWindow->m_lpWindowManager = this;
    lpWindow->m_lpTerminalControl = m_lpGuiTerminal;
    lpWindow->m_hParentRegion = hParentRegion;
    lpWindow->m_lpParentWindow = lpParentWindow;
    lpWindow->m_iAttachedWidth = iWidth;
    lpWindow->m_iAttachedHeight = iHeight;
    lpWindow->ClearPendingAttachSize();

    hRes = lpWindow->OnPostAttachEvent();
    if (FAILED(hRes))
    {
        DetachWindow(lpWindow);
        return hRes;
    }

    return S_OK;
}

HRESULT CTUiWindowManager::DetachWindow(_In_ CTUiWindow *lpWindow) noexcept
{
    if (!lpWindow)
    {
        return E_POINTER;
    }
    if (!m_hInstance)
    {
        return E_FAIL;
    }
    if (lpWindow->Manager() != this)
    {
        return E_INVALIDARG;
    }

    lpWindow->OnPreDetachEvent();

    if (lpWindow->m_regionH)
    {
        if (m_hMouseCaptureRegion == lpWindow->m_regionH)
        {
            m_hMouseCaptureRegion = nullptr;
            m_bMouseCaptured = FALSE;
        }
        m_mapRegionWnd.erase(lpWindow->m_regionH);
        m_lpGuiTerminal->DestroyRegion(lpWindow->m_regionH);
        lpWindow->m_regionH = nullptr;
    }
    lpWindow->m_hParentRegion = nullptr;
    lpWindow->m_lpParentWindow = nullptr;
    lpWindow->m_iAttachedWidth = 0;
    lpWindow->m_iAttachedHeight = 0;
    lpWindow->ClearPendingAttachSize();
    lpWindow->m_lpWindowManager = nullptr;
    lpWindow->m_lpTerminalControl = nullptr;

    // Done
    return S_OK;
}

HRESULT CTUiWindowManager::GetWindowRegionLocation(_In_ CTUiWindow *lpWindow, _Out_opt_ LPINT lpiX, _Out_opt_ LPINT lpiY,
                                                   _Out_opt_ LPINT lpiWidth, _Out_opt_ LPINT lpiHeight) const noexcept
{
    if (!lpWindow)
    {
        return E_POINTER;
    }
    if (!m_hInstance)
    {
        return E_FAIL;
    }
    if (lpWindow->Manager() != this || !lpWindow->RegionHandle())
    {
        return E_INVALIDARG;
    }

    m_lpGuiTerminal->GetRegionLocation(lpWindow->RegionHandle(), lpiX, lpiY, lpiWidth, lpiHeight);
    return S_OK;
}

HRESULT CTUiWindowManager::RelocateWindow(_In_ CTUiWindow *lpWindow, _In_ INT iX, _In_ INT iY) noexcept
{
    INT iWidth, iHeight;
    HRESULT hRes;

    if (!lpWindow)
    {
        return E_POINTER;
    }
    if (!m_hInstance)
    {
        return E_FAIL;
    }
    if (lpWindow->Manager() != this || !lpWindow->RegionHandle())
    {
        return E_INVALIDARG;
    }

    hRes = GetWindowRegionLocation(lpWindow, nullptr, nullptr, &iWidth, &iHeight);
    if (FAILED(hRes))
    {
        return hRes;
    }

    return m_lpGuiTerminal->RelocateRegion(lpWindow->RegionHandle(), iX, iY, iWidth, iHeight);
}

HRESULT CTUiWindowManager::BringWindowToFront(_In_ CTUiWindow *lpWindow) noexcept
{
    if (!lpWindow)
    {
        return E_POINTER;
    }
    if (!m_hInstance)
    {
        return E_FAIL;
    }
    if (lpWindow->Manager() != this || !lpWindow->RegionHandle())
    {
        return E_INVALIDARG;
    }

    return m_lpGuiTerminal->BringRegionToFront(lpWindow->RegionHandle());
}

HRESULT CTUiWindowManager::SetMouseCapture(_In_opt_ GuiTerminal::RegionHandle hRegion) noexcept
{
    if (!m_hInstance)
    {
        return E_FAIL;
    }

    // Set capture region and capture mouse input too
    SetCapture(m_hMainWnd);
    m_hMouseCaptureRegion = hRegion;
    m_bMouseCaptured = TRUE;

    // Done
    return S_OK;
}

HRESULT CTUiWindowManager::ReleaseMouseCapture() noexcept
{
    BOOL bMouseCaptured;

    if (!m_hInstance)
    {
        return E_FAIL;
    }

    // Unset capture region and release mouse input too
    bMouseCaptured = m_bMouseCaptured;
    m_hMouseCaptureRegion = nullptr;
    m_bMouseCaptured = FALSE;
    if (bMouseCaptured != FALSE)
    {
        ReleaseCapture();
    }

    // Done
    return S_OK;
}

VOID CTUiWindowManager::OnClose(_In_ CloseEventCallback_t cCloseEventCallback) noexcept
{
    m_cCloseEventCallback = cCloseEventCallback;
}

VOID CTUiWindowManager::OnDestroy(_In_ DestroyEventCallback_t cDestroyEventCallback) noexcept
{
    m_cDestroyEventCallback = cDestroyEventCallback;
}

VOID CTUiWindowManager::OnResize(_In_ ResizeEventCallback_t cResizeEventCallback) noexcept
{
    m_cResizeEventCallback = cResizeEventCallback;
}

VOID CTUiWindowManager::OnMouse(_In_ MouseEventCallback_t cMouseEventCallback) noexcept
{
    m_cMouseEventCallback = cMouseEventCallback;
}

VOID CTUiWindowManager::OnReleaseCapture(_In_ ReleaseCaptureEventCallback_t cReleaseCaptureEventCallback) noexcept
{
    m_cReleaseCaptureEventCallback = cReleaseCaptureEventCallback;
}

VOID CTUiWindowManager::RequestShutdown(_In_ HRESULT hRes) noexcept
{
    if (m_hInstance)
    {
        PostQuitMessage(static_cast<int>(hRes));
    }
}

VOID CTUiWindowManager::Show(_In_opt_ WORD wShowWindow) noexcept
{
    if (wShowWindow == SW_SHOWDEFAULT)
    {
        STARTUPINFOW sSiW;

        ::GetStartupInfoW(&sSiW);
        wShowWindow = (sSiW.wShowWindow == SW_MAXIMIZE) ? sSiW.wShowWindow : SW_NORMAL;
    }
    ::ShowWindow(m_hMainWnd, wShowWindow);
    ::UpdateWindow(m_hMainWnd);
}

HRESULT CTUiWindowManager::EnablePerMonitorDpiAwareness() noexcept
{
    HRESULT hRes;

    hRes = S_OK;
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE)
    {
        hRes = HRESULT_FROM_WIN32(GetLastError());
        if (hRes == E_ACCESSDENIED)
        {
            hRes = S_OK;
        }
    }
    return hRes;
}

VOID CTUiWindowManager::Cleanup() noexcept
{
    if (m_hInstance)
    {
        UnregisterClassW(m_szWndClassNameW, m_hInstance);
        m_hInstance = nullptr;
    }
    m_hMainWnd = nullptr;
    memset(m_szWndClassNameW, 0, sizeof(m_szWndClassNameW));
    m_bMouseCaptured = FALSE;
    m_hMouseCaptureRegion = nullptr;
    m_mapRegionWnd.clear();
}

CTUiWindow *CTUiWindowManager::FindWindowByRegion(_In_opt_ GuiTerminal::RegionHandle hRegion) const noexcept
{
    if (!hRegion)
    {
        return nullptr;
    }

    auto it = m_mapRegionWnd.find(hRegion);
    return (it != m_mapRegionWnd.end()) ? it->second : nullptr;
}

LRESULT CALLBACK CTUiWindowManager::StaticWndProc(_In_ HWND hWnd, _In_ UINT uMessage, _In_ WPARAM wParam, _In_ LPARAM lParam) noexcept
{
    if (uMessage == WM_NCCREATE)
    {
        return HandleNcCreate(hWnd, wParam, lParam);
    }
    else
    {
        CTUiWindowManager *lpThis = reinterpret_cast<CTUiWindowManager *>(GetWindowLongPtrW(hWnd, OFFSET_TO_CTUIWINDOWMANAGER));
        if (!lpThis)
        {
            return DefWindowProcW(hWnd, uMessage, wParam, lParam);
        }
        return lpThis->WndProc(hWnd, uMessage, wParam, lParam);
    }
}

LRESULT CTUiWindowManager::WndProc(_In_ HWND hWnd, _In_ UINT uMessage, _In_ WPARAM wParam, _In_ LPARAM lParam) noexcept
{
    LRESULT lResult;

    if (GuiTerminal::Control::WndProc(hWnd, uMessage, wParam, lParam, &lResult) != FALSE)
    {
        return lResult;
    }

    switch (uMessage)
    {
        case WM_CREATE:
            return HandleCreate(hWnd, wParam, lParam);

        case WM_CLOSE:
            return HandleClose();

        case WM_DESTROY:
            return HandleDestroy();

        case WM_NCDESTROY:
            return HandleNcDestroy();

        case WM_SIZE:
            return HandleSize(wParam, lParam);

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_MOUSEMOVE:
            return HandleMouseEvent(uMessage, wParam, lParam);

        case WM_CAPTURECHANGED:
            return HandleCaptureChanged(wParam, lParam);
    }
    return DefWindowProcW(hWnd, uMessage, wParam, lParam);
}

LRESULT CTUiWindowManager::HandleNcCreate(_In_ HWND hWnd, _In_ WPARAM wParam, _In_ LPARAM lParam) noexcept
{
    LPCREATESTRUCTW lpCreateStructW = reinterpret_cast<LPCREATESTRUCTW>(lParam);
    CTUiWindowManager *lpThis = reinterpret_cast<CREATE_MAIN_WINDOW_PARAMS *>(lpCreateStructW->lpCreateParams)->lpThis;

    SetWindowLongPtrW(hWnd, OFFSET_TO_CTUIWINDOWMANAGER, reinterpret_cast<LONG_PTR>(lpThis));

    // Done
    return DefWindowProcW(hWnd, WM_NCCREATE, wParam, lParam);
}

LRESULT CTUiWindowManager::HandleCreate(_In_ HWND hWnd, _In_ WPARAM wParam, _In_ LPARAM lParam) noexcept
{
    LPCREATESTRUCTW lpCreateStructW = reinterpret_cast<LPCREATESTRUCTW>(lParam);
    CREATE_MAIN_WINDOW_PARAMS *lpCreateParams = reinterpret_cast<CREATE_MAIN_WINDOW_PARAMS *>(lpCreateStructW->lpCreateParams);
    GuiTerminal::Control::Config sTerminalConfig{};
    SIZE sizeClient;
    RECT rcWorkArea;
    LONG x, y;

    // Create terminal
    sTerminalConfig.iRows = lpCreateParams->lpInitParams->iRows;
    sTerminalConfig.iCols = lpCreateParams->lpInitParams->iCols;
    lpCreateParams->hRes = GuiTerminal::Control::Create(hWnd, sTerminalConfig, &m_lpGuiTerminal);
    if (FAILED(lpCreateParams->hRes))
    {
        return -1;
    }

    lpCreateParams->hRes = m_lpGuiTerminal->GetPreferredWindowSize(&sizeClient);
    if (FAILED(lpCreateParams->hRes))
    {
        return -1;
    }

    // Get the available work area
    lpCreateParams->hRes = GetWorkAreaFromHwnd(hWnd, &rcWorkArea);
    if (FAILED(lpCreateParams->hRes))
    {
        return -1;
    }

    x = rcWorkArea.left + (rcWorkArea.right - rcWorkArea.left - sizeClient.cx) / 2;
    y = rcWorkArea.top + (rcWorkArea.bottom - rcWorkArea.top - sizeClient.cy) / 2;

    if (SetWindowPos(hWnd, nullptr, x, y, sizeClient.cx, sizeClient.cy, SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
    {
        lpCreateParams->hRes = HRESULT_FROM_WIN32(GetLastError());
        return -1;
    }

    // Done
    return DefWindowProcW(hWnd, WM_CREATE, wParam, lParam);
}

LRESULT CTUiWindowManager::HandleClose() noexcept
{
    BOOL bContinueClosing;

    // Raise event
    bContinueClosing = TRUE;
    if (m_cCloseEventCallback)
    {
        m_cCloseEventCallback(bContinueClosing);
    }

    // Destroy window if not canceled
    if (bContinueClosing)
    {
        DestroyWindow(m_hMainWnd);
    }

    // Done
    return 0L;
}

LRESULT CTUiWindowManager::HandleDestroy() noexcept
{
    // Raise event
    if (m_cDestroyEventCallback)
    {
        m_cDestroyEventCallback();
    }

    // Unlink the terminal (it will be destroyed by WM_NCDESTROY)
    m_lpGuiTerminal = nullptr;

    // Quit main loop
    PostQuitMessage(0);

    // Done
    return 0L;
}

LRESULT CTUiWindowManager::HandleNcDestroy() noexcept
{
    Cleanup();

    // Done
    return DefWindowProcW(m_hMainWnd, WM_NCDESTROY, 0, 0);
}

LRESULT CTUiWindowManager::HandleSize(_In_ WPARAM wParam, _In_ LPARAM lParam) noexcept
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    if (m_lpGuiTerminal)
    {
        INT iTotalCols, iTotalRows;

        m_lpGuiTerminal->GetTerminalSize(&iTotalCols, &iTotalRows);

        // Raise event
        if (m_cResizeEventCallback)
        {
            m_cResizeEventCallback(iTotalCols, iTotalRows);
        }
    }
    return 0L;
}

LRESULT CTUiWindowManager::HandleMouseEvent(_In_ UINT uMessage, _In_ WPARAM wParam, _In_ LPARAM lParam) noexcept
{
    MouseEvent_t sEvent;
    GuiTerminal::RegionHandle hTargetRegion;
    WORD wLoWParam;

    memset(&sEvent, 0, sizeof(sEvent));
    sEvent.xPos = GET_X_LPARAM(lParam);
    sEvent.yPos = GET_Y_LPARAM(lParam);

    if (m_lpGuiTerminal->GetCellFromPosition(sEvent.xPos, sEvent.yPos, &sEvent.iCol, &sEvent.iRow) == FALSE)
    {
        return 0L;
    }

    wLoWParam = LOWORD(wParam);
    sEvent.bControlKeyPressed =        (wLoWParam & MK_CONTROL) ? 1 : 0;
    sEvent.bShiftKeyPressed =          (wLoWParam & MK_SHIFT) ? 1 : 0;
    sEvent.bMouseLeftButtonPressed =   (wLoWParam & MK_LBUTTON) ? 1 : 0;
    sEvent.bMouseMiddleButtonPressed = (wLoWParam & MK_MBUTTON) ? 1 : 0;
    sEvent.bMouseRightButtonPressed =  (wLoWParam & MK_RBUTTON) ? 1 : 0;
    sEvent.bMouseX1ButtonPressed =     (wLoWParam & MK_XBUTTON1) ? 1 : 0;
    sEvent.bMouseX2ButtonPressed =     (wLoWParam & MK_XBUTTON2) ? 1 : 0;

    switch (uMessage)
    {
        case WM_LBUTTONDOWN:
            sEvent.nType = MouseEventType_t::LeftButtonDown;
            sEvent.bMouseLeftButtonPressed = 1;
            break;

        case WM_LBUTTONUP:
            sEvent.nType = MouseEventType_t::LeftButtonUp;
            sEvent.bMouseLeftButtonPressed = 0;
            break;

        case WM_MBUTTONDOWN:
            sEvent.nType = MouseEventType_t::MiddleButtonDown;
            sEvent.bMouseMiddleButtonPressed = 1;
            break;

        case WM_MBUTTONUP:
            sEvent.nType = MouseEventType_t::MiddleButtonUp;
            sEvent.bMouseMiddleButtonPressed = 0;
            break;

        case WM_RBUTTONDOWN:
            sEvent.nType = MouseEventType_t::RightButtonDown;
            sEvent.bMouseRightButtonPressed = 1;
            break;

        case WM_RBUTTONUP:
            sEvent.nType = MouseEventType_t::RightButtonUp;
            sEvent.bMouseRightButtonPressed = 0;
            break;

        case WM_XBUTTONDOWN:
            switch (HIWORD(wParam))
            {
                case XBUTTON2:
                    sEvent.nType = MouseEventType_t::X2ButtonDown;
                    sEvent.bMouseX1ButtonPressed = 1;
                    break;

                default:
                    sEvent.nType = MouseEventType_t::X1ButtonDown;
                    sEvent.bMouseX1ButtonPressed = 1;
                    break;
            }
            break;

        case WM_XBUTTONUP:
            switch (HIWORD(wParam))
            {
                case XBUTTON2:
                    sEvent.nType = MouseEventType_t::X2ButtonUp;
                    sEvent.bMouseX2ButtonPressed = 0;
                    break;

                default:
                    sEvent.nType = MouseEventType_t::X1ButtonUp;
                    sEvent.bMouseX1ButtonPressed = 0;
                    break;
            }
            break;

        case WM_MOUSEMOVE:
            sEvent.nType = MouseEventType_t::Move;
            break;

        default:
            return 0;
    }


    // Raise event
    if (m_bMouseCaptured)
    {
        hTargetRegion = m_hMouseCaptureRegion;
    }
    else
    {
        GuiTerminal::RegionHandle hRegion;
        INT iLocalCol, iLocalRow;
        INT iRegionWidth, iRegionHeight;

        hTargetRegion = nullptr;

        // Try to locate hit region
        hRegion = m_lpGuiTerminal->GetLastRegion();
        while (hRegion)
        {
            m_lpGuiTerminal->ConvertToRegionCoordinates(hRegion, sEvent.iCol, sEvent.iRow, &iLocalCol, &iLocalRow);
            if (iLocalCol >= 0 && iLocalRow >= 0)
            {
                m_lpGuiTerminal->GetRegionLocation(hRegion, nullptr, nullptr, &iRegionWidth, &iRegionHeight);
                if (iLocalCol < iRegionWidth && iLocalRow < iRegionHeight)
                {
                    // Got a hit
                    hTargetRegion = hRegion;

                    // Go down and restart
                    hRegion = m_lpGuiTerminal->GetChildLastRegion(hRegion);
                    continue;
                }
            }

            hRegion = m_lpGuiTerminal->GetPreviousRegion(hRegion);
        }
    }

    // Raise event
    if (hTargetRegion)
    {
        CTUiWindow *lpWnd;

        lpWnd = FindWindowByRegion(hTargetRegion);
        if (lpWnd)
        {
            lpWnd->OnMouseEvent(sEvent);
        }
    }
    else
    {
        if (m_cMouseEventCallback)
        {
            m_cMouseEventCallback(sEvent);
        }
    }

    // Done
    return 0L;
}

LRESULT CTUiWindowManager::HandleCaptureChanged(_In_ WPARAM wParam, _In_ LPARAM lParam) noexcept
{
    if (reinterpret_cast<HWND>(lParam) == m_hMainWnd)
    {
        return 0L;
    }

    if (m_bMouseCaptured)
    {
        m_bMouseCaptured = FALSE;
        if (m_hMouseCaptureRegion)
        {
            GuiTerminal::RegionHandle hOrigMouseCaptureRegion = m_hMouseCaptureRegion;
            CTUiWindow *lpWnd;

            m_hMouseCaptureRegion = nullptr;

            // Raise event
            lpWnd = FindWindowByRegion(hOrigMouseCaptureRegion);
            if (lpWnd)
            {
                lpWnd->OnReleaseCaptureEvent();
            }
        }
        else
        {
            if (m_cReleaseCaptureEventCallback)
            {
                m_cReleaseCaptureEventCallback();
            }
        }
    }

    // Done
    return 0L;
}

HRESULT CTUiWindowManager::GetWorkAreaFromHwnd(_In_ HWND hWnd, _Out_ LPRECT lprcWorkArea) noexcept
{
    HMONITOR hMonitor;

    _ASSERT(hWnd);
    _ASSERT(lprcWorkArea);

    hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor)
    {
        MONITORINFO sMi;

        memset(&sMi, 0, sizeof(sMi));
        sMi.cbSize = sizeof(sMi);
        if (GetMonitorInfoW(hMonitor, &sMi))
        {
            *lprcWorkArea = sMi.rcWork;
            return S_OK;
        }
    }

    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, lprcWorkArea, 0))
    {
        return S_OK;
    }

    memset(lprcWorkArea, 0, sizeof(*lprcWorkArea));
    return HRESULT_FROM_WIN32(GetLastError());
}
