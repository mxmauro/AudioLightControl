#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atlbase.h>
#include <audioclient.h>
#include <functional>
#include <string>
#include <vector>
#include "Utils\Threads.h"

// -----------------------------------------------------------------------------

class CAudioCapture
{
public:
    typedef std::function<VOID (_In_ const float */*lpfPCM*/, _In_ UINT32 /*ulSamplesCount*/,
                                     _In_ UINT64 /*ullDevicePosition*/, _In_ UINT64 /*ullCaptureQpcPosition*/)> AudioDataCallback;
    typedef std::function<VOID (_In_ HRESULT /*hRes*/)> ErrorCallback;

    typedef struct Config_s {
        DWORD dwPid{ 0 };
        BOOL bInputCapture{ FALSE };
        LPCWSTR szInputDeviceNameW{ nullptr };
        AudioDataCallback OnAudioDataCallback{};
        ErrorCallback OnErrorCallback{};
    } Config;

public:
    CAudioCapture() noexcept = default;
    CAudioCapture(_In_ const CAudioCapture &) = delete;
    CAudioCapture(_Inout_ CAudioCapture &&) = delete;
    ~CAudioCapture() noexcept;

    CAudioCapture &operator=(_In_ const CAudioCapture &) = delete;
    CAudioCapture &operator=(_Inout_ CAudioCapture &&) = delete;

    HRESULT Initialize(_In_ Config &cConfig) noexcept;
    VOID Finalize() noexcept;

    HRESULT StartCapture() noexcept;
    VOID StopCapture() noexcept;

    BOOL IsCapturing() const noexcept
    {
        return m_audioClient ? TRUE : FALSE;
    };

    ULONG GetSampleRate() const noexcept
    {
        return m_sWaveFormatExt.Format.nSamplesPerSec;
    };

    static HRESULT GetInputAudioDevicesText(_Out_ std::wstring &strDevicesW) noexcept;

private:
    VOID ThreadProc() noexcept;
    VOID ConvertFrameData(_In_ LPBYTE lpData, _In_ UINT32 ulFramesCount) noexcept;

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
