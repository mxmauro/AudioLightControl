#include "CommandLine.h"
#include "Utils\Exception2HR.h"
#include "Utils\MessageBoxUtils.h"
#include <tlhelp32.h>

// -----------------------------------------------------------------------------

static CmdLimeParameters sCmdLineParams;

// -----------------------------------------------------------------------------

static HRESULT GetTopLevelProcessByName(_In_ LPCWSTR szProcessNameW, _Out_ DWORD &dwPid);
static HRESULT GetParameter(_Inout_z_ LPCWSTR &szCmdLineW, _Out_ std::wstring &strArgW);

// -----------------------------------------------------------------------------

HRESULT ParseCmdLimeParameters(_In_z_ LPCWSTR szCmdLineW)
{
    WCHAR szErrMsgBufW[512];
    std::wstring strArgW;
    HRESULT hRes;

    sCmdLineParams.dwPid = 0;
    sCmdLineParams.strConfigPathW.clear();

    while (*szCmdLineW != 0)
    {
        if (wcsncmp(szCmdLineW, L"--pid", 5) == 0)
        {
            szCmdLineW += 5;

            if (sCmdLineParams.dwPid != 0)
            {
error_multiple_pid_procname:
                MessageBoxW(nullptr, L"Error: --pid and/or --process-name parameters specified multiple times.", L"Audio Light Streamer",
                            MB_OK | MB_ICONERROR);
                return E_INVALIDARG;
            }

            hRes = GetParameter(szCmdLineW, strArgW);
            if (FAILED(hRes))
            {
                if (hRes == E_INVALIDARG)
                {
error_invalid_pid:  MessageBoxW(nullptr, L"Error: Invalid or missing value for --pid parameter.", L"Audio Light Streamer",
                                MB_OK | MB_ICONERROR);
                    return E_INVALIDARG;
                }
                if (hRes == E_OUTOFMEMORY)
                {
error_no_mem:       MessageBoxW(nullptr, L"Error: Out of memory.", L"Audio Light Streamer", MB_OK | MB_ICONERROR);
                    return E_OUTOFMEMORY;
                }
                else
                {
error_unexpected:   ErrorMessageBoxW(nullptr, L"Unexpected error while parsing parameters.", L"Audio Light Streamer", hRes);
                    return hRes;
                }
            }

            if (strArgW.empty())
            {
                goto error_invalid_pid;
            }
            sCmdLineParams.dwPid = static_cast<DWORD>(wcstoul(strArgW.c_str(), nullptr, 10));
            if (sCmdLineParams.dwPid == 0)
            {
                goto error_invalid_pid;
            }
        }
        else if (wcsncmp(szCmdLineW, L"--process-name", 14) == 0)
        {
            szCmdLineW += 14;

            if (sCmdLineParams.dwPid != 0)
            {
                goto error_multiple_pid_procname;
            }

            hRes = GetParameter(szCmdLineW, strArgW);
            if (FAILED(hRes))
            {
                if (hRes == E_INVALIDARG)
                {
error_invalid_procname:
                    MessageBoxW(nullptr, L"Error: Invalid or missing value for --process-name parameter.", L"Audio Light Streamer",
                                MB_OK | MB_ICONERROR);
                    return E_INVALIDARG;
                }
                goto error_unexpected;
            }

            if (strArgW.empty())
            {
                goto error_invalid_procname;
            }

            hRes = GetTopLevelProcessByName(strArgW.c_str(), sCmdLineParams.dwPid);
            if (FAILED(hRes))
            {
                if (hRes == E_OUTOFMEMORY)
                {
                    goto error_no_mem;
                }
                if (hRes == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
                {
                    swprintf_s(szErrMsgBufW, 512, L"Error: Could not find process with name \"%s\".", strArgW.c_str());
                    MessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", MB_OK | MB_ICONERROR);
                }
                else
                {
                    ErrorMessageBoxW(nullptr, L"Unexpected error while looking for the specified process.", L"Audio Light Streamer", hRes);
                }
                return hRes;
            }
        }
        else if (wcsncmp(szCmdLineW, L"--config", 8) == 0)
        {
            szCmdLineW += 8;

            if (!sCmdLineParams.strConfigPathW.empty())
            {
                MessageBoxW(nullptr, L"Error: --config parameter specified multiple times.", L"Audio Light Streamer",
                            MB_OK | MB_ICONERROR);
                return E_INVALIDARG;
            }

            hRes = GetParameter(szCmdLineW, strArgW);
            if (FAILED(hRes))
            {
                if (hRes == E_INVALIDARG)
                {
error_invalid_config:
                    MessageBoxW(nullptr, L"Error: Invalid or missing value for --config parameter.", L"Audio Light Streamer",
                                MB_OK | MB_ICONERROR);
                    return E_INVALIDARG;
                }
                goto error_unexpected;
            }

            if (strArgW.empty())
            {
                goto error_invalid_config;
            }

            sCmdLineParams.strConfigPathW = std::move(strArgW);
        }
        else
        {
            MessageBoxW(nullptr, L"Error: Unknown command line parameter specified.", L"Audio Light Streamer", MB_OK | MB_ICONERROR);
            return E_INVALIDARG;
        }
    }

    if (sCmdLineParams.strConfigPathW.empty())
    {
        hRes = TranslateToHresult([&]()
        {
            sCmdLineParams.strConfigPathW = L"AudioLightStreamer.conf.json";
        });
        if (FAILED(hRes))
        {
            goto error_unexpected;
        }
    }

    // Done
    return S_OK;
}

const CmdLimeParameters& GetCmdLimeParameters()
{
    return sCmdLineParams;
}

// -----------------------------------------------------------------------------

static HRESULT GetTopLevelProcessByName(_In_ LPCWSTR szProcessNameW, _Out_ DWORD &dwPid)
{
    typedef struct PidPpid_s {
        DWORD dwPid;
        DWORD dwParentPid;
        BOOL bIsTargetProcess;
    } PidPpid_t;
    PidPpid_t *lpProcTable;
    SIZE_T nProcTableCount;
    PROCESSENTRY32W sPeW;
    HANDLE hSnapShot;
    DWORD dwCurrentPid;

    dwPid = 0;
    hSnapShot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapShot == INVALID_HANDLE_VALUE)
    {
        return HRESULT_FROM_WIN32(::GetLastError());
    }

    lpProcTable = (PidPpid_t *)malloc(8192 * sizeof(PidPpid_t));
    if (!lpProcTable)
    {
        ::CloseHandle(hSnapShot);
        return E_OUTOFMEMORY;
    }
    nProcTableCount = 0;
    memset(&sPeW, 0, sizeof(sPeW));
    sPeW.dwSize = sizeof(sPeW);
    if (!::Process32FirstW(hSnapShot, &sPeW))
    {
        DWORD dwOsErr = ::GetLastError();
        free(lpProcTable);
        ::CloseHandle(hSnapShot);
        return HRESULT_FROM_WIN32(dwOsErr);
    }
    do
    {
        lpProcTable[nProcTableCount].dwPid = sPeW.th32ProcessID;
        lpProcTable[nProcTableCount].dwParentPid = sPeW.th32ParentProcessID;
        lpProcTable[nProcTableCount].bIsTargetProcess = (_wcsicmp(sPeW.szExeFile, szProcessNameW) == 0) ? TRUE : FALSE;
        if (dwPid == 0 && lpProcTable[nProcTableCount].bIsTargetProcess)
        {
            dwPid = sPeW.th32ProcessID;
        }
        nProcTableCount++;
    }
    while (nProcTableCount < 8192 && ::Process32NextW(hSnapShot, &sPeW));
    ::CloseHandle(hSnapShot);

    if (dwPid == 0)
    {
        free(lpProcTable);
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    dwCurrentPid = dwPid;
    for (SIZE_T i = 0; i < 1024; i++)
    {
        SIZE_T nTableIdx;

        for (nTableIdx = 0; nTableIdx < nProcTableCount; nTableIdx++)
        {
            if (lpProcTable[nTableIdx].dwPid == dwCurrentPid)
            {
                break;
            }
        }
        if (nTableIdx == nProcTableCount || lpProcTable[nTableIdx].dwParentPid <= 4 || lpProcTable[nTableIdx].dwParentPid == dwCurrentPid)
        {
            break;
        }
        if (lpProcTable[nTableIdx].bIsTargetProcess)
        {
            dwPid = dwCurrentPid;
        }
        dwCurrentPid = lpProcTable[nTableIdx].dwParentPid;
    }

    free(lpProcTable);
    return S_OK;
}

static HRESULT GetParameter(_Inout_z_ LPCWSTR &szCmdLineW, _Out_ std::wstring &strArgW)
{
    HRESULT hRes;

    strArgW.clear();

    hRes = TranslateToHresult([&]()
    {
        if (*szCmdLineW == L'=')
        {
            szCmdLineW++;
            if (*szCmdLineW == L'=')
            {
                szCmdLineW++;
            }
        }
        else if (*szCmdLineW == L' ')
        {
            while (*szCmdLineW == L' ')
            {
                szCmdLineW++;
            }
        }
        else if (*szCmdLineW == 0)
        {
            return;
        }
        else
        {
            throw (HRESULT)E_INVALIDARG;
        }

        if (*szCmdLineW == L'"')
        {
            szCmdLineW++;
            while (*szCmdLineW != 0)
            {
                if ((*szCmdLineW == L'"' || *szCmdLineW == L'\\') && szCmdLineW[1] == L'"')
                {
                    strArgW += L'"';
                    szCmdLineW += 2;
                }
                else if (*szCmdLineW == L'"')
                {
                    szCmdLineW++;
                    break;
                }
                else
                {
                    strArgW += *szCmdLineW;
                    szCmdLineW++;
                }
            }

            if (*szCmdLineW != 0 && *szCmdLineW != L' ')
            {
                throw (HRESULT)E_INVALIDARG;
            }
        }
        else
        {
            while (*szCmdLineW != 0 && *szCmdLineW != L' ')
            {
                strArgW += *szCmdLineW;
                szCmdLineW++;
            }
        }
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    while (*szCmdLineW == L' ')
    {
        szCmdLineW++;
    }

    return S_OK;
}
