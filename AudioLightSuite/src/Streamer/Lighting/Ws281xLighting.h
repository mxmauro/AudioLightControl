#pragma once

#include "..\Devices\AudioLightNodeDevice.h"
#include "BaseLighting.h"
#include "..\AppConfig.h"
#include <vector>

// -----------------------------------------------------------------------------

class CWs281xLighting : public CBaseLighting, public IAudioLightNodeLighting
{
public:
    CWs281xLighting(_In_ const CAudioProcessors &cAudioProcessors) noexcept;
    CWs281xLighting(_In_ const CWs281xLighting &) = delete;
    CWs281xLighting(_Inout_ CWs281xLighting &&) = delete;
    ~CWs281xLighting() noexcept override = default;

    CWs281xLighting &operator=(_In_ const CWs281xLighting &) = delete;
    CWs281xLighting &operator=(_Inout_ CWs281xLighting &&) = delete;

    HRESULT SetupEffects(_In_ const std::vector<CConfig::DeviceEffect> &vecEffects) noexcept;
    HRESULT SetupStripLength(_In_ USHORT nStripLength) noexcept;
    HRESULT SetupNodeDevice(_In_ CAudioLightNodeDevice *lpNodeDevice, _In_ BYTE byLightingSlot) noexcept;

    LPCSTR GetType() const noexcept final
    {
        return "Ws281xLighting";
    }

    VOID FirstRender() noexcept final;
    VOID Process() noexcept final;
    VOID TimerTick() noexcept final;
    BOOL SupportsManualColorActions() const noexcept final
    {
        return TRUE;
    }
    HRESULT OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept final;

    INT GetDefaultWidth() const noexcept final
    {
        return 36;
    }

    INT GetDefaultHeight() const noexcept final
    {
        return DEVICE_FRAME_HEIGHT;
    }

    HRESULT ValidateNodeLightingConfiguration(_In_ const CIotCommClient::LightingOutput_t &sLighting,
                                               _Out_writes_z_(nReasonLen) LPWSTR szReasonW,
                                               _In_ SIZE_T nReasonLen) noexcept final;
    VOID OnNodeConnectionStateChanged(_In_ BOOL bConnected, _In_ HRESULT hRes) noexcept final;

private:
    typedef enum class OutputMode_e {
        None = 0,
        StripRgb,
        Rgb,
        White,
        UV
    } OutputMode;

#pragma pack(1)
    typedef struct SetColorCommand_s {
        USHORT uRed;
        USHORT uGreen;
        USHORT uBlue;
    } SetColorCommand_t;
#pragma pack()

    BOOL GetColor(_Out_ CBaseAudioProcessor::RGB &sColor) noexcept;
    VOID SendUniformColor(_In_ const CBaseAudioProcessor::RGB &sColor) noexcept;
    VOID RenderStripPreview() noexcept;

private:
    static constexpr USHORT CMD_SET_COLOR = 1;

    CAudioLightNodeDevice *m_lpNodeDevice{ nullptr };
    BYTE m_byLightingSlot{ 0 };
    CNodeLightingSubscription m_cNodeSubscription;
    USHORT m_nStripLength{ 0 };
    OutputMode m_eOutputMode{ OutputMode::None };
    CBaseAudioProcessor *m_lpAudioProcessor{ nullptr };
    std::vector<CBaseAudioProcessor::RGB> m_vecStripColors;
    std::vector<CBaseAudioProcessor::RGB> m_vecPreviewColors;
    std::vector<BYTE> m_vecRgbBytes;

    CMutex m_cManualColorMutex;
    struct {
        BOOL bFixedIsActive{ FALSE };
        BYTE uRed{ 0 };
        BYTE uGreen{ 0 };
        BYTE uBlue{ 0 };
        BYTE uBrightness{ 255 };
    } m_sManualColor{};
};
