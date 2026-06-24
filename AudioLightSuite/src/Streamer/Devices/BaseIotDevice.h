#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "..\Processors\BaseAudioProcessor.h"
#include "IotComm\IotCommClient.h"
#include "TUI\TUiWindow.h"
#include "Utils\PersistantSettings.h"
#include "Utils\Threads.h"
#include "Utils\WindowsEvent.h"
#include <memory>
#include <vector>

// -----------------------------------------------------------------------------

enum class ManualColorActionType_t
{
    SetFixedPaletteColor,
    IncreaseBrightness,
    DecreaseBrightness,
    ReturnToAutomaticMode,
};

typedef struct ManualColorAction_s {
    ManualColorActionType_t eType{ ManualColorActionType_t::SetFixedPaletteColor };
    BYTE uRed{ 0 };
    BYTE uGreen{ 0 };
    BYTE uBlue{ 0 };
} ManualColorAction_t;

// -----------------------------------------------------------------------------

class CBaseIotDevice : public CTUiWindow, public CPersistantSettings
{
protected:
    CBaseIotDevice() noexcept;
    CBaseIotDevice(_In_ const CBaseIotDevice &) = delete;
    CBaseIotDevice(_Inout_ CBaseIotDevice &&) = delete;
public:
    ~CBaseIotDevice() noexcept;

    CBaseIotDevice &operator=(_In_ const CBaseIotDevice &) = delete;
    CBaseIotDevice &operator=(_Inout_ CBaseIotDevice &&) = delete;

    HRESULT SetupDeviceInfo(_In_z_ LPCWSTR szDeviceNameW) noexcept;
    HRESULT SetupCommInfo(_In_z_ LPCWSTR szHostNameW, _In_z_ LPCSTR szUserNameA,
                          _In_ const BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES]) noexcept;

    virtual HRESULT SetupAudioProcessors(_In_ CAudioProcessorList &vecAudioProcessors) noexcept = 0;

    ULONGLONG GetID() const noexcept final;

    virtual LPCSTR GetType() const noexcept = 0;

    LPCWSTR GetDeviceName() const noexcept
    {
        return m_szDeviceNameW;
    };

    HRESULT InitComm() noexcept;
    VOID DeinitComm() noexcept;

    virtual VOID FirstRender() noexcept = 0;

    virtual VOID Process() noexcept = 0;

    virtual VOID TimerTick() noexcept;

    virtual BOOL SupportsManualColorActions() const noexcept;
    virtual HRESULT OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept;

    HRESULT OnPostAttachEvent() noexcept override;

protected:
    HRESULT SendIotMessage(_In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload, _In_ SIZE_T nPayloadLen,
                           _In_ BOOL bReliable) noexcept;

    BOOL IsIotConnected() const noexcept;

    virtual VOID OnIotConnectionStateChanged(_In_ BOOL bConnected) noexcept;

private:
    DWORD IotThreadProc() noexcept;

    VOID OnIotMessage(_In_ USHORT nCmd, _In_ const BYTE *lpData, _In_ SIZE_T nDataLen) noexcept;
    VOID OnIotClose(_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason) noexcept;

protected:
    mutable LONG volatile m_lRundownProt{ 0 };
    LPWSTR m_szDeviceNameW{ nullptr };
    struct {
        CThread cThread;
        // CWindowsEvent cThreadExitEvent;
        CIotCommClient cClient;
        CWindowsEvent cClientClosedEvent;
        LPWSTR szHostNameW{ nullptr };
        LPSTR szUserNameA{ nullptr };
        BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES]{};
    } m_sIot{};
};

typedef std::vector<std::unique_ptr<CBaseIotDevice>> CIotDeviceList;
