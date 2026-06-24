#define _CRT_SECURE_NO_WARNINGS
#include "AppConfig.h"
#include "Audio\AudioAnalysis.h"
#include "CommandLine.h"
#include "Devices\BaseIotDevice.h"
#include "Devices\LedStrip5050.h"
#include "Devices\SharedManualColorControl.h"
#include "Engine.h"
#include "Monitors\AudioAnalysisMonitor.h"
#include "Monitors\BaseMonitor.h"
#include "OutputAudioCapture.h"
#include "Processors\BaseAudioProcessor.h"
#include "Processors\SpectrumColor.h"
#include "TUI\TUiFrameWindow.h"
#include "TUI\TUiWindowManager.h"
#include "Utils\Exception2HR.h"
#include "Utils\MessageBoxUtils.h"
#include <memory>
#include <vector>
#include "resource.h"

// -----------------------------------------------------------------------------

typedef struct PanelGrid_s {
    INT iTotalCols, iTotalRows;
    INT iMaxRowHeight;
    INT iCurrentX;
    INT iCurrentY;
} PanelGrid_t;

// -----------------------------------------------------------------------------

static CTUiWindowManager cWndMgr;
static COutputAudioCapture cOutputAudioCapture;
static CAudioAnalyzer cAudioAnalyzer;
static CAudioProcessorList vecAudioProcessors;
static CMonitorList vecMonitors;
static CIotDeviceList vecIotDevices;
static std::unique_ptr<CSharedManualColorControlWindow> cSharedManualColorControlWindow;
static std::vector<std::unique_ptr<CTUiFrameWindow>> vecTUiFrames;
static CThread cTickThread;

// -----------------------------------------------------------------------------

static HRESULT Initialize(_In_ HINSTANCE hInstance) noexcept;
static VOID Finalize() noexcept;

static VOID TimerTick() noexcept;

static VOID OnAudioData(_In_ const float *lpfPCM, _In_ UINT32 ulSamplesCount);
static VOID OnErrorCallback(_In_ HRESULT hRes);

static HRESULT CreateSpectrumColorAudioProcessor(_Inout_ PanelGrid_t &sPanelGrid);
static HRESULT CreateAudioAnalysisMonitor(_Inout_ PanelGrid_t &sPanelGrid);
static HRESULT CreateSharedManualColorControlWindow(_Inout_ PanelGrid_t &sPanelGrid);

static HRESULT LoadWindowPosition(_In_ CBaseAudioProcessor *lpAudioProc, _In_ CTUiWindow *lpAttachedWindow, _Inout_ PanelGrid_t &sPanelGrid,
                                  _Out_ INT &iX, _Out_ INT &iY);
static HRESULT LoadWindowPosition(_In_ CBaseMonitor *lpMonitor, _In_ CTUiWindow *lpAttachedWindow, _Inout_ PanelGrid_t &sPanelGrid,
                                  _Out_ INT &iX, _Out_ INT &iY);
static HRESULT LoadWindowPosition(_In_ CBaseIotDevice *lpIotDevice, _In_ CTUiWindow *lpAttachedWindow, _Inout_ PanelGrid_t &sPanelGrid,
                                  _Out_ INT &iX, _Out_ INT &iY);
static HRESULT LoadWindowPositionCommon(_In_ HRESULT hRes, _In_ CTUiWindow *lpWnd, _Inout_ PanelGrid_t &sPanelGrid, _Inout_ INT &iX,
                                        _Inout_ INT &iY);

// -----------------------------------------------------------------------------

HRESULT RunEngine(_In_ HINSTANCE hInstance) noexcept
{
    HRESULT hRes;

    hRes = Initialize(hInstance);
    while (SUCCEEDED(hRes))
    {
        hRes = cWndMgr.ProcessMessages(TRUE);
    }
    Finalize();
    return hRes;
}

// -----------------------------------------------------------------------------

static HRESULT Initialize(_In_ HINSTANCE hInstance) noexcept
{
    WCHAR szErrMsgBufW[512];
    COutputAudioCapture::Config sOutputAudioCaptureConfig;
    PanelGrid_t sPanelGrid;
    INT iX, iY;
    CTUiWindowManager::InitParams_t sWndMgrInitParams;
    HRESULT hRes;

    // Initialize output audio capture
    sOutputAudioCaptureConfig = {};
    sOutputAudioCaptureConfig.dwPid = GetCmdLimeParameters().dwPid;
    sOutputAudioCaptureConfig.OnAudioDataCallback = OnAudioData;
    sOutputAudioCaptureConfig.OnErrorCallback = OnErrorCallback;
    hRes = cOutputAudioCapture.Initialize(sOutputAudioCaptureConfig);
    if (FAILED(hRes))
    {
        if (sOutputAudioCaptureConfig.dwPid != 0)
        {
            ErrorMessageBoxW(nullptr, L"Unable to initialize audio capture. Make sure the specified process ID is correct and running.",
                             L"Audio Light Streamer", hRes);
        }
        else
        {
            ErrorMessageBoxW(nullptr, L"Unable to initialize audio capture.", L"Audio Light Streamer", hRes);
        }
        return hRes;
    }

    cAudioAnalyzer.SetFrequency((float)cOutputAudioCapture.GetSampleRate());

    // Initialize windows manager
    cWndMgr.OnDestroy([]() -> VOID
    {
        cTickThread.Stop();
        cOutputAudioCapture.Finalize();
    });

    sWndMgrInitParams = {};
    sWndMgrInitParams.hInstance = hInstance;
    sWndMgrInitParams.szCaptionW = L"Auto Light Streamer";
    sWndMgrInitParams.hIcon = (HICON)::LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_MAINICON), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
    sWndMgrInitParams.hSmallIcon = (HICON)::LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_MAINICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    sWndMgrInitParams.iCols = 160;
    sWndMgrInitParams.iRows = 50;
    hRes = cWndMgr.Initialize(sWndMgrInitParams);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create main window.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    memset(&sPanelGrid, 0, sizeof(sPanelGrid));
    cWndMgr.Terminal()->GetTerminalSize(&sPanelGrid.iTotalCols, &sPanelGrid.iTotalRows);

    // Initialize audio processors

    hRes = CreateSpectrumColorAudioProcessor(sPanelGrid);
    if (FAILED(hRes))
    {
        return hRes;
    }

    hRes = CreateAudioAnalysisMonitor(sPanelGrid);
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Initialize devices

    for (const auto &dev : GetAppConfig().m_vecDevices)
    {
        std::unique_ptr<CBaseIotDevice> iotDevice;
        std::unique_ptr<CTUiFrameWindow> frameWindow;

        // Create device
        switch (dev.eType)
        {
            case CConfig::DeviceType::Strip5050:
                hRes = TranslateToHresult([&]()
                {
                    iotDevice = std::make_unique<CLedStrip5050IotDevice>();
                });
                break;

            default:
                hRes = E_NOTIMPL;
                break;
        }
        if (FAILED(hRes))
        {
            swprintf_s(szErrMsgBufW, 512, L"Unable to create device \"%s\".", dev.strName.c_str());
            ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
            return hRes;
        }

        hRes = iotDevice->SetupDeviceInfo(dev.strName.c_str());
        if (FAILED(hRes))
        {
            swprintf_s(szErrMsgBufW, 512, L"Unable to setup device information for device \"%s\".", dev.strName.c_str());
            ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
            return hRes;
        }

        hRes = iotDevice->SetupCommInfo(dev.strHost.c_str(), dev.strUserName.c_str(), dev.aPrivateKey);
        if (FAILED(hRes))
        {
            swprintf_s(szErrMsgBufW, 512, L"Unable to setup communication parameters for device \"%s\".", dev.strName.c_str());
            ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
            return hRes;
        }

        hRes = iotDevice->SetupAudioProcessors(vecAudioProcessors);
        if (FAILED(hRes))
        {
            return hRes;
        }

        hRes = TranslateToHresult([&]()
        {
            frameWindow = std::make_unique<CTUiFrameWindow>(iotDevice.get());
        });
        if (FAILED(hRes))
        {
            ErrorMessageBoxW(nullptr, L"Unable to create frame window for IoT device.", L"Audio Light Streamer", hRes);
            return hRes;
        }

        hRes = LoadWindowPosition(iotDevice.get(), frameWindow.get(), sPanelGrid, iX, iY);
        if (FAILED(hRes))
        {
            swprintf_s(szErrMsgBufW, 512, L"Unable to load window position for device \"%s\".", dev.strName.c_str());
            ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
            return hRes;
        }

        hRes = cWndMgr.AttachWindow(frameWindow.get(), iX, iY);
        if (FAILED(hRes))
        {
            swprintf_s(szErrMsgBufW, 512, L"Unable to setup terminal console for device \"%s\".", dev.strName.c_str());
            ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
            return hRes;
        }

        hRes = TranslateToHresult([&]()
        {
            vecTUiFrames.push_back(std::move(frameWindow));
        });
        if (FAILED(hRes))
        {
            ErrorMessageBoxW(nullptr, L"Unable to add IoT device frame to list.", L"Audio Light Streamer", hRes);
            return hRes;
        }

        hRes = iotDevice->InitComm();
        if (FAILED(hRes))
        {
            swprintf_s(szErrMsgBufW, 512, L"Unable to initialize communication channel for device \"%s\".", dev.strName.c_str());
            ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
            return hRes;
        }

        hRes = TranslateToHresult([&]()
        {
            vecIotDevices.push_back(std::move(iotDevice));
        });
        if (FAILED(hRes))
        {
            ErrorMessageBoxW(nullptr, L"Unable to add IoT device to list.", L"Audio Light Streamer", hRes);
            return hRes;
        }
    }

    hRes = CreateSharedManualColorControlWindow(sPanelGrid);
    if (FAILED(hRes))
    {
        return hRes;
    }

    for (const auto &audioProc : vecAudioProcessors)
    {
        audioProc->FirstRender();
    }
    for (const auto &monitor : vecMonitors)
    {
        monitor->FirstRender();
    }
    for (const auto &iotDevice : vecIotDevices)
    {
        iotDevice->FirstRender();
    }
    if (cSharedManualColorControlWindow)
    {
        cSharedManualColorControlWindow->FirstRender();
    }

    hRes = cTickThread.Start([](_In_ CThread *lpThread) -> DWORD
    {
        while (lpThread->CheckForAbort(10) == HRESULT_FROM_WIN32(ERROR_TIMEOUT))
        {
            TimerTick();
            InvalidateRect(cWndMgr.GetHwnd(), nullptr, FALSE);
        }
        return 0;
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to start tick timer.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = cOutputAudioCapture.StartCapture();
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to start audio capture.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    // Done
    cWndMgr.Show();
    return hRes;
}

static VOID Finalize() noexcept
{
    cTickThread.Stop();
    cOutputAudioCapture.Finalize();
    cWndMgr.Finalize();
    vecTUiFrames.clear();
    cSharedManualColorControlWindow.reset();
    vecIotDevices.clear();
    vecMonitors.clear();
    vecAudioProcessors.clear();
}

static VOID TimerTick() noexcept
{
    for (const auto &audioProc : vecAudioProcessors)
    {
        audioProc->TimerTick();
    }

    for (const auto &monitor : vecMonitors)
    {
        monitor->TimerTick();
    }

    for (const auto &iotDevice : vecIotDevices)
    {
        iotDevice->TimerTick();
    }
}

static VOID OnAudioData(_In_ const float *lpfPCM, _In_ UINT32 ulSamplesCount)
{
    CAudioAnalyzer::Frame_t sFrame;

    cAudioAnalyzer.Analyze(lpfPCM, ulSamplesCount, sFrame);

    for (const auto &audioProc : vecAudioProcessors)
    {
        audioProc->Process(sFrame);
    }

    for (const auto &monitor : vecMonitors)
    {
        monitor->Process(sFrame);
    }

    for (const auto &iotDevice : vecIotDevices)
    {
        iotDevice->Process();
    }
}

static VOID OnErrorCallback(_In_ HRESULT hRes)
{
    ErrorMessageBoxW(cWndMgr.GetHwnd(), L"An error occurred while capturing audio output.", L"Audio Light Streamer", hRes);
    cWndMgr.RequestShutdown(hRes);
}

static HRESULT CreateSpectrumColorAudioProcessor(_Inout_ PanelGrid_t &sPanelGrid)
{
    std::unique_ptr<CSpectrumColorAudioProcessor> spectrumColorAudioProc;
    std::unique_ptr<CTUiFrameWindow> frameWindow;
    CSpectrumColorAudioProcessor::Config sSpectrumColorProcConfig;
    INT iX, iY;
    HRESULT hRes;

    hRes = TranslateToHresult([&]()
    {
        spectrumColorAudioProc = std::make_unique<CSpectrumColorAudioProcessor>();
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create spectrum color audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    sSpectrumColorProcConfig = CSpectrumColorAudioProcessor::GetDefaultConfig();
    sSpectrumColorProcConfig.ulSampleRate = cOutputAudioCapture.GetSampleRate();
    spectrumColorAudioProc->Initialize(sSpectrumColorProcConfig);

    hRes = TranslateToHresult([&]()
    {
        frameWindow = std::make_unique<CTUiFrameWindow>(spectrumColorAudioProc.get());
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create frame window for spectrum color audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = LoadWindowPosition(spectrumColorAudioProc.get(), frameWindow.get(), sPanelGrid, iX, iY);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to load window position for spectrum color audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = cWndMgr.AttachWindow(frameWindow.get(), iX, iY);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to setup terminal console for spectrum color audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        vecTUiFrames.push_back(std::move(frameWindow));
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to add spectrum color frame to list.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        vecAudioProcessors.push_back(std::move(spectrumColorAudioProc));
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to add spectrum color audio processor to list.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    // Done
    return S_OK;
}

static HRESULT CreateAudioAnalysisMonitor(_Inout_ PanelGrid_t &sPanelGrid)
{
    std::unique_ptr<CAudioAnalysisMonitor> cAudioAnalysisMonitor;
    std::unique_ptr<CTUiFrameWindow> frameWindow;
    INT iX, iY;
    HRESULT hRes;

    hRes = TranslateToHresult([&]()
    {
        cAudioAnalysisMonitor = std::make_unique<CAudioAnalysisMonitor>();
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create audio analysis monitor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        frameWindow = std::make_unique<CTUiFrameWindow>(cAudioAnalysisMonitor.get());
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create frame window for audio analysis monitor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = LoadWindowPosition(cAudioAnalysisMonitor.get(), frameWindow.get(), sPanelGrid, iX, iY);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to load window position for audio analysis monitor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = cWndMgr.AttachWindow(frameWindow.get(), iX, iY);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to setup terminal console for audio analysis monitor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        vecTUiFrames.push_back(std::move(frameWindow));
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to add audio analysis monitor frame to list.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        vecMonitors.push_back(std::move(cAudioAnalysisMonitor));
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to add audio analysis monitor to list.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    return S_OK;
}

static HRESULT CreateSharedManualColorControlWindow(_Inout_ PanelGrid_t &sPanelGrid)
{
    std::vector<CBaseIotDevice *> vecSupportedDevices;
    std::unique_ptr<CTUiFrameWindow> frameWindow;
    INT iX, iY;
    HRESULT hRes;

    for (const auto &iotDevice : vecIotDevices)
    {
        if (iotDevice && iotDevice->SupportsManualColorActions() != FALSE)
        {
            vecSupportedDevices.push_back(iotDevice.get());
        }
    }

    hRes = TranslateToHresult([&]()
    {
        cSharedManualColorControlWindow = std::make_unique<CSharedManualColorControlWindow>(vecSupportedDevices);
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create shared manual color control window.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        frameWindow = std::make_unique<CTUiFrameWindow>(cSharedManualColorControlWindow.get());
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create frame window for shared manual color control.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = LoadWindowPositionCommon(cSharedManualColorControlWindow->LoadWindowPosition(iX, iY), frameWindow.get(), sPanelGrid, iX, iY);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to load window position for shared manual color control.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = cWndMgr.AttachWindow(frameWindow.get(), iX, iY);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to setup terminal console for shared manual color control.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        vecTUiFrames.push_back(std::move(frameWindow));
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to add shared manual color control frame to list.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    return S_OK;
}

static HRESULT LoadWindowPosition(_In_ CBaseAudioProcessor *lpAudioProc, _In_ CTUiWindow *lpAttachedWindow, _Inout_ PanelGrid_t &sPanelGrid, _Out_ INT &iX,
                                  _Out_ INT &iY)
{
    return LoadWindowPositionCommon(lpAudioProc->LoadWindowPosition(iX, iY), lpAttachedWindow, sPanelGrid, iX, iY);
}

static HRESULT LoadWindowPosition(_In_ CBaseMonitor *lpMonitor, _In_ CTUiWindow *lpAttachedWindow, _Inout_ PanelGrid_t &sPanelGrid, _Out_ INT &iX,
                                  _Out_ INT &iY)
{
    return LoadWindowPositionCommon(lpMonitor->LoadWindowPosition(iX, iY), lpAttachedWindow, sPanelGrid, iX, iY);
}

static HRESULT LoadWindowPosition(_In_ CBaseIotDevice *lpIotDevice, _In_ CTUiWindow *lpAttachedWindow, _Inout_ PanelGrid_t &sPanelGrid, _Out_ INT &iX,
                                  _Out_ INT &iY)
{
    return LoadWindowPositionCommon(lpIotDevice->LoadWindowPosition(iX, iY), lpAttachedWindow, sPanelGrid, iX, iY);
}

static HRESULT LoadWindowPositionCommon(_In_ HRESULT hRes, _In_ CTUiWindow *lpWnd, _Inout_ PanelGrid_t &sPanelGrid, _Inout_ INT &iX, _Inout_ INT &iY)
{
    INT iWidth, iHeight;

    iWidth = lpWnd->GetWidth();
    iHeight = lpWnd->GetHeight();

    if (FAILED(hRes))
    {
        if (hRes != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && hRes != HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
        {
            return hRes;
        }

        // Set default position (next available position in grid, starting from top left)
        if (sPanelGrid.iTotalCols - sPanelGrid.iCurrentX < iWidth)
        {
            sPanelGrid.iCurrentX = 0;
            sPanelGrid.iCurrentY += sPanelGrid.iMaxRowHeight;
            sPanelGrid.iMaxRowHeight = 0;
        }
        iX = sPanelGrid.iCurrentX;
        iY = sPanelGrid.iCurrentY;
    }

    // Adjust if the window would go out of bounds (e.g. if the screen resolution changed since the last saved position)
    if (sPanelGrid.iTotalCols - iX < iWidth)
    {
        iX = (sPanelGrid.iTotalCols >= iWidth) ? sPanelGrid.iTotalCols - iWidth : 0;
    }
    if (sPanelGrid.iTotalRows - iY < iHeight)
    {
        iY = (sPanelGrid.iTotalRows >= iHeight) ? sPanelGrid.iTotalRows - iHeight : 0;
    }

    sPanelGrid.iCurrentX = iX + iWidth;
    sPanelGrid.iCurrentY = iY;

    if (iHeight > sPanelGrid.iMaxRowHeight)
    {
        sPanelGrid.iMaxRowHeight = iHeight;
    }

    // Done
    return S_OK;
}
