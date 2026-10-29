#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "..\Processors\BaseAudioProcessor.h"
#include "TUI\TUiWindow.h"
#include "Utils\PersistantSettings.h"
#include "Utils\Mutex.h"
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
    CBaseIotDevice(_In_ const CAudioProcessors &cAudioProcessors) noexcept;
    CBaseIotDevice(_In_ const CBaseIotDevice &) = delete;
    CBaseIotDevice(_Inout_ CBaseIotDevice &&) = delete;

public:
    virtual ~CBaseIotDevice() noexcept;

    CBaseIotDevice &operator=(_In_ const CBaseIotDevice &) = delete;
    CBaseIotDevice &operator=(_Inout_ CBaseIotDevice &&) = delete;

    HRESULT SetupDeviceInfo(_In_z_ LPCWSTR szDeviceNameW) noexcept;

    ULONGLONG GetID() const noexcept final;

    virtual LPCSTR GetType() const noexcept = 0;

    LPCWSTR GetDeviceName() const noexcept
    {
        return m_szDeviceNameW;
    };

    virtual HRESULT InitComm() noexcept;
    virtual VOID DeinitComm() noexcept;

    virtual VOID FirstRender() noexcept = 0;

    virtual VOID Process() noexcept = 0;

    virtual VOID TimerTick() noexcept;

    virtual BOOL SupportsManualColorActions() const noexcept;
    virtual HRESULT OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept;

    HRESULT OnPostAttachEvent() noexcept override;

protected:
    // Leave three client rows for device content and status inside the frame border.
    static constexpr INT DEVICE_STATUS_ROW = 3;
    static constexpr INT DEVICE_FRAME_HEIGHT = DEVICE_STATUS_ROW + 2;

    VOID SetDeviceStatus(_In_z_ LPCWSTR szReasonW, _In_ INT iColor) noexcept;

protected:
    const CAudioProcessors &m_cAudioProcessors;

    struct {
        CMutex cMutex;
        WCHAR szTextW[128]{ L"Disconnected" };
        SIZE_T nTextLen{ 12 };
        INT iTextColor{ 0 };
        SIZE_T nTextOffset{ SIZE_MAX };
        INT iLastVisibleStatusChars{ INT_MAX };
        ULONGLONG ullLastRefreshMs{ 0 };
    } m_sDeviceStatus = {};

    LPWSTR m_szDeviceNameW{ nullptr };

private:
    VOID RenderDeviceStatus() noexcept;
};

typedef std::vector<std::unique_ptr<CBaseIotDevice>> CIotDeviceList;
