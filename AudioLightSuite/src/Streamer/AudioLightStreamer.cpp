#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <combaseapi.h>
#ifdef _DEBUG
    #define _CRTDBG_MAP_ALLOC
    #include <crtdbg.h>
#endif //_DEBUG
#include "AppConfig.h"
#include "AudioCapture.h"
#include "CommandLine.h"
#include "Engine.h"
#include "Utils\ComStart.h"
#include "Utils\MessageBoxUtils.h"
#include "Utils\WinsockStart.h"

#pragma comment(lib, "Crypt32.lib")

// -----------------------------------------------------------------------------

static HRESULT RunStreamer(_In_ HINSTANCE hInstance);
static HRESULT ListInputDevices();

// -----------------------------------------------------------------------------

INT APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLineW, _In_ INT nCmdShow)
{
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

    // Run streamer or list input devices
    if (GetCmdLimeParameters().bListInputDevices != FALSE)
    {
        return (int)ListInputDevices();
    }

    return (int)RunStreamer(hInstance);
}

// -----------------------------------------------------------------------------

static HRESULT RunStreamer(_In_ HINSTANCE hInstance)
{
    CComStart cComStart;
    CWinsockStart cWinsockStart;
    HRESULT hRes;

    // Read application configuration
    hRes = ReadAppConfig();
    if (FAILED(hRes))
    {
        return (int)hRes;
    }

    // Check COM initialization
    if (!cComStart.Succeeded())
    {
        ErrorMessageBoxW(nullptr, L"Unable to initialize COM.", L"Audio Light Streamer", cComStart.HResult());
        return (int)cComStart.HResult();
    }

    // Check Winsock initialization
    if (!cWinsockStart.Succeeded())
    {
        ErrorMessageBoxW(nullptr, L"Unable to initialize Winsock.", L"Audio Light Streamer", cWinsockStart.HResult());
        return (int)cWinsockStart.HResult();
    }

    // Create main window
    hRes = RunEngine(hInstance);

    // Done
    return (int)hRes;
}

static HRESULT ListInputDevices()
{
    CComStart cComStart;
    std::wstring strInputDevicesW;
    HRESULT hRes;

    // Check COM initialization
    if (!cComStart.Succeeded())
    {
        ErrorMessageBoxW(nullptr, L"Unable to initialize COM.", L"Audio Light Streamer", cComStart.HResult());
        return (int)cComStart.HResult();
    }

    // Get list of input audio devices
    hRes = CAudioCapture::GetInputAudioDevicesText(strInputDevicesW);
    if (SUCCEEDED(hRes))
    {
        MessageBoxW(nullptr, strInputDevicesW.c_str(), L"Audio Input Devices", MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        ErrorMessageBoxW(nullptr, L"Unable to list audio input devices.", L"Audio Light Streamer", hRes);
    }

    // Done
    return (int)hRes;
}
