#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// -----------------------------------------------------------------------------

typedef struct CmdLimeParameters_s {
    DWORD dwPid;
    std::wstring strConfigPathW;
} CmdLimeParameters;

// -----------------------------------------------------------------------------

HRESULT ParseCmdLimeParameters(_In_z_ LPCWSTR szCmdLineW);

const CmdLimeParameters& GetCmdLimeParameters();
