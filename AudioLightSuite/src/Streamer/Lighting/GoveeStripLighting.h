#pragma once

#include "BaseLighting.h"
#include "..\Devices\GoveeStripDevice.h"
#include "..\AppConfig.h"
#include <string>
#include <vector>

// -----------------------------------------------------------------------------

class CGoveeStripLighting : public CBaseLighting
{
public:
    static constexpr USHORT GOVEE_MAX_STRIP_LENGTH = 255;

public:
    CGoveeStripLighting(_In_ const CAudioProcessors &cAudioProcessors) noexcept;
    CGoveeStripLighting(_In_ const CGoveeStripLighting &) = delete;
    CGoveeStripLighting(_Inout_ CGoveeStripLighting &&) = delete;
    ~CGoveeStripLighting() noexcept override = default;

    CGoveeStripLighting &operator=(_In_ const CGoveeStripLighting &) = delete;
    CGoveeStripLighting &operator=(_Inout_ CGoveeStripLighting &&) = delete;

    HRESULT SetupEffects(_In_ const std::vector<CConfig::DeviceEffect> &vecEffects) noexcept;
    HRESULT SetupGoveeStripDevice(_In_ CGoveeStripDevice *lpGoveeStripDevice) noexcept;
    HRESULT SetupSupportsRazer(_In_ BOOL bSupportsRazer) noexcept;
    HRESULT SetupStripLength(_In_ USHORT nStripLength) noexcept;

    LPCSTR GetType() const noexcept final
    {
        return "GoveeStripLighting";
    }

    VOID FirstRender() noexcept final;
    VOID Process() noexcept final;

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

private:
    typedef enum class OutputMode_e {
        None = 0,
        StripRgb,
        Rgb,
        White,
        UV
    } OutputMode;

private:
    static BYTE CalculateChecksum(_In_reads_bytes_(nDataLen) const BYTE *lpData, _In_ SIZE_T nDataLen) noexcept;
    static HRESULT CreateRazerJson(_In_ BYTE byCommand, _In_reads_bytes_(nDataLen) const BYTE *lpData, _In_ SIZE_T nDataLen,
                                   _Out_ std::string &strJsonPacket) noexcept;
    static HRESULT CreatePtRealJson(_In_reads_bytes_(nDataLen) const BYTE *lpData, _In_ SIZE_T nDataLen,
                                    _Out_ std::string &strJsonPacket) noexcept;

    BOOL SendTurnOnOff(_In_ BOOL bOn) noexcept;
    BOOL SendStreamingEnable(_In_ BOOL bEnable) noexcept;
    VOID SendRazerColors() noexcept;
    VOID SendColor(_In_ const CBaseAudioProcessor::RGB &sColor) noexcept;
    VOID SendUniformColor(_In_ const CBaseAudioProcessor::RGB &sColor) noexcept;
    BOOL SendPacket(_In_ LPCVOID lpData, _In_ SIZE_T nDataSize) noexcept;
    BOOL GetManualColor(_Out_ CBaseAudioProcessor::RGB &sColor) noexcept;

private:
    BOOL m_bSupportsRazer{ FALSE };
    USHORT m_nStripLength{ 0 };
    CGoveeStripDevice *m_lpGoveeStripDevice{ nullptr };
    ULONGLONG m_ullLastTurnOnMs{ 0 };
    ULONGLONG m_ullLastStreamEnableMs{ 0 };
    ULONGLONG m_ullLastColorUpdateMs{ 0 };

    OutputMode m_eOutputMode{ OutputMode::None };
    CBaseAudioProcessor *m_lpAudioProcessor{ nullptr };

    CMutex m_cManualColorMutex;
    struct {
        BOOL bFixedIsActive{ FALSE };
        BYTE uRed{ 0 };
        BYTE uGreen{ 0 };
        BYTE uBlue{ 0 };
        BYTE uBrightness{ 255 };
    } m_sManualColor = {};

    std::vector<CBaseAudioProcessor::RGB> m_vecStripColors;
};
