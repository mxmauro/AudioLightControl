#include "BLEDeviceImpl.h"
#include "BLEDeviceInternals.h"
#include "BLEDeviceNotification.h"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Security.Cryptography.h>

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Security::Cryptography;
using namespace winrt::Windows::Storage::Streams;

// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------

BLEDevice::Operation::Operation(_In_ std::shared_ptr<Impl> spImpl, _In_ BLEOperationContext const& context,
                                _In_ UINT64 ullConnectionGeneration) noexcept
    : m_spImpl(std::move(spImpl)), m_context(context), m_ullConnectionGeneration(ullConnectionGeneration)
{
}

BLEDevice::Operation::~Operation() noexcept
{
    if (m_bOwnsGate && m_spImpl && m_spImpl->Gate())
    {
        ReleaseSemaphore(m_spImpl->Gate(), 1, nullptr);
    }
}

UINT64 BLEDevice::Operation::GetConnectionGeneration() const noexcept
{
    return m_ullConnectionGeneration;
}

void BLEDevice::Operation::InvalidateConnection() noexcept
{
    if (m_spImpl)
    {
        m_spImpl->InvalidateDevice(m_ullConnectionGeneration);
    }
}

HRESULT BLEDevice::Operation::GetCharacteristic(_In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid,
                                                _Out_ void* pCharacteristic) noexcept
{
    BluetoothLEDevice bluetoothDevice{ nullptr };
    GattDeviceServicesResult servicesResult{ nullptr };
    GattCharacteristicsResult characteristicsResult{ nullptr };
    GattCharacteristic* pGattCharacteristic = static_cast<GattCharacteristic*>(pCharacteristic);
    HRESULT hr;

    if (!pGattCharacteristic)
    {
        return E_POINTER;
    }
    *pGattCharacteristic = nullptr;

    if (m_spImpl->TryGetCharacteristic(m_ullConnectionGeneration, serviceUuid, characteristicUuid, pGattCharacteristic))
    {
        return S_OK;
    }

    hr = m_spImpl->GetDevice(m_ullConnectionGeneration, &bluetoothDevice);
    if (FAILED(hr))
    {
        return hr;
    }

    // Resolve the requested service on the current connection.
    hr = BLEDeviceInternals::WaitForOperation(
        bluetoothDevice.GetGattServicesForUuidAsync(winrt::guid{ serviceUuid }, BluetoothCacheMode::Uncached), &servicesResult, m_context);
    if (FAILED(hr))
    {
        return hr;
    }
    if (servicesResult.Status() != GattCommunicationStatus::Success)
    {
        hr = BLEDeviceInternals::GetGattStatusError(servicesResult.Status());
        if (BLEDeviceInternals::IsConnectionFailure(hr))
        {
            InvalidateConnection();
        }
        return hr;
    }
    if (servicesResult.Services().Size() == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    // Resolve the requested characteristic within that service.
    hr = BLEDeviceInternals::WaitForOperation(
        servicesResult.Services().GetAt(0).GetCharacteristicsForUuidAsync(winrt::guid{ characteristicUuid }, BluetoothCacheMode::Uncached),
        &characteristicsResult, m_context);
    if (FAILED(hr))
    {
        return hr;
    }
    if (characteristicsResult.Status() != GattCommunicationStatus::Success)
    {
        hr = BLEDeviceInternals::GetGattStatusError(characteristicsResult.Status());
        if (BLEDeviceInternals::IsConnectionFailure(hr))
        {
            InvalidateConnection();
        }
        return hr;
    }
    if (characteristicsResult.Characteristics().Size() == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    *pGattCharacteristic = characteristicsResult.Characteristics().GetAt(0);
    m_spImpl->CacheCharacteristic(m_ullConnectionGeneration, serviceUuid, characteristicUuid, *pGattCharacteristic);
    return S_OK;
}

_Success_(return == S_OK)
HRESULT BLEDevice::Operation::Read(_In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid,
                                   _Out_ std::vector<BYTE>& vecValue) noexcept
{
    GattCharacteristic characteristic{ nullptr };
    GattReadResult result{ nullptr };
    HRESULT hr;

    vecValue.clear();

    try
    {
        hr = GetCharacteristic(serviceUuid, characteristicUuid, &characteristic);
        if (FAILED(hr))
        {
            return hr;
        }
        hr = BLEDeviceInternals::WaitForOperation(characteristic.ReadValueAsync(BluetoothCacheMode::Uncached), &result, m_context);
        if (FAILED(hr))
        {
            return hr;
        }
        if (result.Status() != GattCommunicationStatus::Success)
        {
            hr = BLEDeviceInternals::GetGattStatusError(result.Status());
            if (BLEDeviceInternals::IsConnectionFailure(hr))
            {
                InvalidateConnection();
            }
            return hr;
        }

        // Copy the uncached characteristic value into the caller's buffer.
        DataReader reader = DataReader::FromBuffer(result.Value());
        vecValue.resize(reader.UnconsumedBufferLength());
        if (!vecValue.empty())
        {
            reader.ReadBytes(vecValue);
        }
        return S_OK;
    }
    catch (winrt::hresult_error const& error)
    {
        return error.code();
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

_Success_(return == S_OK)
HRESULT BLEDevice::Operation::Write(_In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid,
                                    _In_reads_bytes_opt_(nDataLen) LPCVOID lpData, _In_ SIZE_T nDataLen, _In_ BOOL bWaitForAck) noexcept
{
    GattCharacteristic characteristic{ nullptr };
    GattWriteResult result{ nullptr };
    GattWriteOption writeOption;
    HRESULT hr;

    if ((!lpData && nDataLen != 0) || nDataLen > MAXUINT32)
    {
        return E_INVALIDARG;
    }

    try
    {
        hr = GetCharacteristic(serviceUuid, characteristicUuid, &characteristic);
        if (FAILED(hr))
        {
            return hr;
        }

        // Create the owned WinRT buffer directly from the caller's bytes.
        winrt::array_view<BYTE const> bytes{ static_cast<BYTE const*>(lpData), static_cast<UINT32>(nDataLen) };
        writeOption = bWaitForAck ? GattWriteOption::WriteWithResponse : GattWriteOption::WriteWithoutResponse;

        hr = BLEDeviceInternals::WaitForOperation(
            characteristic.WriteValueWithResultAsync(CryptographicBuffer::CreateFromByteArray(bytes), writeOption), &result, m_context);
        if (FAILED(hr))
        {
            return hr;
        }
        if (result.Status() != GattCommunicationStatus::Success)
        {
            hr = BLEDeviceInternals::GetGattStatusError(result.Status());
            if (BLEDeviceInternals::IsConnectionFailure(hr))
            {
                InvalidateConnection();
            }
            return hr;
        }
        return S_OK;
    }
    catch (winrt::hresult_error const& error)
    {
        return error.code();
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

_Must_inspect_result_ _Success_(return == S_OK)
HRESULT BLEDevice::Operation::Subscribe(_In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid,
                                        _In_ NotificationCallback_t const& callback,
                                        _Out_ std::shared_ptr<NotificationSubscription>& spSubscription,
                                        _In_ BOOL bEnableNotifications) noexcept
{
    GattCharacteristic characteristic{ nullptr };
    IAsyncOperation<GattCommunicationStatus> configureOperation{ nullptr };
    GattCommunicationStatus status;
    winrt::event_token token;
    HRESULT hr;

    spSubscription.reset();
    if (!callback)
    {
        return E_INVALIDARG;
    }

    try
    {
        hr = GetCharacteristic(serviceUuid, characteristicUuid, &characteristic);
        if (FAILED(hr))
        {
            return hr;
        }

        // Register before enabling the CCCD so no early notification is lost.
        token = characteristic.ValueChanged([callback](GattCharacteristic const&, GattValueChangedEventArgs const& arguments) noexcept
        {
            try
            {
                DataReader reader = DataReader::FromBuffer(arguments.CharacteristicValue());
                std::vector<BYTE> vecValue(reader.UnconsumedBufferLength());
                if (!vecValue.empty())
                {
                    reader.ReadBytes(vecValue);
                }
                callback(vecValue);
            }
            catch (...)
            {
            }
        });
        if (bEnableNotifications)
        {
            configureOperation = characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify);
            hr = BLEDeviceInternals::WaitForOperation(configureOperation, &status, m_context);
            if (FAILED(hr) || status != GattCommunicationStatus::Success)
            {
                characteristic.ValueChanged(token);
                if (SUCCEEDED(hr))
                {
                    hr = BLEDeviceInternals::GetGattStatusError(status);
                    if (BLEDeviceInternals::IsConnectionFailure(hr))
                    {
                        InvalidateConnection();
                    }
                }
                return hr;
            }
        }
        spSubscription = std::shared_ptr<NotificationSubscription>(
            new NotificationSubscription(
                std::make_shared<NotificationSubscriptionState>(characteristic, token, m_ullConnectionGeneration)
            )
        );
        return S_OK;
    }
    catch (winrt::hresult_error const& error)
    {
        return error.code();
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

