#include "BLEDevice.h"
#include "BLEDeviceImpl.h"
#include "BLEDeviceInternals.h"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>

using namespace winrt::Windows::Foundation;

// -----------------------------------------------------------------------------

static BluetoothAddressType ToBluetoothAddressType(_In_ BLEDevice::AddressType_t addressType) noexcept;
static UINT64 ToBluetoothAddress(_In_reads_(6) BYTE const byAddress[6]) noexcept;

static UINT64 Fnv1a64(_In_ LPCVOID pData, _In_ SIZE_T nDataSize, _In_opt_ UINT64 ullHash = 14695981039346656037ull) noexcept;

// -----------------------------------------------------------------------------

BLEDevice::Impl::Impl(_In_reads_(6) BYTE const byAddress[6], _In_ AddressType_t addressType) : m_addressType(addressType)
{
    CopyMemory(m_byAddress, byAddress, sizeof(m_byAddress));
    m_hGate = CreateSemaphoreW(nullptr, 1, 1, nullptr);
    if (!m_hGate)
    {
        throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));
    }
}

BLEDevice::Impl::~Impl() noexcept
{
    if (m_hGate)
    {
        CloseHandle(m_hGate);
    }
}

HRESULT BLEDevice::Impl::EnsureDevice(_In_ BLEOperationContext const& context, _Out_ UINT64* pullGeneration) noexcept
{
    BluetoothLEDevice bluetoothDevice{ nullptr };
    HRESULT hr;

    if (!pullGeneration)
    {
        return E_POINTER;
    }

    // Reuse the current device connection when it is still valid.
    {
        std::shared_lock lock(m_mutex);

        if (m_bluetoothDevice)
        {
            *pullGeneration = m_ullGeneration;
            return S_OK;
        }
    }

    // Establish a new device connection for this generation.
    hr = BLEDeviceInternals::WaitForOperation(BluetoothLEDevice::FromBluetoothAddressAsync(ToBluetoothAddress(m_byAddress),
                                                                                            ToBluetoothAddressType(m_addressType)),
                                              &bluetoothDevice, context);
    if (FAILED(hr))
    {
        return hr;
    }
    if (!bluetoothDevice)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    {
        std::unique_lock lock(m_mutex);

        if (!m_bluetoothDevice)
        {
            m_bluetoothDevice = std::move(bluetoothDevice);
            m_mapCharacteristics.clear();
            ++m_ullGeneration;
        }

        *pullGeneration = m_ullGeneration;
    }
    return S_OK;
}

HRESULT BLEDevice::Impl::GetDevice(_In_ UINT64 ullGeneration, _Out_ BluetoothLEDevice* pBluetoothDevice) noexcept
{
    std::shared_lock lock(m_mutex);

    if (!pBluetoothDevice)
    {
        return E_POINTER;
    }
    if ((!m_bluetoothDevice) || m_ullGeneration != ullGeneration)
    {
        return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    }

    *pBluetoothDevice = m_bluetoothDevice;
    return S_OK;
}

void BLEDevice::Impl::InvalidateDevice(_In_ UINT64 ullGeneration) noexcept
{
    std::unique_lock lock(m_mutex);

    if (m_ullGeneration == ullGeneration)
    {
        m_bluetoothDevice = nullptr;
        m_mapCharacteristics.clear();
        ++m_ullGeneration;
    }
}

BOOL BLEDevice::Impl::TryGetCharacteristic(_In_ UINT64 ullGeneration, _In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid,
                                           _Out_ GattCharacteristic* pCharacteristic) noexcept
{
    if (!pCharacteristic)
    {
        return FALSE;
    }

    std::shared_lock lock(m_mutex);
    CharacteristicKey_t key{ serviceUuid, characteristicUuid };
    auto it = m_mapCharacteristics.find(key);

    if (!m_bluetoothDevice || m_ullGeneration != ullGeneration || it == m_mapCharacteristics.end())
    {
        return FALSE;
    }

    *pCharacteristic = it->second;
    return TRUE;
}

void BLEDevice::Impl::CacheCharacteristic(_In_ UINT64 ullGeneration, _In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid,
                                          _In_ GattCharacteristic const& characteristic) noexcept
{
    try
    {
        std::unique_lock lock(m_mutex);

        if (m_bluetoothDevice && m_ullGeneration == ullGeneration)
        {
            m_mapCharacteristics.insert_or_assign(CharacteristicKey_t{ serviceUuid, characteristicUuid }, characteristic);
        }
    }
    catch (...)
    {
        // Cache insertion is optional; the caller still owns a valid characteristic reference.
    }
}

HANDLE BLEDevice::Impl::Gate() const noexcept
{
    return m_hGate;
}

SIZE_T BLEDevice::Impl::CharacteristicKeyHasher::operator()(_In_ CharacteristicKey_t const &key) const noexcept
{
    UINT64 ullHash = Fnv1a64(&key.m_serviceUuid, sizeof(key.m_serviceUuid));
    ullHash = Fnv1a64(&key.m_characteristicUuid, sizeof(key.m_characteristicUuid), ullHash);
    return static_cast<SIZE_T>(ullHash);
}

// -----------------------------------------------------------------------------

static BluetoothAddressType ToBluetoothAddressType(_In_ BLEDevice::AddressType_t addressType) noexcept
{
    switch (addressType)
    {
        case BLEDevice::AddressType_t::Public:
            return BluetoothAddressType::Public;

        case BLEDevice::AddressType_t::Random:
            return BluetoothAddressType::Random;

        case BLEDevice::AddressType_t::Unspecified:
        default:
            return BluetoothAddressType::Unspecified;
    }
}

static UINT64 ToBluetoothAddress(_In_reads_(6) BYTE const byAddress[6]) noexcept
{
    return (static_cast<UINT64>(byAddress[0]) << 40) | (static_cast<UINT64>(byAddress[1]) << 32) |
           (static_cast<UINT64>(byAddress[2]) << 24) | (static_cast<UINT64>(byAddress[3]) << 16) |
           (static_cast<UINT64>(byAddress[4]) << 8) | static_cast<UINT64>(byAddress[5]);
}

static UINT64 Fnv1a64(_In_ LPCVOID pData, _In_ SIZE_T nDataSize, _In_opt_ UINT64 ullHash) noexcept
{
    BYTE const *pBytes;

    pBytes = reinterpret_cast<BYTE const*>(pData);
    while (nDataSize > 0)
    {
        ullHash ^= *pBytes++;
        ullHash *= 1099511628211ull;
        nDataSize--;
    }
    return ullHash;
}

