#include "BLEDeviceNotification.h"

using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;

// -----------------------------------------------------------------------------

BLEDevice::NotificationSubscriptionState::NotificationSubscriptionState(_In_ GattCharacteristic const& characteristic,
                                                                        _In_ winrt::event_token token, _In_ UINT64 ullGeneration) noexcept
    : m_characteristic(characteristic), m_token(token), m_ullGeneration(ullGeneration)
{
}

BLEDevice::NotificationSubscriptionState::~NotificationSubscriptionState() noexcept
{
    try
    {
        m_characteristic.ValueChanged(m_token);
    }
    catch (...)
    {
    }
}

UINT64 BLEDevice::NotificationSubscriptionState::Generation() const noexcept
{
    return m_ullGeneration;
}

// -----------------------------------------------------------------------------

BLEDevice::NotificationSubscription::NotificationSubscription(_In_ std::shared_ptr<NotificationSubscriptionState> spState) noexcept
    : m_spState(std::move(spState))
{
}

BLEDevice::NotificationSubscription::~NotificationSubscription() noexcept = default;

UINT64 BLEDevice::NotificationSubscription::GetConnectionGeneration() const noexcept
{
    if (!m_spState)
    {
        return 0;
    }
    return m_spState->Generation();
}
