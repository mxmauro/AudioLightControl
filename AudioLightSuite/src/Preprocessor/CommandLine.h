#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// -----------------------------------------------------------------------------

typedef struct CmdLimeParameters_s {
    std::wstring strInputFilePathW;
    std::wstring strOutputFilePathW;
} CmdLimeParameters;

// -----------------------------------------------------------------------------

HRESULT ParseCmdLimeParameters(_In_ INT argc, _In_reads_(argc) LPWSTR argvW[]) noexcept;

const CmdLimeParameters& GetCmdLimeParameters() noexcept;
