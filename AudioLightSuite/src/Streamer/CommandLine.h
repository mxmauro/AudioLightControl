#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// -----------------------------------------------------------------------------

typedef enum CmdLineAudioSource_e {
    CmdLineAudioSource_DefaultOutput,
    CmdLineAudioSource_ProcessOutput,
    CmdLineAudioSource_Input,
} CmdLineAudioSource_t;

// -----------------------------------------------------------------------------

typedef struct CmdLimeParameters_s {
    CmdLineAudioSource_t eAudioSource;
    DWORD dwPid;
    BOOL bListInputDevices;
    std::wstring strInputDeviceNameW;
    std::wstring strConfigPathW;
} CmdLimeParameters;

// -----------------------------------------------------------------------------

HRESULT ParseCmdLimeParameters(_In_z_ LPCWSTR szCmdLineW);

const CmdLimeParameters& GetCmdLimeParameters();
