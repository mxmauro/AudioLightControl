#pragma once

#include "BaseIotDevice.h"
#include "Utils\PersistantSettings.h"
#include <vector>

// -----------------------------------------------------------------------------

class CSharedManualColorControlWindow : public CTUiWindow, public CPersistantSettings
{
public:
    typedef struct DeviceEntry_s {
        CBaseIotDevice *lpDevice{ nullptr };
        BOOL bChecked{ TRUE };
    } DeviceEntry_t;

public:
    explicit CSharedManualColorControlWindow(_In_ const std::vector<CBaseIotDevice *> &vecDevices) noexcept;
    CSharedManualColorControlWindow(_In_ const CSharedManualColorControlWindow &) = delete;
    CSharedManualColorControlWindow(_Inout_ CSharedManualColorControlWindow &&) = delete;
    ~CSharedManualColorControlWindow() noexcept = default;

    CSharedManualColorControlWindow &operator=(_In_ const CSharedManualColorControlWindow &) = delete;
    CSharedManualColorControlWindow &operator=(_Inout_ CSharedManualColorControlWindow &&) = delete;

    ULONGLONG GetID() const noexcept final;

    VOID FirstRender() noexcept;

    INT GetDefaultWidth() const noexcept final;
    INT GetDefaultHeight() const noexcept final;

    HRESULT OnPostAttachEvent() noexcept final;
    VOID OnMouseEvent(_In_ CTUiWindowManager::MouseEvent_t &sEvent) noexcept final;

private:
    VOID RenderPalette() noexcept;
    VOID RenderDeviceHeader() noexcept;
    VOID RenderDeviceLine(_In_ INT iDeviceIndex) noexcept;
    VOID BroadcastAction(_In_ const ManualColorAction_t &sAction) noexcept;

private:
    std::vector<DeviceEntry_t> m_vecDevices;
};
