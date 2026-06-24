#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "CommandLine.h"
#include "FeatureExtractor.h"
#include "..\Common\Utils\WinsockStart.h"
#include <mfapi.h>
#include <stdio.h>

// -----------------------------------------------------------------------------

static VOID PrintUsage() noexcept
{
    fwprintf(stderr, L"Usage: AudioLightPreprocessor --input <input.mp3> [--output <output.alcfeat>]\n");
}

// -----------------------------------------------------------------------------

INT wmain(_In_ INT argc, _In_reads_(argc) LPWSTR argvW[])
{
    CWinsockStart cWinsockStart;
    HRESULT hRes;

    hRes = ParseCmdLimeParameters(argc, argvW);
    if (FAILED(hRes))
    {
        return (int)hRes;
    }

    if (!cWinsockStart.Succeeded())
    {
        fwprintf(stderr, L"Error 0x%08X: Unable to initialize Winsock.\n", (unsigned int)cWinsockStart.HResult());
        return (int)cWinsockStart.HResult();
    }

    hRes = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hRes))
    {
        fwprintf(stderr, L"Error 0x%08X: Unable to initialize COM.", (unsigned int)hRes);
        return (int)hRes;
    }

    hRes = ::MFStartup(MF_VERSION);
    if (SUCCEEDED(hRes))
    {
        hRes = ExtractFeatures();

        // Cleanup
        ::MFShutdown();
    }
    else
    {
        fwprintf(stderr, L"Error 0x%08X: Unable to initialize media foundation.\n", (unsigned int)hRes);
    }

    // Done
    ::CoUninitialize();
    return (int)hRes;
}
