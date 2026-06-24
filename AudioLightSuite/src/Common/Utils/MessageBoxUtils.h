#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

int ErrorMessageBoxW(_In_opt_ HWND hWnd, _In_opt_z_ LPCWSTR szMessageW, _In_opt_z_ LPCWSTR szCaptionW, _In_ HRESULT hRes);
