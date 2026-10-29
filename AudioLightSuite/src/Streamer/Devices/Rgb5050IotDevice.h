#pragma once

#include "AudioLightNodeIotDevice.h"
#include "..\AppConfig.h"
#include "Utils\RwMutex.h"
#include <vector>

// -----------------------------------------------------------------------------

class CRgb5050IotDevice : public CAudioLightNodeIotDevice
{
public:
    CRgb5050IotDevice(_In_ const CAudioProcessors &cAudioProcessors) noexcept;
    CRgb5050IotDevice(_In_ const CRgb5050IotDevice &) = delete;
    CRgb5050IotDevice(_Inout_ CRgb5050IotDevice &&) = delete;
    ~CRgb5050IotDevice() noexcept = default;

    CRgb5050IotDevice &operator=(_In_ const CRgb5050IotDevice &) = delete;
    CRgb5050IotDevice &operator=(_Inout_ CRgb5050IotDevice &&) = delete;

    HRESULT SetupEffects(_In_ const std::vector<CConfig::DeviceEffect> &vecEffects) noexcept;

    LPCSTR GetType() const noexcept final
    {
        return "Rgb5050";
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
        return DEVICE_FRAME_HEIGHT;
    }

protected:
    VOID OnIotConnectionStateChanged(_In_ BOOL bConnected) noexcept final;
    HRESULT ValidateDeviceConfiguration(_In_ const CIotCommClient::CDeviceConfigCommandResult &cConfig,
                                        _Out_writes_z_(nReasonLen) LPWSTR szReasonW, _In_ SIZE_T nReasonLen) noexcept final;
    BOOL SupportsManualColorActions() const noexcept final
    {
        return TRUE;
    }
    HRESULT OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept final;

private:
    BOOL GetAutomaticRGB(_Out_ BYTE &uRed, _Out_ BYTE &uGreen, _Out_ BYTE &uBlue) const noexcept;
    VOID GetCurrentRGBNoLock(_In_ ULONGLONG ullNow, _Out_ LPBYTE lpuRed, _Out_ LPBYTE lpuGreen, _Out_ LPBYTE lpuBlue) noexcept;
    VOID SendSetColorCommand(_In_ BYTE uRed, _In_ BYTE uGreen, _In_ BYTE uBlue) noexcept;
    VOID SendSetAndFadeToColorCommand(_In_ BYTE uRed, _In_ BYTE uGreen, _In_ BYTE uBlue, _In_ USHORT uFadeMs) noexcept;

private:
    typedef enum class OutputMode_e {
        None = 0,
        Rgb,
        White,
        UV
    } OutputMode;

    typedef struct CurrentState_s {
        ULONGLONG ullStartMs{ 0 };
        BYTE uRed{ 0 };
        BYTE uGreen{ 0 };
        BYTE uBlue{ 0 };
        BYTE uFixedBrightness{ 0 };
        USHORT uFadeMs{ 0 };
        BOOL bFixedIsActive{ FALSE };
    } CurrentState_t;

private:
    OutputMode m_eOutputMode{ OutputMode::None };
    CBaseAudioProcessor *m_lpAudioProcessor{ nullptr };

    CRwMutex m_cCurrentStateRwMutex;
    CurrentState_t m_sCurrentState{};
};
