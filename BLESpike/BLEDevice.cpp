#include "BLEDevice.h"
#include "BLEDeviceImpl.h"
#include "BLEDeviceInternals.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;

// -----------------------------------------------------------------------------

typedef struct AdvertisementKey_s {
    BYTE m_byAddress[6];
    BLEDevice::AddressType_t m_addressType;

    BOOL operator<(_In_ AdvertisementKey_s const& other) const noexcept
    {
        INT nResult = std::memcmp(m_byAddress, other.m_byAddress, sizeof(m_byAddress));

        if (nResult != 0)
        {
            return nResult < 0;
        }

        return static_cast<UINT>(m_addressType) < static_cast<UINT>(other.m_addressType);
    }
} AdvertisementKey_t;

typedef struct AdvertisementData_s {
    BYTE m_byAddress[6];
    BLEDevice::AddressType_t m_addressType;
    std::wstring m_strName;
    INT16 m_nRssi;
} AdvertisementData_t;

typedef struct ScanState_s {
    explicit ScanState_s(_In_ HANDLE hFailureEvent) noexcept : m_hFailureEvent(hFailureEvent)
    {
    }

    std::mutex m_mutex;
    BOOL m_bAccepting{ TRUE };
    std::map<AdvertisementKey_t, AdvertisementData_t> m_mapAdvertisements;
    HRESULT m_hrFailure{ S_OK };
    HANDLE m_hFailureEvent{ nullptr };
} ScanState_t;

// -----------------------------------------------------------------------------

static BLEDevice::AddressType_t ToBleAddressType(_In_ BluetoothAddressType addressType) noexcept;
static VOID ToBleAddress(_In_ UINT64 ullAddress, _Out_writes_(6) BYTE byAddress[6]) noexcept;

static void RecordCallbackFailure(_In_ std::shared_ptr<ScanState_t> const& spState, _In_ HRESULT hr) noexcept;

static void SaveAdvertisement(_In_ std::shared_ptr<ScanState_t> const& spState,
                              _In_ BluetoothLEAdvertisementReceivedEventArgs const& arguments) noexcept;
static HRESULT ScanForAdvertisements(_Out_ std::vector<AdvertisementData_t>& vecAdvertisements,
                                     _In_ BLEOperationContext const& context) noexcept;

// -----------------------------------------------------------------------------

BLEDevice::BLEDevice(_In_ std::wstring&& strDeviceName, _In_reads_(6) BYTE const byAddressDevice[6], _In_ AddressType_t addressTypeDevice,
                     _In_ INT16 nDeviceRssi) noexcept
    : m_strName(std::move(strDeviceName)), m_addressType(addressTypeDevice), m_nRssi(nDeviceRssi)
{
    CopyMemory(m_byAddress, byAddressDevice, sizeof(m_byAddress));
}

BLEDevice::~BLEDevice() noexcept = default;

BLEDevice* BLEDevice::New(_In_z_ LPCWSTR szDeviceNameW, _In_reads_(6) BYTE const byAddressDevice[6], _In_ AddressType_t addressTypeDevice,
                          _In_ INT16 nDeviceRssi) noexcept
{
    BLEDevice* lpBleDevice;

    try
    {
        std::wstring strDeviceName;

        if (!byAddressDevice)
        {
            return nullptr;
        }
        strDeviceName = szDeviceNameW;
        lpBleDevice = new BLEDevice(std::move(strDeviceName), byAddressDevice, addressTypeDevice, nDeviceRssi);
    }
    catch (std::bad_alloc const&)
    {
        return nullptr;
    }
    catch (...)
    {
        return nullptr;
    }
    return lpBleDevice;
}

_Must_inspect_result_ _Success_(return == S_OK)
HRESULT BLEDevice::EnumerateBleDevices(_Out_ std::vector<std::shared_ptr<BLEDevice>>& vecDevices,
                                       _In_ BLEOperationContext const& context) noexcept
{
    std::vector<AdvertisementData_t> vecAdvertisements;
    HRESULT hr;

    vecDevices.clear();

    try
    {
        // Scan for advertisements before creating stable device instances.
        hr = ScanForAdvertisements(vecAdvertisements, context);
        if (FAILED(hr))
        {
            return hr;
        }

        // Populate device list.
        vecDevices.reserve(vecAdvertisements.size());
        for (AdvertisementData_t& advertisement : vecAdvertisements)
        {
            vecDevices.push_back(
                std::shared_ptr<BLEDevice>(
                    new BLEDevice(std::move(advertisement.m_strName), advertisement.m_byAddress, advertisement.m_addressType,
                                  advertisement.m_nRssi)
                )
            );
        }
        return S_OK;
    }
    catch (winrt::hresult_error const& error)
    {
        vecDevices.clear();
        return error.code();
    }
    catch (std::bad_alloc const&)
    {
        vecDevices.clear();
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        vecDevices.clear();
        return E_FAIL;
    }
}

_Must_inspect_result_ _Success_(return == S_OK)
HRESULT BLEDevice::EnumerateServices(_Out_ std::vector<Service_t> &vecServices,
                                     _In_ BLEOperationContext const &context) noexcept
{
    std::shared_ptr<Operation> spOperation;
    BluetoothLEDevice bluetoothDevice{ nullptr };
    GattDeviceServicesResult servicesResult{ nullptr };
    HRESULT hr;

    vecServices.clear();

    // Begin transaction
    hr = BeginOperation(spOperation, context);
    if (FAILED(hr))
    {
        return hr;
    }

    try
    {
        // Retrieve all services and their available characteristics.
        hr = spOperation->m_spImpl->GetDevice(spOperation->m_ullConnectionGeneration, &bluetoothDevice);
        if (FAILED(hr))
        {
            return hr;
        }
        hr = BLEDeviceInternals::WaitForOperation(bluetoothDevice.GetGattServicesAsync(BluetoothCacheMode::Uncached), &servicesResult,
                                                  context);
        if (FAILED(hr))
        {
            return hr;
        }
        if (servicesResult.Status() != GattCommunicationStatus::Success)
        {
            hr = BLEDeviceInternals::GetGattStatusError(servicesResult.Status());
            if (BLEDeviceInternals::IsConnectionFailure(hr))
            {
                spOperation->InvalidateConnection();
            }
            return hr;
        }

        // Get services
        for (GattDeviceService const &gattService : servicesResult.Services())
        {
            Service_t service{};
            GattCharacteristicsResult characteristicsResult{ nullptr };

            // Query service characteristics.
            service.m_uuid = gattService.Uuid();
            service.m_uAttributeHandle = gattService.AttributeHandle();
            hr = BLEDeviceInternals::WaitForOperation(gattService.GetCharacteristicsAsync(BluetoothCacheMode::Uncached),
                                                      &characteristicsResult, context);
            if (FAILED(hr))
            {
                return hr;
            }
            if (characteristicsResult.Status() != GattCommunicationStatus::Success)
            {
                hr = BLEDeviceInternals::GetGattStatusError(characteristicsResult.Status());
                if (BLEDeviceInternals::IsConnectionFailure(hr))
                {
                    spOperation->InvalidateConnection();
                }

                return hr;
            }

            // Populate characteristics
            for (GattCharacteristic const &characteristic : characteristicsResult.Characteristics())
            {
                Characteristic_t item{};

                item.m_uuid = characteristic.Uuid();
                item.m_uAttributeHandle = characteristic.AttributeHandle();
                item.m_ulProperties = static_cast<UINT32>(characteristic.CharacteristicProperties());
                service.m_vecCharacteristics.push_back(std::move(item));
            }

            // Add service to list
            vecServices.push_back(std::move(service));
        }
        return S_OK;
    }
    catch (winrt::hresult_error const &error)
    {
        vecServices.clear();
        return error.code();
    }
    catch (std::bad_alloc const &)
    {
        vecServices.clear();
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        vecServices.clear();
        return E_FAIL;
    }
}

_Success_(return == S_OK)
HRESULT BLEDevice::Read(_In_ GUID const &serviceUuid, _In_ GUID const &characteristicUuid, _Out_ std::vector<BYTE> &vecValue,
                        _In_ BLEOperationContext const &context) noexcept
{
    std::shared_ptr<Operation> spOperation;
    HRESULT hr;

    // Acquire a serialized GATT operation for this single read.
    hr = BeginOperation(spOperation, context);
    if (FAILED(hr))
    {
        return hr;
    }

    return spOperation->Read(serviceUuid, characteristicUuid, vecValue);
}

_Success_(return == S_OK)
HRESULT BLEDevice::Write(_In_ GUID const &serviceUuid, _In_ GUID const &characteristicUuid,
                         _In_reads_bytes_opt_(nDataLen) LPCVOID lpData, _In_ SIZE_T nDataLen, _In_ BOOL bWaitForAck,
                         _In_ BLEOperationContext const &context) noexcept
{
    std::shared_ptr<Operation> spOperation;
    HRESULT hr;

    // Acquire a serialized GATT operation for this single write.
    hr = BeginOperation(spOperation, context);
    if (FAILED(hr))
    {
        return hr;
    }

    return spOperation->Write(serviceUuid, characteristicUuid, lpData, nDataLen, bWaitForAck);
}

_Must_inspect_result_ _Success_(return == S_OK)
HRESULT BLEDevice::BeginOperation(_Out_ std::shared_ptr<Operation> &spOperation, _In_ BLEOperationContext const &context) noexcept
{
    std::shared_ptr<Impl> spImpl;
    HANDLE hGate;
    DWORD dwSignaledIndex;
    UINT64 ullGeneration;
    HRESULT hr;

    spOperation.reset();

    // Create the shared implementation and wait for exclusive GATT access.
    hr = GetOrCreateImpl(spImpl);
    if (FAILED(hr))
    {
        return hr;
    }
    hGate = spImpl->Gate();
    hr = context.Wait(&hGate, 1, &dwSignaledIndex);
    if (FAILED(hr))
    {
        if (dwSignaledIndex == 0)
        {
            ReleaseSemaphore(spImpl->Gate(), 1, nullptr);
        }
        return hr;
    }

    // Bind this operation to the connection generation it acquired.
    hr = spImpl->EnsureDevice(context, &ullGeneration);
    if (FAILED(hr))
    {
        ReleaseSemaphore(spImpl->Gate(), 1, nullptr);
        return hr;
    }
    try
    {
        spOperation = std::shared_ptr<Operation>(new Operation(spImpl, context, ullGeneration));
        return S_OK;
    }
    catch (std::bad_alloc const &)
    {
        ReleaseSemaphore(spImpl->Gate(), 1, nullptr);
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        ReleaseSemaphore(spImpl->Gate(), 1, nullptr);
        return E_FAIL;
    }
}

HRESULT BLEDevice::GetOrCreateImpl(_Out_ std::shared_ptr<Impl> &spImpl) noexcept
{
    try
    {
        std::lock_guard lock(m_mutex);

        if (!m_spImpl)
        {
            m_spImpl = std::make_shared<Impl>(m_byAddress, m_addressType);
        }
        spImpl = m_spImpl;
        return S_OK;
    }
    catch (winrt::hresult_error const &error)
    {
        return error.code();
    }
    catch (std::bad_alloc const &)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

// -----------------------------------------------------------------------------

static BLEDevice::AddressType_t ToBleAddressType(_In_ BluetoothAddressType addressType) noexcept
{
    switch (addressType)
    {
        case BluetoothAddressType::Public:
            return BLEDevice::AddressType_t::Public;

        case BluetoothAddressType::Random:
            return BLEDevice::AddressType_t::Random;

        case BluetoothAddressType::Unspecified:
        default:
            return BLEDevice::AddressType_t::Unspecified;
    }
}

static VOID ToBleAddress(_In_ UINT64 ullAddress, _Out_writes_(6) BYTE byAddress[6]) noexcept
{
    byAddress[0] = static_cast<BYTE>((ullAddress >> 40) & 0xFF);
    byAddress[1] = static_cast<BYTE>((ullAddress >> 32) & 0xFF);
    byAddress[2] = static_cast<BYTE>((ullAddress >> 24) & 0xFF);
    byAddress[3] = static_cast<BYTE>((ullAddress >> 16) & 0xFF);
    byAddress[4] = static_cast<BYTE>((ullAddress >>  8) & 0xFF);
    byAddress[5] = static_cast<BYTE>( ullAddress        & 0xFF);
}

static void RecordCallbackFailure(_In_ std::shared_ptr<ScanState_t> const& spState, _In_ HRESULT hr) noexcept
{
    try
    {
        std::lock_guard lock(spState->m_mutex);
        if (SUCCEEDED(spState->m_hrFailure))
        {
            if (FAILED(hr))
            {
                spState->m_hrFailure = hr;
            }
            else
            {
                spState->m_hrFailure = E_FAIL;
            }
        }
    }
    catch (...)
    {
    }
    SetEvent(spState->m_hFailureEvent);
}

static void SaveAdvertisement(_In_ std::shared_ptr<ScanState_t> const& spState,
                              _In_ BluetoothLEAdvertisementReceivedEventArgs const& arguments) noexcept
{
    try
    {
        AdvertisementKey_t key{};
        AdvertisementData_t advertisement{};
        std::lock_guard lock(spState->m_mutex);

        ToBleAddress(arguments.BluetoothAddress(), key.m_byAddress);
        key.m_addressType = ToBleAddressType(arguments.BluetoothAddressType());

        if (!spState->m_bAccepting)
        {
            return;
        }

        auto it = spState->m_mapAdvertisements.find(key);
        if (it == spState->m_mapAdvertisements.end())
        {
            CopyMemory(advertisement.m_byAddress, key.m_byAddress, sizeof(advertisement.m_byAddress));
            advertisement.m_addressType = key.m_addressType;
            advertisement.m_nRssi = arguments.RawSignalStrengthInDBm();
            it = spState->m_mapAdvertisements.emplace(key, std::move(advertisement)).first;
        }

        it->second.m_nRssi = arguments.RawSignalStrengthInDBm();
        winrt::hstring name = arguments.Advertisement().LocalName();
        if (!name.empty())
        {
            it->second.m_strName.assign(name.c_str(), name.size());
        }
    }
    catch (winrt::hresult_error const& error)
    {
        RecordCallbackFailure(spState, error.code());
    }
    catch (...)
    {
        RecordCallbackFailure(spState, E_FAIL);
    }
}

static HRESULT ScanForAdvertisements(_Out_ std::vector<AdvertisementData_t>& vecAdvertisements,
                                     _In_ BLEOperationContext const& context) noexcept
{
    HANDLE hFailureEvent;
    winrt::handle failureEvent;
    HRESULT hr;

    vecAdvertisements.clear();

    hFailureEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!hFailureEvent)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    failureEvent.attach(hFailureEvent);

    try
    {
        std::shared_ptr<ScanState_t> spState;
        BluetoothLEAdvertisementWatcher watcher;
        winrt::event_token receivedToken;
        DWORD dwSignaledIndex;

        // Prepare shared state for advertisement callbacks.
        spState = std::make_shared<ScanState_t>(failureEvent.get());

        // Start scanning for nearby BLE advertisements.
        watcher.ScanningMode(BluetoothLEScanningMode::Active);
        receivedToken = watcher.Received([spState](auto const&, auto const& arguments)
        {
            SaveAdvertisement(spState, arguments);
        });
        watcher.Start();

        // Wait until the scan times out, is cancelled, or a callback fails.
        hFailureEvent = failureEvent.get();
        hr = context.Wait(&hFailureEvent, 1, &dwSignaledIndex);

        // Stop scanning and unregister the advertisement callback.
        watcher.Stop();
        watcher.Received(receivedToken);

        // Lock the scan state, reject late callbacks, and evaluate the scan result.
        {
            std::lock_guard lock(spState->m_mutex);

            spState->m_bAccepting = FALSE;
            if (SUCCEEDED(hr) && dwSignaledIndex == 0 && FAILED(spState->m_hrFailure))
            {
                return spState->m_hrFailure;
            }
            if (SUCCEEDED(hr))
            {
                return E_FAIL;
            }
            if (hr != HRESULT_FROM_WIN32(ERROR_TIMEOUT))
            {
                return hr;
            }

            // Populate the caller's vector from the advertisements collected during the scan.
            vecAdvertisements.clear();
            vecAdvertisements.reserve(spState->m_mapAdvertisements.size());
            for (auto const& [key, value] : spState->m_mapAdvertisements)
            {
                vecAdvertisements.push_back(value);
            }
        }
        return S_OK;
    }
    catch (winrt::hresult_error const& error)
    {
        vecAdvertisements.clear();
        return error.code();
    }
    catch (std::bad_alloc const&)
    {
        vecAdvertisements.clear();
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        vecAdvertisements.clear();
        return E_FAIL;
    }
}

