#pragma once

#include "BaseIotDevice.h"
#include "..\Processors\SpectrumColor.h"

// -----------------------------------------------------------------------------

class CLedStrip5050IotDevice : public CBaseIotDevice
{
public:
    CLedStrip5050IotDevice() = default;
    CLedStrip5050IotDevice(_In_ const CLedStrip5050IotDevice &) = delete;
    CLedStrip5050IotDevice(_Inout_ CLedStrip5050IotDevice &&) = delete;
    ~CLedStrip5050IotDevice() = default;

    CLedStrip5050IotDevice &operator=(_In_ const CLedStrip5050IotDevice &) = delete;
    CLedStrip5050IotDevice &operator=(_Inout_ CLedStrip5050IotDevice &&) = delete;

    HRESULT SetupAudioProcessors(_In_ CAudioProcessorList &vecAudioProcessors) noexcept final;

    LPCSTR GetType() const noexcept final
    {
        return "Strip5050";
    }

    VOID FirstRender() noexcept final;

    VOID Process() noexcept final;

    VOID TimerTick() noexcept final;

    INT GetDefaultWidth() const noexcept final
    {
        return 31;
    }

    INT GetDefaultHeight() const noexcept final
    {
        return 5;
    }

protected:
    VOID OnIotConnectionStateChanged(_In_ BOOL bConnected) noexcept final;
    BOOL SupportsManualColorActions() const noexcept final
    {
        return TRUE;
    }
    HRESULT OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept final;

private:
    VOID GetCurrentRGBNoLock(_In_ ULONGLONG ullNow, _Out_ LPBYTE lpuRed, _Out_ LPBYTE lpuGreen, _Out_ LPBYTE lpuBlue) noexcept;
    VOID SendSetAndFadeToColorCommand(_In_ BYTE uRed, _In_ BYTE uGreen, _In_ BYTE uBlue, _In_ USHORT uFadeMs) noexcept;

private:
    typedef struct CurrentState_s {
        ULONGLONG ullStartMs{ 0 };
        BYTE uRed{ 0 };
        BYTE uGreen{ 0 };
        BYTE uBlue{ 0 };
        BYTE uFixedBrightness{ 0 };
        USHORT uFadeMs{ 0 };
    } CurrentState_t;

    CSpectrumColorAudioProcessor *m_lpSpectrumColorAudioProc{ nullptr };

    CMutex m_cStateMutex;
    CurrentState_t m_sCurrent{};
    BOOL m_bFixedIsActive{ FALSE };
};

