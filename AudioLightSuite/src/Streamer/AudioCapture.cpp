#include "AudioCapture.h"
#include "Utils\MathHelpers.h"
#include "Utils\Exception2HR.h"
#include "Utils\Timer.h"
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclientactivationparams.h>
#include <new>

#pragma comment(lib, "mmdevapi.lib")

// -----------------------------------------------------------------------------

#define MILLISECONDS_TO_100NANOSECONDS(durationMs) ((durationMs) * 1000 * 10)

static const REFERENCE_TIME hnsRequestedBufferDuration = MILLISECONDS_TO_100NANOSECONDS(100); // 100ms

/*
class KickGenerator
{
public:
    static constexpr float SampleRate = 48000.0f;
    static constexpr std::size_t BlockSize = 480;

    // kicksPerSecond = 4.0 -> one kick every 250 ms
    explicit KickGenerator(float kicksPerSecond = 4.0f) : m_kicksPerSecond(kicksPerSecond)
    {
        m_samplesPerKick = static_cast<uint32_t>(SampleRate / kicksPerSecond);
    }

    void Generate(float *buffer)
    {
        for (std::size_t i = 0; i < BlockSize; ++i)
        {
            buffer[i] = GenerateSample();
            if (buffer[i] >= -MATH_EPSILON && buffer[i] <= MATH_EPSILON)
            {
                buffer[i] = 0.0f;
            }
        }
    }

private:
    float GenerateSample()
    {
        // Start a new kick.
        if (m_samplesUntilKick == 0)
        {
            m_kickSample = 0;
            m_phase = 0.0f;
            m_samplesUntilKick = m_samplesPerKick;
        }

        --m_samplesUntilKick;

        const float t = static_cast<float>(m_kickSample) / SampleRate;

        float sample = 0.0f;

        // Generate approximately 200 ms of kick audio.
        if (t < 0.200f)
        {
            // Pitch envelope:
            // starts around 150 Hz and quickly falls toward 45 Hz.
            const float frequency = 45.0f + (150.0f - 45.0f) * std::exp(-t / 0.025f);

            // Amplitude envelope.
            const float amplitude = std::exp(-t / 0.080f);

            // Phase accumulator.
            m_phase += 2.0f * Pi * frequency / SampleRate;

            sample = amplitude * std::sin(m_phase);
        }

        ++m_kickSample;

        return sample;
    }

private:
    static constexpr float Pi = 3.14159265358979323846f;

    float m_kicksPerSecond = 4.0f;

    uint32_t m_samplesPerKick = 12000;
    uint32_t m_samplesUntilKick = 0;
    uint32_t m_kickSample = 0;

    float m_phase = 0.0f;
};

KickGenerator kickGenerator(4.0f);
*/

// -----------------------------------------------------------------------------

static HRESULT ActivateInputAudioEndpoint(_In_opt_ LPCWSTR szDeviceNameW, _Out_ IAudioClient **lplpAudioClient) noexcept;
static HRESULT ActivateDefaultOutputMixerAudioEndpoint(_Out_ IAudioClient **lplpAudioClient) noexcept;
static HRESULT ActivateProcessOutputMixerAudioEndpoint(_In_ DWORD dwPid, _Out_ IAudioClient **lplpAudioClient) noexcept;
static HRESULT GetActiveInputAudioEndpoints(_Out_ CComPtr<IMMDeviceEnumerator> &devNum,
                                            _Out_ CComPtr<IMMDeviceCollection> &inputDevices) noexcept;
static HRESULT GetAudioEndpointFriendlyName(_In_ IMMDevice *lpDevice, _Out_ std::wstring &strFriendlyNameW) noexcept;
static HRESULT GetAudioEndpointId(_In_ IMMDevice *lpDevice, _Out_ std::wstring &strIdW) noexcept;
static BOOL ContainsCaseInsensitive(_In_ LPCWSTR szTextW, _In_ LPCWSTR szValueW) noexcept;

// -----------------------------------------------------------------------------

class CCActivateAudioInterfaceAsyncCallback : public IActivateAudioInterfaceCompletionHandler
{
public:
    CCActivateAudioInterfaceAsyncCallback() noexcept = default;
    CCActivateAudioInterfaceAsyncCallback(_In_ const CCActivateAudioInterfaceAsyncCallback &) = delete;
    CCActivateAudioInterfaceAsyncCallback(_Inout_ CCActivateAudioInterfaceAsyncCallback &&) = delete;
    ~CCActivateAudioInterfaceAsyncCallback() noexcept;

    CCActivateAudioInterfaceAsyncCallback &operator=(_In_ const CCActivateAudioInterfaceAsyncCallback &) = delete;
    CCActivateAudioInterfaceAsyncCallback &operator=(_Inout_ CCActivateAudioInterfaceAsyncCallback &&) = delete;

    HRESULT Init() noexcept;

    HRESULT Wait() noexcept;

    IAudioClient *GetAudioClient() const noexcept;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(_In_ const IID &riid, _Out_ void **ppvObject) noexcept;
    ULONG STDMETHODCALLTYPE AddRef() noexcept;
    ULONG STDMETHODCALLTYPE Release() noexcept;

    // IActivateAudioInterfaceCompletionHandler
    HRESULT STDMETHODCALLTYPE ActivateCompleted(_In_ IActivateAudioInterfaceAsyncOperation *lpActivateOperation) noexcept;

protected:
    HANDLE m_hCompletedEvent{ nullptr };
    HRESULT m_hrActivate{ E_FAIL };
    IAudioClient *m_lpAudioClient{ nullptr };
};

// -----------------------------------------------------------------------------

CAudioCapture::~CAudioCapture() noexcept
{
    // Reset any prior endpoint before constructing a new capture pipeline.
    Finalize();
}

HRESULT CAudioCapture::Initialize(_In_ Config &cConfig) noexcept
{
    HRESULT hRes;

    Finalize();

    if (!(cConfig.OnAudioDataCallback && cConfig.OnErrorCallback))
    {
        return E_INVALIDARG;
    }

    hRes = TranslateToHresult([&]()
    {
        m_OnAudioDataCallback = cConfig.OnAudioDataCallback;
        m_OnErrorCallback = cConfig.OnErrorCallback;
    });
    if (FAILED(hRes))
    {
        Finalize();
        return hRes;
    }

    // Select the requested input, default output mix, or process loopback endpoint.
    if (cConfig.bInputCapture != FALSE)
    {
        hRes = ActivateInputAudioEndpoint(cConfig.szInputDeviceNameW, &m_audioClient);
    }
    else if (cConfig.dwPid == 0)
    {
        hRes = ActivateDefaultOutputMixerAudioEndpoint(&m_audioClient);
    }
    else
    {
        hRes = ActivateProcessOutputMixerAudioEndpoint(cConfig.dwPid, &m_audioClient);
    }
    if (FAILED(hRes))
    {
        goto done;
    }
    // Negotiate a broadly compatible PCM or floating-point shared-mode format.
    for (ULONG i = 0; i < 12; i++)
    {
        memset(&m_sWaveFormatExt, 0, sizeof(m_sWaveFormatExt));
        m_sWaveFormatExt.Format.cbSize = sizeof(m_sWaveFormatExt);
        m_sWaveFormatExt.Format.nSamplesPerSec = ((i % 2) == 0) ? 48000 : 44100;
        m_sWaveFormatExt.Format.wBitsPerSample = 32 - 8 * ((i / 2) % 3); // 32, 24 or 16
        m_sWaveFormatExt.Format.nChannels = ((i / 6) == 0) ? 2 : 1;
        m_sWaveFormatExt.Format.nBlockAlign = (m_sWaveFormatExt.Format.wBitsPerSample >> 3) * m_sWaveFormatExt.Format.nChannels;
        m_sWaveFormatExt.Format.nAvgBytesPerSec = m_sWaveFormatExt.Format.nSamplesPerSec * m_sWaveFormatExt.Format.nBlockAlign;
        m_sWaveFormatExt.Samples.wValidBitsPerSample = m_sWaveFormatExt.Format.wBitsPerSample;
        if (m_sWaveFormatExt.Format.wBitsPerSample == 32)
        {
            m_sWaveFormatExt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            memcpy(&m_sWaveFormatExt.SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof(GUID));
        }
        else
        {
            m_sWaveFormatExt.Format.wFormatTag = WAVE_FORMAT_PCM;
        }

        // In shared mode this requests the stream buffer duration, but GetBuffer() packet sizes
        // still follow the audio engine period and may be much smaller, such as 480 frames at 48 kHz. (10ms of audio)
        hRes = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                         (cConfig.bInputCapture != FALSE) ? 0 : AUDCLNT_STREAMFLAGS_LOOPBACK,
                                         hnsRequestedBufferDuration, 0, &m_sWaveFormatExt.Format, nullptr);
        if (SUCCEEDED(hRes))
        {
            break;
        }
    }
    if (FAILED(hRes))
    {
        goto done;
    }

    // Reserve one normalized sample buffer for the capture thread.
    hRes = TranslateToHresult([&]
    {
        m_vecFramesBuffer.resize((size_t)ulMaxChunkFrames);
    });
    if (FAILED(hRes))
    {
        goto done;
    }

    hRes = m_audioClient->GetService(__uuidof(IAudioCaptureClient), (void **)&m_audioCaptureClient);
    if (FAILED(hRes))
    {
        goto done;
    }

    // Start the worker only after the endpoint and buffer are ready.
    hRes = m_cThread.Start([this](_In_ CThread &)
    {
        ThreadProc();
    });

done:
    if (FAILED(hRes))
    {
        Finalize();
    }
    return hRes;
}

VOID CAudioCapture::Finalize() noexcept
{
    StopCapture();
    m_cThread.Stop();
    if (m_audioClient)
    {
        m_audioClient->Stop();
    }
    m_audioClient.Release();
    m_audioCaptureClient.Release();
    m_vecFramesBuffer.clear();
    memset(&m_sWaveFormatExt, 0, sizeof(m_sWaveFormatExt));
    m_OnAudioDataCallback = nullptr;
    m_OnErrorCallback = nullptr;
}

HRESULT CAudioCapture::StartCapture() noexcept
{
    return (m_audioClient) ? m_audioClient->Start() : E_NOT_VALID_STATE;
}

VOID CAudioCapture::StopCapture() noexcept
{
    if (m_audioClient)
    {
        m_audioClient->Stop();
    }
}

VOID CAudioCapture::ThreadProc() noexcept
{
    LPBYTE lpData;
    UINT32 ulFramesCount, ulPacketLength;
    UINT64 ullDevicePosition, ullCaptureQpcPosition;
    DWORD dwFlags;
    HRESULT hRes;

    hRes = S_OK;
    try
    {
        while (::WaitForSingleObject(m_cThread.GetExitEvent(), 0) != WAIT_OBJECT_0)
        {
            ulPacketLength = 0;
            hRes = m_audioCaptureClient->GetNextPacketSize(&ulPacketLength);
            if (FAILED(hRes))
            {
                break;
            }
            if (ulPacketLength == 0)
            {
                if (::WaitForSingleObject(m_cThread.GetExitEvent(), 5) == WAIT_OBJECT_0)
                {
                    hRes = S_OK;
                    break;
                }
                continue;
            }

            while (ulPacketLength != 0)
            {
                lpData = nullptr;
                ulFramesCount = 0;
                dwFlags = 0;
                ullDevicePosition = 0;
                ullCaptureQpcPosition = 0;

                hRes = m_audioCaptureClient->GetBuffer(&lpData, &ulFramesCount, &dwFlags, &ullDevicePosition, &ullCaptureQpcPosition);
                if (FAILED(hRes))
                {
                    break;
                }

                if (ulFramesCount > 0)
                {
                    if (ulFramesCount > ulMaxChunkFrames)
                    {
                        ulFramesCount = ulMaxChunkFrames;
                    }

                    if ((dwFlags & AUDCLNT_BUFFERFLAGS_SILENT) == 0)
                    {
                        ConvertFrameData(lpData, ulFramesCount);
                        // kickGenerator.Generate(m_vecFramesBuffer.data());
                    }
                    else
                    {
                        memset(m_vecFramesBuffer.data(), 0, sizeof(float) * (size_t)ulFramesCount);
                    }
                    hRes = m_audioCaptureClient->ReleaseBuffer(ulFramesCount);
                    if (FAILED(hRes))
                    {
                        break;
                    }

                    m_OnAudioDataCallback(m_vecFramesBuffer.data(), ulFramesCount, ullDevicePosition, ullCaptureQpcPosition);
                }

                ulPacketLength = 0;
                hRes = m_audioCaptureClient->GetNextPacketSize(&ulPacketLength);
                if (FAILED(hRes))
                {
                    break;
                }
            }
        }
    }
    catch (HRESULT hr)
    {
        hRes = hr;
    }
    catch (const std::bad_alloc &)
    {
        hRes = E_OUTOFMEMORY;
    }
    catch (...)
    {
        hRes = E_UNEXPECTED;
    }

    if (FAILED(hRes))
    {
        try
        {
            m_OnErrorCallback(hRes);
        }
        catch (...)
        {
        }
    }
}

VOID CAudioCapture::ConvertFrameData(_In_ LPBYTE lpData, _In_ UINT32 ulFramesCount) noexcept
{
    typedef union _SOURCE_SAMPLES_PTR {
        const float *lpf32;
        const BYTE *lpn24;
        const SHORT *lpn16;
    } SOURCE_SAMPLES_PTR;
    UINT32 i;
    INT32 iVal[2];
    float x, *lpfFramesBuffer;
    SOURCE_SAMPLES_PTR uSourceSamples;

    lpfFramesBuffer = m_vecFramesBuffer.data();

    switch (m_sWaveFormatExt.Format.wBitsPerSample)
    {
        case 32:
            uSourceSamples.lpf32 = (const float *)lpData;
            if (m_sWaveFormatExt.Format.nChannels == 1)
            {
                for (i = 0; i < ulFramesCount; i++)
                {
                    x = uSourceSamples.lpf32[i];
                    lpfFramesBuffer[i] = (x >= -MATH_EPSILON && x <= MATH_EPSILON) ? 0.0f : x;
                }
            }
            else
            {
                for (i = 0; i < ulFramesCount; i++, uSourceSamples.lpf32 += 2)
                {
                    x = (uSourceSamples.lpf32[0] + uSourceSamples.lpf32[1]) * 0.5f;
                    lpfFramesBuffer[i] = (x >= -MATH_EPSILON && x <= MATH_EPSILON) ? 0.0f : x;
                }
            }
            break;

        case 24:
            uSourceSamples.lpn24 = (const BYTE *)lpData;
            if (m_sWaveFormatExt.Format.nChannels == 1)
            {
                for (i = 0; i < ulFramesCount; i++, uSourceSamples.lpn24 += 3)
                {
                    iVal[0] = (INT32)((UINT32)uSourceSamples.lpn24[0] | ((UINT32)uSourceSamples.lpn24[1] << 8) |
                                      ((UINT32)uSourceSamples.lpn24[2] << 16));
                    if ((iVal[0] & 0x00800000) != 0)
                    {
                        iVal[0] -= 0x01000000;
                    }

                    x = (float)iVal[0] / 8388608.0f;
                    lpfFramesBuffer[i] = (x >= -MATH_EPSILON && x <= MATH_EPSILON) ? 0.0f : x;
                }
            }
            else
            {
                for (i = 0; i < ulFramesCount; i++, uSourceSamples.lpn24 += 6)
                {
                    iVal[0] = (INT32)((UINT32)uSourceSamples.lpn24[0] | ((UINT32)uSourceSamples.lpn24[1] << 8) |
                                      ((UINT32)uSourceSamples.lpn24[2] << 16));
                    if ((iVal[0] & 0x00800000) != 0)
                    {
                        iVal[0] -= 0x01000000;
                    }
                    iVal[1] = (INT32)((UINT32)uSourceSamples.lpn24[3] | ((UINT32)uSourceSamples.lpn24[4] << 8) |
                                      ((UINT32)uSourceSamples.lpn24[5] << 16));
                    if ((iVal[1] & 0x00800000) != 0)
                    {
                        iVal[1] -= 0x01000000;
                    }

                    x = (float)(iVal[0] + iVal[1]) / 16777216.0f;
                    lpfFramesBuffer[i] = (x >= -MATH_EPSILON && x <= MATH_EPSILON) ? 0.0f : x;
                }
            }
            break;

        case 16:
            uSourceSamples.lpn16 = (const SHORT *)lpData;
            if (m_sWaveFormatExt.Format.nChannels == 1)
            {
                for (i = 0; i < ulFramesCount; i++)
                {
                    x = (float)(*uSourceSamples.lpn16++) / 32768.0f;
                    lpfFramesBuffer[i] = (x >= -MATH_EPSILON && x <= MATH_EPSILON) ? 0.0f : x;
                }
            }
            else
            {
                for (i = 0; i < ulFramesCount; i++, uSourceSamples.lpn16 += 2)
                {
                    x = ((float)uSourceSamples.lpn16[0] + (float)uSourceSamples.lpn16[1]) / 65536.0f;
                    lpfFramesBuffer[i] = (x >= -MATH_EPSILON && x <= MATH_EPSILON) ? 0.0f : x;
                }
            }
            break;
    }
}

HRESULT CAudioCapture::GetInputAudioDevicesText(_Out_ std::wstring &strDevicesW) noexcept
{
    CComPtr<IMMDeviceEnumerator> devNum;
    CComPtr<IMMDeviceCollection> inputDevices;
    CComPtr<IMMDevice> defaultInputDevice;
    std::wstring strDefaultDeviceIdW;
    HRESULT hRes;
    UINT uDeviceCount;

    strDevicesW.clear();

    hRes = GetActiveInputAudioEndpoints(devNum, inputDevices);
    if (FAILED(hRes))
    {
        return hRes;
    }
    hRes = inputDevices->GetCount(&uDeviceCount);
    if (FAILED(hRes))
    {
        return hRes;
    }
    if (uDeviceCount == 0)
    {
        return TranslateToHresult([&]()
        {
            strDevicesW = L"No audio input devices found.";
        });
    }

    hRes = devNum->GetDefaultAudioEndpoint(eCapture, eMultimedia, &defaultInputDevice);
    if (SUCCEEDED(hRes))
    {
        hRes = GetAudioEndpointId(defaultInputDevice, strDefaultDeviceIdW);
        if (FAILED(hRes))
        {
            return hRes;
        }
    }

    return TranslateToHresult([&]()
    {
        strDevicesW = L"List of input devices:\r\n";
        for (UINT u = 0; u < uDeviceCount; u++)
        {
            CComPtr<IMMDevice> inputDevice;
            std::wstring strFriendlyNameW;
            std::wstring strDeviceIdW;

            hRes = inputDevices->Item(u, &inputDevice);
            if (SUCCEEDED(hRes))
            {
                hRes = GetAudioEndpointFriendlyName(inputDevice, strFriendlyNameW);
            }
            if (SUCCEEDED(hRes))
            {
                hRes = GetAudioEndpointId(inputDevice, strDeviceIdW);
            }
            if (FAILED(hRes))
            {
                throw (HRESULT)hRes;
            }

            strDevicesW += L"\r\n\x2022 ";
            strDevicesW += strFriendlyNameW;
            if (!strDefaultDeviceIdW.empty() && strDeviceIdW == strDefaultDeviceIdW)
            {
                strDevicesW += L" (default)";
            }
        }
    });
}

// -----------------------------------------------------------------------------

static HRESULT ActivateInputAudioEndpoint(_In_opt_ LPCWSTR szDeviceNameW, _Out_ IAudioClient **lplpAudioClient) noexcept
{
    CComPtr<IMMDeviceEnumerator> devNum;
    CComPtr<IMMDeviceCollection> inputDevices;
    CComPtr<IMMDevice> inputDevice;
    HRESULT hRes;

    if (!lplpAudioClient)
    {
        return E_POINTER;
    }
    *lplpAudioClient = nullptr;

    if (!szDeviceNameW || *szDeviceNameW == 0)
    {
        hRes = devNum.CoCreateInstance(__uuidof(MMDeviceEnumerator));
        if (FAILED(hRes))
        {
            return hRes;
        }
        hRes = devNum->GetDefaultAudioEndpoint(eCapture, eMultimedia, &inputDevice);
    }
    else
    {
        CComPtr<IMMDevice> partialMatchDevice;
        UINT uPartialMatchCount{ 0 };
        UINT uDeviceCount;

        hRes = GetActiveInputAudioEndpoints(devNum, inputDevices);
        if (FAILED(hRes))
        {
            return hRes;
        }
        hRes = inputDevices->GetCount(&uDeviceCount);
        if (FAILED(hRes))
        {
            return hRes;
        }

        for (UINT u = 0; u < uDeviceCount; u++)
        {
            CComPtr<IMMDevice> candidateDevice;
            std::wstring strFriendlyNameW;

            hRes = inputDevices->Item(u, &candidateDevice);
            if (SUCCEEDED(hRes))
            {
                hRes = GetAudioEndpointFriendlyName(candidateDevice, strFriendlyNameW);
            }
            if (FAILED(hRes))
            {
                return hRes;
            }

            if (_wcsicmp(strFriendlyNameW.c_str(), szDeviceNameW) == 0)
            {
                if (inputDevice)
                {
                    return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
                }
                inputDevice = candidateDevice;
            }
            else if (ContainsCaseInsensitive(strFriendlyNameW.c_str(), szDeviceNameW) != FALSE)
            {
                partialMatchDevice = candidateDevice;
                uPartialMatchCount++;
            }
        }

        if (!inputDevice)
        {
            if (uPartialMatchCount == 1)
            {
                inputDevice = partialMatchDevice;
            }
            else if (uPartialMatchCount > 1)
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
            else
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            }
        }
    }
    if (FAILED(hRes))
    {
        return hRes;
    }

    return inputDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)lplpAudioClient);
}

static HRESULT GetActiveInputAudioEndpoints(_Out_ CComPtr<IMMDeviceEnumerator> &devNum,
                                            _Out_ CComPtr<IMMDeviceCollection> &inputDevices) noexcept
{
    HRESULT hRes;

    devNum.Release();
    inputDevices.Release();

    hRes = devNum.CoCreateInstance(__uuidof(MMDeviceEnumerator));
    if (FAILED(hRes))
    {
        return hRes;
    }

    return devNum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &inputDevices);
}

static HRESULT GetAudioEndpointFriendlyName(_In_ IMMDevice *lpDevice, _Out_ std::wstring &strFriendlyNameW) noexcept
{
    CComPtr<IPropertyStore> propertyStore;
    PROPVARIANT sFriendlyName;
    HRESULT hRes;

    if (!lpDevice)
    {
        return E_POINTER;
    }

    strFriendlyNameW.clear();
    PropVariantInit(&sFriendlyName);
    hRes = lpDevice->OpenPropertyStore(STGM_READ, &propertyStore);
    if (SUCCEEDED(hRes))
    {
        hRes = propertyStore->GetValue(PKEY_Device_FriendlyName, &sFriendlyName);
    }
    if (SUCCEEDED(hRes) && (sFriendlyName.vt != VT_LPWSTR || !sFriendlyName.pwszVal))
    {
        hRes = E_UNEXPECTED;
    }
    if (SUCCEEDED(hRes))
    {
        hRes = TranslateToHresult([&]()
        {
            strFriendlyNameW = sFriendlyName.pwszVal;
        });
    }
    PropVariantClear(&sFriendlyName);
    return hRes;
}

static HRESULT GetAudioEndpointId(_In_ IMMDevice *lpDevice, _Out_ std::wstring &strIdW) noexcept
{
    LPWSTR szIdW;
    HRESULT hRes;

    if (!lpDevice)
    {
        return E_POINTER;
    }

    strIdW.clear();
    szIdW = nullptr;
    hRes = lpDevice->GetId(&szIdW);
    if (SUCCEEDED(hRes))
    {
        hRes = TranslateToHresult([&]()
        {
            strIdW = szIdW;
        });
    }
    if (szIdW)
    {
        ::CoTaskMemFree(szIdW);
    }
    return hRes;
}

static BOOL ContainsCaseInsensitive(_In_ LPCWSTR szTextW, _In_ LPCWSTR szValueW) noexcept
{
    SIZE_T uValueLength;

    if (!szTextW || !szValueW)
    {
        return FALSE;
    }

    uValueLength = wcslen(szValueW);
    if (uValueLength == 0)
    {
        return FALSE;
    }

    while (*szTextW != 0)
    {
        if (_wcsnicmp(szTextW, szValueW, uValueLength) == 0)
        {
            return TRUE;
        }
        szTextW++;
    }

    return FALSE;
}

static HRESULT ActivateDefaultOutputMixerAudioEndpoint(_Out_ IAudioClient **lplpAudioClient) noexcept
{
    CComPtr<IMMDeviceEnumerator> devNum;
    CComPtr<IMMDevice> defaultAudioEpDevice;
    HRESULT hRes;

    hRes = devNum.CoCreateInstance(__uuidof(MMDeviceEnumerator));
    if (FAILED(hRes))
    {
        return hRes;
    }

    hRes = devNum->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultAudioEpDevice);
    if (FAILED(hRes))
    {
        return hRes;
    }

    return defaultAudioEpDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)lplpAudioClient);
}

static HRESULT ActivateProcessOutputMixerAudioEndpoint(_In_ DWORD dwPid, _Out_ IAudioClient **lplpAudioClient) noexcept
{
    CCActivateAudioInterfaceAsyncCallback cAsyncCallback;
    CComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;
    AUDIOCLIENT_ACTIVATION_PARAMS sParams;
    PROPVARIANT sParamsVt;
    HRESULT hRes;

    // Setup activation params
    memset(&sParams, 0, sizeof(sParams));
    sParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    sParams.ProcessLoopbackParams.TargetProcessId = dwPid;
    sParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    // Put AUDIOCLIENT_ACTIVATION_PARAMS into a PROPVARIANT blob (VT_BLOB).
    sParamsVt.vt = VT_BLOB;
    sParamsVt.blob.cbSize = sizeof(sParams);
    sParamsVt.blob.pBlobData = (LPBYTE)&sParams;

    // Activate asynchronously
    hRes = cAsyncCallback.Init();
    if (FAILED(hRes))
    {
        return hRes;
    }

    hRes = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &sParamsVt, &cAsyncCallback,
                                       &asyncOp);
    if (FAILED(hRes))
    {
        return hRes;
    }

    hRes = cAsyncCallback.Wait();
    if (FAILED(hRes))
    {
        return hRes;
    }

    *lplpAudioClient = cAsyncCallback.GetAudioClient();
    (*lplpAudioClient)->AddRef();

    return S_OK;
}

// -----------------------------------------------------------------------------

CCActivateAudioInterfaceAsyncCallback::~CCActivateAudioInterfaceAsyncCallback() noexcept
{
    if (m_hCompletedEvent)
    {
        ::CloseHandle(m_hCompletedEvent);
    }
    if (m_lpAudioClient)
    {
        m_lpAudioClient->Release();
    }
}

HRESULT CCActivateAudioInterfaceAsyncCallback::Init()  noexcept
{
    m_hCompletedEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_hCompletedEvent)
    {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    return S_OK;
}

HRESULT CCActivateAudioInterfaceAsyncCallback::Wait() noexcept
{
    ::WaitForSingleObject(m_hCompletedEvent, INFINITE);
    return m_hrActivate;
}

IAudioClient *CCActivateAudioInterfaceAsyncCallback::GetAudioClient() const noexcept
{
    return m_lpAudioClient;
}

HRESULT STDMETHODCALLTYPE CCActivateAudioInterfaceAsyncCallback::QueryInterface(_In_ const IID &riid, _Out_ void **ppvObject) noexcept
{
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAgileObject))
    {
        *ppvObject = this;
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CCActivateAudioInterfaceAsyncCallback::AddRef() noexcept
{
    return 1;
}

ULONG STDMETHODCALLTYPE CCActivateAudioInterfaceAsyncCallback::Release() noexcept
{
    return 1;
}

HRESULT STDMETHODCALLTYPE
CCActivateAudioInterfaceAsyncCallback::ActivateCompleted(_In_ IActivateAudioInterfaceAsyncOperation *lpActivateOperation) noexcept
{
    CComPtr<IUnknown> unk;
    HRESULT hRes;

    hRes = lpActivateOperation->GetActivateResult(&m_hrActivate, &unk);
    if (SUCCEEDED(hRes))
    {
        if (SUCCEEDED(m_hrActivate))
        {
            if (unk)
            {
                m_hrActivate = unk->QueryInterface(__uuidof(IAudioClient), (void **)&m_lpAudioClient);
            }
            else
            {
                m_hrActivate = E_NOINTERFACE;
            }
        }
    }
    else
    {
        m_hrActivate = hRes;
    }

    ::SetEvent(m_hCompletedEvent);
    return S_OK;
}
