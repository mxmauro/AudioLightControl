#include "OutputAudioCapture.h"
#include "Utils\MathHelpers.h"
#include "Utils\Exception2HR.h"
#include <mmdeviceapi.h>
#include <audioclientactivationparams.h>

#pragma comment(lib, "mmdevapi.lib")

// -----------------------------------------------------------------------------

#define MILLISECONDS_TO_100NANOSECONDS(durationMs) ((durationMs) * 1000 * 10)

static const REFERENCE_TIME hnsRequestedBufferDuration = MILLISECONDS_TO_100NANOSECONDS(100); // 100ms

// -----------------------------------------------------------------------------

class CCActivateAudioInterfaceAsyncCallback : public IActivateAudioInterfaceCompletionHandler
{
public:
    CCActivateAudioInterfaceAsyncCallback() = default;
    ~CCActivateAudioInterfaceAsyncCallback()
    {
        if (m_hCompletedEvent)
        {
            ::CloseHandle(m_hCompletedEvent);
        }
        if (m_lpAudioClient)
        {
            m_lpAudioClient->Release();
        }
    };

    HRESULT Init()
    {
        m_hCompletedEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_hCompletedEvent)
        {
            return HRESULT_FROM_WIN32(::GetLastError());
        }
        return S_OK;
    }

    HRESULT Wait()
    {
        ::WaitForSingleObject(m_hCompletedEvent, INFINITE);
        return m_hrActivate;
    };

    IAudioClient* GetAudioClient() const
    {
        return m_lpAudioClient;
    };

    STDMETHOD(ActivateCompleted)(_In_ IActivateAudioInterfaceAsyncOperation *lpActivateOperation)
    {
        IUnknown *lpUnk = nullptr;
        HRESULT hRes;

        hRes = lpActivateOperation->GetActivateResult(&m_hrActivate, &lpUnk);
        if (SUCCEEDED(hRes))
        {
            if (SUCCEEDED(m_hrActivate))
            {
                if (lpUnk)
                {
                    m_hrActivate = lpUnk->QueryInterface(__uuidof(IAudioClient), (void **)&m_lpAudioClient);
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
        if (lpUnk)
        {
            lpUnk->Release();
        }

        ::SetEvent(m_hCompletedEvent);
        return S_OK;
    };

    STDMETHOD(QueryInterface)(_In_ const IID &riid, _Out_ void **ppvObject)
    {
        if (riid == __uuidof(IAgileObject))
        {
            *ppvObject = this;
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    };

    STDMETHOD_(ULONG, AddRef)()
    {
        return 1;
    };

    STDMETHOD_(ULONG, Release)()
    {
        return 0;
    };

protected:
    HANDLE m_hCompletedEvent{ nullptr };
    HRESULT m_hrActivate{ E_FAIL };
    IAudioClient *m_lpAudioClient{ nullptr };
};

// -----------------------------------------------------------------------------

COutputAudioCapture::~COutputAudioCapture()
{
    Finalize();
}

HRESULT COutputAudioCapture::Initialize(_In_ Config &cConfig)
{
    HRESULT hRes;

    Finalize();

    if (!(cConfig.OnAudioDataCallback && cConfig.OnErrorCallback))
    {
        return E_INVALIDARG;
    }

    m_OnAudioDataCallback = cConfig.OnAudioDataCallback;
    m_OnErrorCallback = cConfig.OnErrorCallback;

    if (cConfig.dwPid == 0)
    {
        CComPtr<IMMDeviceEnumerator> devNum;
        CComPtr<IMMDevice> defaultAudioEpDevice;

        hRes = devNum.CoCreateInstance(__uuidof(MMDeviceEnumerator));
        if (SUCCEEDED(hRes))
        {
            hRes = devNum->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultAudioEpDevice);
            if (SUCCEEDED(hRes))
            {
                hRes = defaultAudioEpDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&m_audioClient);
            }
        }
    }
    else
    {
        CCActivateAudioInterfaceAsyncCallback cAsyncCallback;
        CComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;
        AUDIOCLIENT_ACTIVATION_PARAMS sParams;
        PROPVARIANT sParamsVt;

        // Setup activation params
        memset(&sParams, 0, sizeof(sParams));
        sParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        sParams.ProcessLoopbackParams.TargetProcessId = cConfig.dwPid;
        sParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

        // Put AUDIOCLIENT_ACTIVATION_PARAMS into a PROPVARIANT blob (VT_BLOB).
        sParamsVt.vt = VT_BLOB;
        sParamsVt.blob.cbSize = sizeof(sParams);
        sParamsVt.blob.pBlobData = (LPBYTE)&sParams;

        // Activate asynchronously
        hRes = cAsyncCallback.Init();
        if (SUCCEEDED(hRes))
        {
            hRes = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &sParamsVt, &cAsyncCallback, &asyncOp);
            if (SUCCEEDED(hRes))
            {
                hRes = cAsyncCallback.Wait();
                if (SUCCEEDED(hRes))
                {
                    m_audioClient = cAsyncCallback.GetAudioClient();
                }
            }
        }
    }
    if (FAILED(hRes))
    {
        goto done;
    }
    for (ULONG i = 0; i < 8; i++)
    {
        memset(&m_sWaveFormatExt, 0, sizeof(m_sWaveFormatExt));
        m_sWaveFormatExt.Format.cbSize = sizeof(m_sWaveFormatExt);
        m_sWaveFormatExt.Format.nSamplesPerSec = ((i & 1) == 0) ? 48000 : 44100;
        m_sWaveFormatExt.Format.wBitsPerSample = ((i & 2) == 0) ? 32 : 16;
        m_sWaveFormatExt.Format.nChannels = ((i & 4) == 0) ? 2 : 1;
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
        hRes = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, hnsRequestedBufferDuration,
                                       0, &m_sWaveFormatExt.Format, nullptr);
        if (SUCCEEDED(hRes))
        {
            break;
        }
    }
    if (FAILED(hRes))
    {
        goto done;
    }

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

    hRes = m_cThread.Start([this](_In_ CThread *)
    {
        ThreadProc();
        return 0;
    });

    // Done
done:
    if (FAILED(hRes))
    {
        Finalize();
    }
    return hRes;
}

VOID COutputAudioCapture::Finalize()
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

HRESULT COutputAudioCapture::StartCapture()
{
    return (m_audioClient) ? m_audioClient->Start() : E_NOT_VALID_STATE;
}

VOID COutputAudioCapture::StopCapture()
{
    if (m_audioClient)
    {
        m_audioClient->Stop();
    }
}

VOID COutputAudioCapture::ThreadProc()
{
    LPBYTE lpData;
    UINT32 ulFramesCount, ulPacketLength;
    DWORD dwFlags;
    HRESULT hRes;

    hRes = S_OK;
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

            hRes = m_audioCaptureClient->GetBuffer(&lpData, &ulFramesCount, &dwFlags, nullptr, nullptr);
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

                // Call callback
                m_OnAudioDataCallback(m_vecFramesBuffer.data(), ulFramesCount);
            }

            ulPacketLength = 0;
            hRes = m_audioCaptureClient->GetNextPacketSize(&ulPacketLength);
            if (FAILED(hRes))
            {
                break;
            }
        }
    }

    if (FAILED(hRes))
    {
        m_OnErrorCallback(hRes);
    }
}

VOID COutputAudioCapture::ConvertFrameData(_In_ LPBYTE lpData, _In_ UINT32 ulFramesCount)
{
    typedef union _SOURCE_SAMPLES_PTR {
        const float *lpf32;
        const SHORT *lpn16;
    } SOURCE_SAMPLES_PTR;
    UINT32 i;
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
