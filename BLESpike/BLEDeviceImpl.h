#pragma once

#include "BLEDevice.h"
#include <shared_mutex>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>

using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;

// -----------------------------------------------------------------------------

class BLEDevice::Impl
{
public:
    Impl(_In_reads_(6) BYTE const byAddress[6], _In_ AddressType_t addressType);
    Impl(_In_ Impl const &) noexcept = default;
    Impl(_Inout_ Impl &&) noexcept = default;
    ~Impl() noexcept;

    Impl &operator=(_In_ Impl const &) noexcept = default;
    Impl &operator=(_Inout_ Impl &&) noexcept = default;

    HRESULT EnsureDevice(_In_ BLEOperationContext const &context, _Out_ UINT64 *pullGeneration) noexcept;
    
    HRESULT GetDevice(_In_ UINT64 ullGeneration, _Out_ BluetoothLEDevice *pBluetoothDevice) noexcept;
    
    void InvalidateDevice(_In_ UINT64 ullGeneration) noexcept;

    BOOL TryGetCharacteristic(_In_ UINT64 ullGeneration, _In_ GUID const &serviceUuid, _In_ GUID const &characteristicUuid,
                              _Out_ GattCharacteristic *pCharacteristic) noexcept;
    
    void CacheCharacteristic(_In_ UINT64 ullGeneration, _In_ GUID const &serviceUuid, _In_ GUID const &characteristicUuid,
                             _In_ GattCharacteristic const &characteristic) noexcept;
    
    HANDLE Gate() const noexcept;

public:
    typedef struct CharacteristicKey_s {
        GUID m_serviceUuid;
        GUID m_characteristicUuid;

        BOOL operator==(_In_ CharacteristicKey_s const& other) const noexcept
        {
            return std::memcmp(&m_serviceUuid, &other.m_serviceUuid, sizeof(m_serviceUuid)) == 0 &&
                   std::memcmp(&m_characteristicUuid, &other.m_characteristicUuid, sizeof(m_characteristicUuid)) == 0;
        }
    } CharacteristicKey_t;

public:
    class CharacteristicKeyHasher
    {
    public:
        SIZE_T operator()(_In_ CharacteristicKey_t const& key) const noexcept;
    };

private:
    std::shared_mutex m_mutex;
    HANDLE m_hGate{ nullptr };
    BYTE m_byAddress[6];
    AddressType_t m_addressType;
    BluetoothLEDevice m_bluetoothDevice{ nullptr };
    std::unordered_map<CharacteristicKey_t, GattCharacteristic, CharacteristicKeyHasher> m_mapCharacteristics;
    UINT64 m_ullGeneration{ 0 };
};
