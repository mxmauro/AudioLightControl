#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "BaseIotDevice.h"
#include "..\AppConfig.h"
#include <string>
#include <vector>

// -----------------------------------------------------------------------------

class CGoveeStripIotDevice : public CBaseIotDevice
{
public:
    static constexpr USHORT GOVEE_MAX_STRIP_LENGTH = 255;

public:
    CGoveeStripIotDevice(_In_ const CAudioProcessors &cAudioProcessors) noexcept;
    CGoveeStripIotDevice(_In_ const CGoveeStripIotDevice &) = delete;
    CGoveeStripIotDevice(_Inout_ CGoveeStripIotDevice &&) = delete;
    ~CGoveeStripIotDevice() noexcept override;

    CGoveeStripIotDevice &operator=(_In_ const CGoveeStripIotDevice &) = delete;
    CGoveeStripIotDevice &operator=(_Inout_ CGoveeStripIotDevice &&) = delete;

    HRESULT SetupEffects(_In_ const std::vector<CConfig::DeviceEffect> &vecEffects) noexcept;
    HRESULT SetupCommInfo(_In_z_ LPCWSTR szHostAndPortW) noexcept;
    HRESULT SetupSupportsRazer(_In_ BOOL bSupportsRazer) noexcept;
    HRESULT SetupStripLength(_In_ USHORT nStripLength) noexcept;

    LPCSTR GetType() const noexcept final
    {
        return "GoveeStrip";
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
    VOID CloseSocket() noexcept;
    BOOL GetManualColor(_Out_ CBaseAudioProcessor::RGB &sColor) noexcept;

private:
    std::wstring m_strHostAndPort;
    BOOL m_bSupportsRazer{ FALSE };
    USHORT m_nStripLength{ 0 };
    SOCKET m_hSocket{ INVALID_SOCKET };
    SOCKADDR_STORAGE m_sDestination{};
    INT m_iDestinationLen{ 0 };
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
