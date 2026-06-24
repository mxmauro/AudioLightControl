#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <combaseapi.h>
#ifdef _DEBUG
    #define _CRTDBG_MAP_ALLOC
    #include <crtdbg.h>
#endif //_DEBUG
#include "AppConfig.h"
#include "CommandLine.h"
#include "Engine.h"
#include "Utils\MessageBoxUtils.h"
#include "Utils\WinsockStart.h"

#pragma comment(lib, "Crypt32.lib")

// -----------------------------------------------------------------------------

INT APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLineW, _In_ INT nCmdShow)
{
    CWinsockStart cWinsockStart;
    HRESULT hRes;

#ifdef _DEBUG
    //_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_CHECK_CRT_DF | _CRTDBG_LEAK_CHECK_DF);
#endif //_DEBUG

    // Parse command line parameters

    hRes = ParseCmdLimeParameters(lpCmdLineW);
    if (FAILED(hRes))
    {
        return (int)hRes;
    }

    // Read application configuration
    hRes = ReadAppConfig();
    if (FAILED(hRes))
    {
        return (int)hRes;
    }

    // Initialize Winsock
    if (!cWinsockStart.Succeeded())
    {
        ErrorMessageBoxW(nullptr, L"Unable to initialize Winsock.", L"Audio Light Streamer", cWinsockStart.HResult());
        return (int)cWinsockStart.HResult();
    }

    // Initialize COM
    hRes = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to initialize COM.", L"Audio Light Streamer", hRes);
        return (int)hRes;
    }

    // Create main window
    hRes = RunEngine(hInstance);

    // Done
    ::CoUninitialize();
    return (int)hRes;
}
