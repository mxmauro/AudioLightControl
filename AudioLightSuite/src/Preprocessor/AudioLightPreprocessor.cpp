#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "CommandLine.h"
#include "FeatureExtractor.h"
#include "..\Common\Utils\ComStart.h"
#include <mfapi.h>
#include <stdio.h>

// -----------------------------------------------------------------------------

static HRESULT RunPreprocessor();

// -----------------------------------------------------------------------------

INT wmain(_In_ INT argc, _In_reads_(argc) LPWSTR argvW[])
{
    HRESULT hRes;

#ifdef _DEBUG
    //_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_CHECK_CRT_DF | _CRTDBG_LEAK_CHECK_DF);
#endif //_DEBUG

    // Parse command line parameters
    hRes = ParseCmdLimeParameters(argc, argvW);
    if (FAILED(hRes))
    {
        return (int)hRes;
    }

    return (int)RunPreprocessor();
}

// -----------------------------------------------------------------------------

static HRESULT RunPreprocessor()
{
    CComStart cComStart;
    HRESULT hRes;

    // Check COM initialization
    if (!cComStart.Succeeded())
    {
        fwprintf(stderr, L"Error 0x%08X: Unable to initialize COM.\n", (unsigned int)cComStart.HResult());
        return (int)cComStart.HResult();
    }

    // Run preprocessor
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
    return (int)hRes;
}
