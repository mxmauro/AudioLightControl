#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atlbase.h>
#include <audioclient.h>
#include <functional>
#include <vector>
#include "Utils\Threads.h"

// -----------------------------------------------------------------------------

class COutputAudioCapture
{
public:
    typedef std::function<VOID (_In_ const float */*lpfPCM*/, _In_ UINT32 /*ulSamplesCount*/)> AudioDataCallback;
    typedef std::function<VOID (_In_ HRESULT /*hRes*/)> ErrorCallback;

    typedef struct Config_s {
        DWORD dwPid{ 0 };
        AudioDataCallback OnAudioDataCallback{};
        ErrorCallback OnErrorCallback{};
    } Config;

public:
    COutputAudioCapture() = default;
    COutputAudioCapture(_In_ const COutputAudioCapture &) = delete;
    COutputAudioCapture(_Inout_ COutputAudioCapture &&) = delete;
    ~COutputAudioCapture();

    COutputAudioCapture &operator=(_In_ const COutputAudioCapture &) = delete;
    COutputAudioCapture &operator=(_Inout_ COutputAudioCapture &&) = delete;

    HRESULT Initialize(_In_ Config &cConfig);
    VOID Finalize();

    HRESULT StartCapture();
    VOID StopCapture();

    BOOL IsCapturing() const
    {
        return m_audioClient ? TRUE : FALSE;
    };

    ULONG GetSampleRate() const
    {
        return m_sWaveFormatExt.Format.nSamplesPerSec;
    };

private:
    VOID ThreadProc();
    VOID ConvertFrameData(_In_ LPBYTE lpData, _In_ UINT32 ulFramesCount);

private:
    static const UINT32 ulMaxChunkFrames = 8192;

    AudioDataCallback m_OnAudioDataCallback{ nullptr };
    ErrorCallback m_OnErrorCallback{ nullptr };

    CComPtr<IAudioClient> m_audioClient;
    CComPtr<IAudioCaptureClient> m_audioCaptureClient;
    WAVEFORMATEXTENSIBLE m_sWaveFormatExt{ 0 };
    CThread m_cThread;
    std::vector<float> m_vecFramesBuffer;
};
