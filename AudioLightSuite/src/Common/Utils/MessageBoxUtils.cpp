#include "MessageBoxUtils.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

// -----------------------------------------------------------------------------

static SIZE_T TrimRightSpaces(_Inout_updates_z_(nLen + 1) LPWSTR szMessageW, _In_ SIZE_T nLen);

// -----------------------------------------------------------------------------

int ErrorMessageBoxW(_In_opt_ HWND hWnd, _In_opt_z_ LPCWSTR szMessageW, _In_opt_z_ LPCWSTR szCaptionW, _In_ HRESULT hRes)
{
    LPWSTR szFormattedMessageW = nullptr;
    LPWSTR szSystemMessageW = nullptr;
    SIZE_T nSystemMessageLen, nFormattedMessageLen;
    DWORD cchSystemMessage = 0;
    int res, cchPrefix;

    if (!szMessageW)
    {
        szMessageW = L"";
    }

    if (hRes == E_OUTOFMEMORY || hRes == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY))
    {
error_no_mem:
        res = ::MessageBoxW(hWnd, L"Error: Not enough memory.", szCaptionW, MB_ICONERROR | MB_OK);
        goto done;
    }

    cchSystemMessage = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                        FORMAT_MESSAGE_FROM_SYSTEM |
                                        FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, static_cast<DWORD>(hRes),
                                        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                                        reinterpret_cast<LPWSTR>(&szSystemMessageW), 0,
                                        nullptr);
    if (cchSystemMessage != 0 && szSystemMessageW)
    {
        nSystemMessageLen = TrimRightSpaces(szSystemMessageW, static_cast<SIZE_T>(cchSystemMessage));
    }
    else
    {
        nSystemMessageLen = 0;
    }

    cchPrefix = _scwprintf(L"Error 0x%08X: %s", static_cast<unsigned>(hRes), szMessageW);
    if (cchPrefix < 0)
    {
        goto error_no_mem;
    }

    nFormattedMessageLen = static_cast<SIZE_T>(cchPrefix);
    if (nSystemMessageLen > 0)
    {
        if (nFormattedMessageLen > (SIZE_MAX - 2 - nSystemMessageLen))
        {
            goto error_no_mem;
        }
        nFormattedMessageLen += 2 + nSystemMessageLen;
    }

    if (nFormattedMessageLen > (SIZE_MAX / sizeof(WCHAR)) - 1)
    {
        goto error_no_mem;
    }

    szFormattedMessageW = static_cast<LPWSTR>(malloc((nFormattedMessageLen + 1) * sizeof(WCHAR)));
    if (!szFormattedMessageW)
    {
        goto error_no_mem;
    }

    if (swprintf_s(szFormattedMessageW, nFormattedMessageLen + 1, L"Error 0x%08X: %s",
                   static_cast<unsigned>(hRes), szMessageW) < 0)
    {
        goto error_no_mem;
    }

    if (nSystemMessageLen > 0)
    {
        memcpy(szFormattedMessageW + cchPrefix, L"\n\n", 2 * sizeof(WCHAR));
        memcpy(szFormattedMessageW + cchPrefix + 2, szSystemMessageW, nSystemMessageLen * sizeof(WCHAR));
        szFormattedMessageW[nFormattedMessageLen] = 0;
    }

    res = ::MessageBoxW(hWnd, szFormattedMessageW, szCaptionW, MB_ICONERROR | MB_OK);

done:
    if (szFormattedMessageW)
    {
        free(szFormattedMessageW);
    }
    if (szSystemMessageW)
    {
        ::LocalFree(szSystemMessageW);
    }
    return res;
}
// -----------------------------------------------------------------------------

static SIZE_T TrimRightSpaces(_Inout_updates_z_(nLen + 1) LPWSTR szMessageW, _In_ SIZE_T nLen)
{
    while (nLen > 0)
    {
        WCHAR ch = szMessageW[nLen - 1];
        if (ch != L'\r' && ch != L'\n' && ch != L' ' && ch != L'\t')
        {
            break;
        }
        nLen--;
    }
    szMessageW[nLen] = 0;
    return nLen;
}
