#pragma once

#include "BaseIotDevice.h"
#include "IotComm\IotCommClient.h"
#include "Utils\Threads.h"
#include "Utils\WindowsEvent.h"

// -----------------------------------------------------------------------------

class CAudioLightNodeIotDevice : public CBaseIotDevice
{
protected:
    CAudioLightNodeIotDevice(_In_ const CAudioProcessors &cAudioProcessors) noexcept;
    CAudioLightNodeIotDevice(_In_ const CAudioLightNodeIotDevice &) = delete;
    CAudioLightNodeIotDevice(_Inout_ CAudioLightNodeIotDevice &&) = delete;

public:
    ~CAudioLightNodeIotDevice() noexcept override;

    CAudioLightNodeIotDevice &operator=(_In_ const CAudioLightNodeIotDevice &) = delete;
    CAudioLightNodeIotDevice &operator=(_Inout_ CAudioLightNodeIotDevice &&) = delete;

    HRESULT SetupCommInfo(_In_z_ LPCWSTR szHostNameW, _In_z_ LPCSTR szUserNameA,
                          _In_ const BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES]) noexcept;

    HRESULT InitComm() noexcept final;
    VOID DeinitComm() noexcept final;

protected:
    static constexpr USHORT CMD_SET_COLOR = 1;
    static constexpr USHORT CMD_FADE_TO_COLOR = 2;
    static constexpr USHORT CMD_SET_AND_FADE_TO_COLOR = 3;
    static constexpr DWORD CMD_FLAG_SET_COLOR = 0x00000001;
    static constexpr DWORD CMD_FLAG_SET_AND_FADE_TO_COLOR = 0x00000004;
    static constexpr DWORD CMD_FLAG_GET_DEVICE_CONFIG = 0x00000010;
    static constexpr DWORD CMD_FLAG_SET_RAW_DMX_CHANNEL_DATA = 0x00000020;

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

    HRESULT SendIotMessage(_In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload, _In_ SIZE_T nPayloadLen,
                           _In_ BOOL bReliable) noexcept;

    BOOL IsIotConnected() const noexcept;

    virtual VOID OnIotConnectionStateChanged(_In_ BOOL bConnected) noexcept;
    virtual HRESULT ValidateDeviceConfiguration(_In_ const CIotCommClient::CDeviceConfigCommandResult &cConfig,
                                                _Out_writes_z_(nReasonLen) LPWSTR szReasonW, _In_ SIZE_T nReasonLen) noexcept = 0;

protected:
    mutable LONG volatile m_lRundownProt{ 0 };
    mutable LONG volatile m_iConnected{ 0 };

    struct {
        CThread cThread;
        CIotCommClient cClient;
        CWindowsEvent cClientClosedEvent;
        LPWSTR szHostNameW{ nullptr };
        LPSTR szUserNameA{ nullptr };
        BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES]{};
    } m_sIot{};

private:
    VOID IotThreadProc() noexcept;
    VOID OnIotMessage(_In_ USHORT nCmd, _In_ const BYTE *lpData, _In_ SIZE_T nDataLen) noexcept;
    VOID OnIotClose(_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason) noexcept;
};
