#pragma once

#include "..\Devices\AudioLightNodeDevice.h"
#include "BaseLighting.h"
#include "..\AppConfig.h"
#include "Utils\RwMutex.h"
#include <vector>

// -----------------------------------------------------------------------------

class CRgb5050Lighting : public CBaseLighting, public IAudioLightNodeLighting
{
public:
    CRgb5050Lighting(_In_ const CAudioProcessors &cAudioProcessors) noexcept;
    CRgb5050Lighting(_In_ const CRgb5050Lighting &) = delete;
    CRgb5050Lighting(_Inout_ CRgb5050Lighting &&) = delete;
    ~CRgb5050Lighting() noexcept override = default;

    CRgb5050Lighting &operator=(_In_ const CRgb5050Lighting &) = delete;
    CRgb5050Lighting &operator=(_Inout_ CRgb5050Lighting &&) = delete;

    HRESULT SetupEffects(_In_ const std::vector<CConfig::DeviceEffect> &vecEffects) noexcept;
    HRESULT SetupNodeDevice(_In_ CAudioLightNodeDevice *lpNodeDevice, _In_ BYTE byLightingSlot) noexcept;

    LPCSTR GetType() const noexcept final
    {
        return "Rgb5050Lighting";
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

    HRESULT ValidateNodeLightingConfiguration(_In_ const CIotCommClient::LightingOutput_t &sLighting,
                                               _Out_writes_z_(nReasonLen) LPWSTR szReasonW,
                                               _In_ SIZE_T nReasonLen) noexcept final;
    VOID OnNodeConnectionStateChanged(_In_ BOOL bConnected, _In_ HRESULT hRes) noexcept final;
    BOOL SupportsManualColorActions() const noexcept final
    {
        return TRUE;
    }
    HRESULT OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept final;

private:
    static constexpr USHORT CMD_SET_COLOR = 1;
    static constexpr USHORT CMD_SET_AND_FADE_TO_COLOR = 3;

#pragma pack(1)
    typedef struct SetColorCommand_s {
        USHORT uRed;
        USHORT uGreen;
        USHORT uBlue;
    } SetColorCommand_t;

    typedef struct SetAndFadeToColorCommand_s {
        USHORT uRedStart;
        USHORT uGreenStart;
        USHORT uBlueStart;
        USHORT uRedEnd;
        USHORT uGreenEnd;
        USHORT uBlueEnd;
        USHORT uFadeMs;
    } SetAndFadeToColorCommand_t;
#pragma pack()

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
    CAudioLightNodeDevice *m_lpNodeDevice{ nullptr };
    BYTE m_byLightingSlot{ 0 };
    CNodeLightingSubscription m_cNodeSubscription;

    CRwMutex m_cCurrentStateRwMutex;
    CurrentState_t m_sCurrentState{};
};
