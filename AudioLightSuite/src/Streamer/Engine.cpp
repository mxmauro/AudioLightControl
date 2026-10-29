#define _CRT_SECURE_NO_WARNINGS
#include "AppConfig.h"
#include "Audio\AudioAnalysis.h"
#include "CommandLine.h"
#include "Devices\BaseIotDevice.h"
#include "Devices\DmxIotDevice.h"
#include "Devices\GoveeStripIotDevice.h"
#include "Devices\Rgb5050IotDevice.h"
#include "Devices\SharedManualColorControl.h"
#include "Engine.h"
#include "Monitors\AudioAnalysisMonitor.h"
#include "Monitors\BaseMonitor.h"
#include "AudioCapture.h"
#include "Processors\BaseAudioProcessor.h"
#include "Processors\BandSpectrum.h"
#include "Processors\SpectrumColor.h"
#include "Processors\TrebleStrobe.h"
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
static CAudioCapture cAudioCapture;
static CAudioAnalyzer cAudioAnalyzer;
static CAudioProcessors cAudioProcessors;
static CMonitorList vecMonitors;
static CIotDeviceList vecIotDevices;
static std::unique_ptr<CSharedManualColorControlWindow> cSharedManualColorControlWindow;
static std::vector<std::unique_ptr<CTUiFrameWindow>> vecTUiFrames;
static CThread cTickThread;

// -----------------------------------------------------------------------------

static HRESULT Initialize(_In_ HINSTANCE hInstance) noexcept;
static VOID Finalize() noexcept;

static VOID TimerTick() noexcept;

static VOID OnAudioData(_In_ const float *lpfPCM, _In_ UINT32 ulSamplesCount, _In_ UINT64 ullDevicePosition,
                        _In_ UINT64 ullCaptureQpcPosition);
static VOID OnErrorCallback(_In_ HRESULT hRes);

static HRESULT CreateSpectrumColorAudioProcessor(_Inout_ PanelGrid_t &sPanelGrid);
static HRESULT CreateBandSpectrumAudioProcessor(_Inout_ PanelGrid_t &sPanelGrid);
static HRESULT CreateTrebleStrobeAudioProcessor(_Inout_ PanelGrid_t &sPanelGrid);
static HRESULT CreateAudioAnalysisMonitor(_Inout_ PanelGrid_t &sPanelGrid);
static HRESULT CreateSharedManualColorControlWindow(_Inout_ PanelGrid_t &sPanelGrid);

static HRESULT LoadWindowPosition(_In_ CBaseAudioProcessor *lpAudioProc, _In_ CTUiWindow *lpAttachedWindow,
                                  _Inout_ PanelGrid_t &sPanelGrid, _Out_ INT &iX, _Out_ INT &iY);
static HRESULT LoadWindowPosition(_In_ CBaseMonitor *lpMonitor, _In_ CTUiWindow *lpAttachedWindow,
                                  _Inout_ PanelGrid_t &sPanelGrid, _Out_ INT &iX, _Out_ INT &iY);
static HRESULT LoadWindowPosition(_In_ CBaseIotDevice *lpIotDevice, _In_ CTUiWindow *lpAttachedWindow,
                                  _Inout_ PanelGrid_t &sPanelGrid, _Out_ INT &iX, _Out_ INT &iY);
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
    CAudioCapture::Config sAudioCaptureConfig;
    PanelGrid_t sPanelGrid;
    INT iX, iY;
    CTUiWindowManager::InitParams_t sWndMgrInitParams;
    const CmdLimeParameters &sCmdLineParams = GetCmdLimeParameters();
    HRESULT hRes;

    // Initialize audio capture engine settings from the selected command-line source.
    sAudioCaptureConfig = {};
    sAudioCaptureConfig.dwPid = sCmdLineParams.dwPid;
    sAudioCaptureConfig.bInputCapture = (sCmdLineParams.eAudioSource == CmdLineAudioSource_Input) ? TRUE : FALSE;
    sAudioCaptureConfig.szInputDeviceNameW = sCmdLineParams.strInputDeviceNameW.empty() ? nullptr
        : sCmdLineParams.strInputDeviceNameW.c_str();
    sAudioCaptureConfig.OnAudioDataCallback = OnAudioData;
    sAudioCaptureConfig.OnErrorCallback = OnErrorCallback;
    hRes = cAudioCapture.Initialize(sAudioCaptureConfig);
    if (FAILED(hRes))
    {
        if (sAudioCaptureConfig.bInputCapture != FALSE)
        {
            if (hRes == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && sAudioCaptureConfig.szInputDeviceNameW)
            {
                swprintf_s(szErrMsgBufW, 512, L"Unable to find audio input device \"%s\".", sAudioCaptureConfig.szInputDeviceNameW);
                MessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", MB_OK | MB_ICONERROR);
            }
            else if (hRes == HRESULT_FROM_WIN32(ERROR_DUP_NAME))
            {
                swprintf_s(szErrMsgBufW, 512, L"More than one audio input device matches \"%s\".",
                           sAudioCaptureConfig.szInputDeviceNameW);
                MessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", MB_OK | MB_ICONERROR);
            }
            else
            {
                ErrorMessageBoxW(nullptr, L"Unable to initialize audio input capture.", L"Audio Light Streamer", hRes);
            }
        }
        else if (sAudioCaptureConfig.dwPid != 0)
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

    // Keep audio analysis frequency scaling aligned with the negotiated capture format.
    hRes = cAudioAnalyzer.Initialize((float)cAudioCapture.GetSampleRate());
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to initialize audio analyzer.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    // Release the time-sensitive audio resources before terminal windows are destroyed.
    cWndMgr.OnDestroy([]() -> VOID
    {
        cTickThread.Stop();
        cAudioCapture.Finalize();
    });

    // Create the terminal window before attaching any processor or device panels.
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

    // Use the terminal dimensions to lay out the independently created panels.
    memset(&sPanelGrid, 0, sizeof(sPanelGrid));
    cWndMgr.Terminal()->GetTerminalSize(&sPanelGrid.iTotalCols, &sPanelGrid.iTotalRows);

    // Create the built-in audio producers and their diagnostic panels.
    hRes = CreateSpectrumColorAudioProcessor(sPanelGrid);
    if (FAILED(hRes))
    {
        return hRes;
    }

    hRes = CreateBandSpectrumAudioProcessor(sPanelGrid);
    if (FAILED(hRes))
    {
        return hRes;
    }

    hRes = CreateTrebleStrobeAudioProcessor(sPanelGrid);
    if (FAILED(hRes))
    {
        return hRes;
    }

    hRes = CreateAudioAnalysisMonitor(sPanelGrid);
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Instantiate every configured output, then let its connection worker validate the physical device.
    for (const auto &dev : GetAppConfig().m_vecDevices)
    {
        std::unique_ptr<CBaseIotDevice> iotDevice;
        std::unique_ptr<CTUiFrameWindow> frameWindow;

        switch (dev.eType)
        {
            case CConfig::DeviceType::Rgb5050:
                {
                    std::unique_ptr<CRgb5050IotDevice> rgbDevice;

                    hRes = TranslateToHresult([&]()
                    {
                        rgbDevice = std::make_unique<CRgb5050IotDevice>(cAudioProcessors);
                    });
                    if (FAILED(hRes))
                    {
                        swprintf_s(szErrMsgBufW, 512, L"Unable to create device \"%s\".", dev.strName.c_str());
                        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                        return hRes;
                    }

                    hRes = rgbDevice->SetupEffects(dev.vecEffects);
                    if (FAILED(hRes))
                    {
                        swprintf_s(szErrMsgBufW, 512, L"Unable to setup effects for device \"%s\".", dev.strName.c_str());
                        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                        return hRes;
                    }

                    hRes = rgbDevice->SetupCommInfo(dev.strHost.c_str(), dev.strUserName.c_str(), dev.aPrivateKey);
                    if (FAILED(hRes))
                    {
                        swprintf_s(szErrMsgBufW, 512, L"Unable to setup communication parameters for device \"%s\".", dev.strName.c_str());
                        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                        return hRes;
                    }

                    iotDevice = std::move(rgbDevice);
                }
                break;

            case CConfig::DeviceType::Dmx:
                {
                    std::unique_ptr<CDmxIotDevice> dmxDevice;

                    hRes = TranslateToHresult([&]()
                    {
                        dmxDevice = std::make_unique<CDmxIotDevice>(cAudioProcessors);
                    });
                    if (FAILED(hRes))
                    {
                        swprintf_s(szErrMsgBufW, 512, L"Unable to create device \"%s\".", dev.strName.c_str());
                        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                        return hRes;
                    }

                    hRes = dmxDevice->SetupFixtures(dev.vecDmxFixtures);
                    if (FAILED(hRes))
                    {
                        swprintf_s(szErrMsgBufW, 512, L"Unable to setup fixtures for device \"%s\".", dev.strName.c_str());
                        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                        return hRes;
                    }

                    hRes = dmxDevice->SetupCommInfo(dev.strHost.c_str(), dev.strUserName.c_str(), dev.aPrivateKey);
                    if (FAILED(hRes))
                    {
                        swprintf_s(szErrMsgBufW, 512, L"Unable to setup communication parameters for device \"%s\".", dev.strName.c_str());
                        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                        return hRes;
                    }

                    iotDevice = std::move(dmxDevice);
                }
                break;

            case CConfig::DeviceType::GoveeStrip:
                {
                    std::unique_ptr<CGoveeStripIotDevice> goveeDevice;

                    hRes = TranslateToHresult([&]()
                    {
                        goveeDevice = std::make_unique<CGoveeStripIotDevice>(cAudioProcessors);
                    });
                    if (FAILED(hRes))
                    {
                        swprintf_s(szErrMsgBufW, 512, L"Unable to create device \"%s\".", dev.strName.c_str());
                        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                        return hRes;
                    }

                    hRes = goveeDevice->SetupSupportsRazer(dev.bSupportsRazer);
                    if (FAILED(hRes))
                    {
                        swprintf_s(szErrMsgBufW, 512, L"Unable to setup Razer support for device \"%s\".", dev.strName.c_str());
                        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                        return hRes;
                    }

                    hRes = goveeDevice->SetupEffects(dev.vecEffects);
                    if (FAILED(hRes))
                    {
                        swprintf_s(szErrMsgBufW, 512, L"Unable to setup effects for device \"%s\".", dev.strName.c_str());
                        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                        return hRes;
                    }

                    hRes = goveeDevice->SetupCommInfo(dev.strHost.c_str());
                    if (FAILED(hRes))
                    {
                        swprintf_s(szErrMsgBufW, 512, L"Unable to setup communication parameters for device \"%s\".", dev.strName.c_str());
                        ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                        return hRes;
                    }

                    if (dev.bSupportsRazer != FALSE)
                    {
                        hRes = goveeDevice->SetupStripLength(dev.nStripLength);
                        if (FAILED(hRes))
                        {
                            swprintf_s(szErrMsgBufW, 512, L"Unable to setup strip length for device \"%s\".", dev.strName.c_str());
                            ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
                            return hRes;
                        }
                    }

                    iotDevice = std::move(goveeDevice);
                }
                break;

            default:
                ErrorMessageBoxW(nullptr, L"Unsupported IoT device type.", L"Audio Light Streamer", E_NOTIMPL);
                return E_NOTIMPL;
        }

        // Apply identity before attaching the device panel.
        hRes = iotDevice->SetupDeviceInfo(dev.strName.c_str());
        if (FAILED(hRes))
        {
            swprintf_s(szErrMsgBufW, 512, L"Unable to setup device information for device \"%s\".", dev.strName.c_str());
            ErrorMessageBoxW(nullptr, szErrMsgBufW, L"Audio Light Streamer", hRes);
            return hRes;
        }

        // Attach the device panel before starting its worker so status is visible from the first connection attempt.
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

    // Add the shared manual-color panel after its target devices have been registered.
    hRes = CreateSharedManualColorControlWindow(sPanelGrid);
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Render every panel once before asynchronous audio and timer updates begin.
    for (const auto &audioProc : cAudioProcessors.All())
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

    // Drive time-based fades and panel refreshes independently from audio callbacks.
    hRes = cTickThread.Start([](_In_ CThread &thread) -> DWORD
    {
        while (thread.CheckForAbort(10) == HRESULT_FROM_WIN32(ERROR_TIMEOUT))
        {
            TimerTick();
        }
        return 0;
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to start tick timer.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = cAudioCapture.StartCapture();
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to start audio capture.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    cWndMgr.Show();
    return hRes;
}

static VOID Finalize() noexcept
{
    cTickThread.Stop();
    cAudioCapture.Finalize();
    cWndMgr.Finalize();
    vecTUiFrames.clear();
    cSharedManualColorControlWindow.reset();
    vecIotDevices.clear();
    vecMonitors.clear();
    cAudioProcessors.Clear();
}

static VOID TimerTick() noexcept
{
    for (const auto &audioProc : cAudioProcessors.All())
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

static VOID OnAudioData(_In_ const float *lpfPCM, _In_ UINT32 ulSamplesCount, _In_ UINT64 ullDevicePosition,
                        _In_ UINT64 ullCaptureQpcPosition)
{
    CAudioAnalyzer::Frame_t sFrame;
    LARGE_INTEGER qpcCallback;

    cAudioAnalyzer.Analyze(lpfPCM, ulSamplesCount, sFrame);
    qpcCallback = {};
    ::QueryPerformanceCounter(&qpcCallback);
    sFrame.ullCaptureDevicePosition = ullDevicePosition;
    sFrame.ullCaptureQpcPosition = ullCaptureQpcPosition;
    sFrame.ullCallbackQpcPosition = static_cast<UINT64>(qpcCallback.QuadPart);

    for (const auto &audioProc : cAudioProcessors.All())
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
    ErrorMessageBoxW(cWndMgr.GetHwnd(), L"An error occurred while capturing audio.", L"Audio Light Streamer", hRes);
    cWndMgr.RequestShutdown(hRes);
}

static HRESULT CreateSpectrumColorAudioProcessor(_Inout_ PanelGrid_t &sPanelGrid)
{
    std::shared_ptr<CSpectrumColorAudioProcessor> spectrumColorAudioProc;
    std::unique_ptr<CTUiFrameWindow> frameWindow;
    CSpectrumColorAudioProcessor::Config sSpectrumColorProcConfig;
    INT iX, iY;
    HRESULT hRes;

    hRes = TranslateToHresult([&]()
    {
        spectrumColorAudioProc = std::make_shared<CSpectrumColorAudioProcessor>();
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create spectrum color audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    sSpectrumColorProcConfig = CSpectrumColorAudioProcessor::GetDefaultConfig();
    sSpectrumColorProcConfig.ulSampleRate = cAudioCapture.GetSampleRate();
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

    hRes = cAudioProcessors.Add(spectrumColorAudioProc);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to add spectrum color audio processor to list.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    return S_OK;
}

static HRESULT CreateBandSpectrumAudioProcessor(_Inout_ PanelGrid_t &sPanelGrid)
{
    std::shared_ptr<CBandSpectrumAudioProcessor> bandSpectrumAudioProc;
    std::unique_ptr<CTUiFrameWindow> frameWindow;
    INT iX, iY;
    HRESULT hRes;

    hRes = TranslateToHresult([&]()
    {
        bandSpectrumAudioProc = std::make_shared<CBandSpectrumAudioProcessor>();
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create band spectrum audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        frameWindow = std::make_unique<CTUiFrameWindow>(bandSpectrumAudioProc.get());
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create frame window for band spectrum audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = LoadWindowPosition(bandSpectrumAudioProc.get(), frameWindow.get(), sPanelGrid, iX, iY);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to load window position for band spectrum audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = cWndMgr.AttachWindow(frameWindow.get(), iX, iY);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to setup terminal console for band spectrum audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        vecTUiFrames.push_back(std::move(frameWindow));
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to add band spectrum frame to list.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = cAudioProcessors.Add(bandSpectrumAudioProc);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to add band spectrum audio processor to list.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    return S_OK;
}

static HRESULT CreateTrebleStrobeAudioProcessor(_Inout_ PanelGrid_t &sPanelGrid)
{
    std::shared_ptr<CTrebleStrobeAudioProcessor> trebleStrobeAudioProc;
    std::unique_ptr<CTUiFrameWindow> frameWindow;
    CTrebleStrobeAudioProcessor::Config sTrebleStrobeProcConfig;
    INT iX, iY;
    HRESULT hRes;

    hRes = TranslateToHresult([&]()
    {
        trebleStrobeAudioProc = std::make_shared<CTrebleStrobeAudioProcessor>();
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create treble strobe audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    sTrebleStrobeProcConfig = CTrebleStrobeAudioProcessor::GetDefaultConfig();
    hRes = trebleStrobeAudioProc->Initialize(sTrebleStrobeProcConfig, cAudioCapture.GetSampleRate());
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to initialize treble strobe audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        frameWindow = std::make_unique<CTUiFrameWindow>(trebleStrobeAudioProc.get());
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to create frame window for treble strobe audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = LoadWindowPosition(trebleStrobeAudioProc.get(), frameWindow.get(), sPanelGrid, iX, iY);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to load window position for treble strobe audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = cWndMgr.AttachWindow(frameWindow.get(), iX, iY);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to setup terminal console for treble strobe audio processor.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = TranslateToHresult([&]()
    {
        vecTUiFrames.push_back(std::move(frameWindow));
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to add treble strobe frame to list.", L"Audio Light Streamer", hRes);
        return hRes;
    }

    hRes = cAudioProcessors.Add(trebleStrobeAudioProc);
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to add treble strobe audio processor to list.", L"Audio Light Streamer", hRes);
        return hRes;
    }

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

    hRes = cAudioAnalysisMonitor->Initialize(cAudioAnalyzer.GetFftMagnitudeCount(), cAudioAnalyzer.GetFftMagnitudeFullScale(),
                                             (float)cAudioCapture.GetSampleRate(), cAudioAnalyzer.GetSpectralHopDurationSec());
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to initialize audio analysis monitor.", L"Audio Light Streamer", hRes);
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

    hRes = TranslateToHresult([&]()
    {
        // Collect the raw panel targets before constructing the shared manual-color controller.
        for (const auto &iotDevice : vecIotDevices)
        {
            if (iotDevice && iotDevice->SupportsManualColorActions() != FALSE)
            {
                vecSupportedDevices.push_back(iotDevice.get());
            }
        }
    });
    if (FAILED(hRes))
    {
        ErrorMessageBoxW(nullptr, L"Unable to enumerate devices for shared manual color control.", L"Audio Light Streamer", hRes);
        return hRes;
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

static HRESULT LoadWindowPosition(_In_ CBaseAudioProcessor *lpAudioProc, _In_ CTUiWindow *lpAttachedWindow,
                                  _Inout_ PanelGrid_t &sPanelGrid, _Out_ INT &iX, _Out_ INT &iY)
{
    return LoadWindowPositionCommon(lpAudioProc->LoadWindowPosition(iX, iY), lpAttachedWindow, sPanelGrid, iX, iY);
}

static HRESULT LoadWindowPosition(_In_ CBaseMonitor *lpMonitor, _In_ CTUiWindow *lpAttachedWindow, _Inout_ PanelGrid_t &sPanelGrid,
                                  _Out_ INT &iX, _Out_ INT &iY)
{
    return LoadWindowPositionCommon(lpMonitor->LoadWindowPosition(iX, iY), lpAttachedWindow, sPanelGrid, iX, iY);
}

static HRESULT LoadWindowPosition(_In_ CBaseIotDevice *lpIotDevice, _In_ CTUiWindow *lpAttachedWindow, _Inout_ PanelGrid_t &sPanelGrid,
                                  _Out_ INT &iX, _Out_ INT &iY)
{
    return LoadWindowPositionCommon(lpIotDevice->LoadWindowPosition(iX, iY), lpAttachedWindow, sPanelGrid, iX, iY);
}

static HRESULT LoadWindowPositionCommon(_In_ HRESULT hRes, _In_ CTUiWindow *lpWnd, _Inout_ PanelGrid_t &sPanelGrid, _Inout_ INT &iX,
                                        _Inout_ INT &iY)
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

        if (sPanelGrid.iTotalCols - sPanelGrid.iCurrentX < iWidth)
        {
            sPanelGrid.iCurrentX = 0;
            sPanelGrid.iCurrentY += sPanelGrid.iMaxRowHeight;
            sPanelGrid.iMaxRowHeight = 0;
        }
        iX = sPanelGrid.iCurrentX;
        iY = sPanelGrid.iCurrentY;
    }

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

    return S_OK;
}
