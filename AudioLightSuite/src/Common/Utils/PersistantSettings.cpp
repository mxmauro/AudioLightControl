#include "PersistantSettings.h"
#include "WinRegistry.h"
#include <stdio.h>

// -----------------------------------------------------------------------------

HRESULT CPersistantSettings::LoadWindowPosition(_Out_ INT &iX, _Out_ INT &iY) noexcept
{
    CWindowsRegistry cWinReg;
    WCHAR szValueNameW[32];
    DWORD dwValue;
    HRESULT hRes;

    iX = iY = 0;

    hRes = cWinReg.OpenKey(HKEY_CURRENT_USER, L"SOFTWARE\\AudioLightStreamer\\WindowPositions", TRUE);
    if (FAILED(hRes))
    {
        return hRes;
    }

    swprintf_s(szValueNameW, _countof(szValueNameW), L"%I64u_X", GetID());
    hRes = cWinReg.ReadDWord(szValueNameW, dwValue);
    if (FAILED(hRes))
    {
        return hRes;
    }
    iX = static_cast<INT>(dwValue);

    swprintf_s(szValueNameW, _countof(szValueNameW), L"%I64u_Y", GetID());
    hRes = cWinReg.ReadDWord(szValueNameW, dwValue);
    if (FAILED(hRes))
    {
        iX = 0;
        return hRes;
    }
    iY = static_cast<INT>(dwValue);

    // Done
    return S_OK;
}

HRESULT CPersistantSettings::SaveWindowPosition(_In_ INT iX, _In_ INT iY) noexcept
{
    CWindowsRegistry cWinReg;
    WCHAR szValueNameW[32];
    HRESULT hRes;

    hRes = cWinReg.CreateKey(HKEY_CURRENT_USER, L"SOFTWARE\\AudioLightStreamer\\WindowPositions");
    if (SUCCEEDED(hRes))
    {
        swprintf_s(szValueNameW, _countof(szValueNameW), L"%I64u_X", GetID());
        hRes = cWinReg.WriteDWord(szValueNameW, static_cast<DWORD>(iX));
        if (SUCCEEDED(hRes))
        {
            swprintf_s(szValueNameW, _countof(szValueNameW), L"%I64u_Y", GetID());
            hRes = cWinReg.WriteDWord(szValueNameW, static_cast<DWORD>(iY));
            return hRes;
        }
        if (FAILED(hRes))
        {
            swprintf_s(szValueNameW, _countof(szValueNameW), L"%I64u_X", GetID());
            cWinReg.DeleteValue(szValueNameW);
            swprintf_s(szValueNameW, _countof(szValueNameW), L"%I64u_Y", GetID());
            cWinReg.DeleteValue(szValueNameW);
        }
    }

    // Done
    return hRes;
}
