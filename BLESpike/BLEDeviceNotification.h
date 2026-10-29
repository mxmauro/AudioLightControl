#pragma once

#include "BLEDevice.h"
#include <shared_mutex>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>

using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;

// -----------------------------------------------------------------------------

class BLEDevice::NotificationSubscriptionState
{
public:
    NotificationSubscriptionState(_In_ GattCharacteristic const &characteristic, _In_ winrt::event_token token,
                                  _In_ UINT64 ullGeneration) noexcept;
    NotificationSubscriptionState(_In_ NotificationSubscriptionState const &) noexcept = default;
    NotificationSubscriptionState(_Inout_ NotificationSubscriptionState &&) noexcept = default;
    ~NotificationSubscriptionState() noexcept;
        
    NotificationSubscriptionState &operator=(_In_ NotificationSubscriptionState const &) noexcept = default;
    NotificationSubscriptionState &operator=(_Inout_ NotificationSubscriptionState &&) noexcept = default;

    UINT64 Generation() const noexcept;

private:
    GattCharacteristic m_characteristic;
    winrt::event_token m_token;
    UINT64 m_ullGeneration;
};
