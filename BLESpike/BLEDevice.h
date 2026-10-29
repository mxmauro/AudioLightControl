#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "BLEOperationContext.h"

// -----------------------------------------------------------------------------

class BLEDevice
{
private:
    class Impl;
    class NotificationSubscriptionState;

public:
    // Identifies the BLE address kind used to connect to the device.
    typedef enum class AddressType_e {
        Unspecified,
        Public,
        Random
    } AddressType_t;

    // Describes a characteristic discovered on a BLE GATT service.
    typedef struct Characteristic_s {
        GUID m_uuid;
        UINT16 m_uAttributeHandle;
        UINT32 m_ulProperties;
    } Characteristic_t;

    // Describes a BLE GATT service and its discovered characteristics.
    typedef struct Service_s {
        GUID m_uuid;
        UINT16 m_uAttributeHandle;
        std::vector<Characteristic_t> m_vecCharacteristics;
    } Service_t;

    // Receives the byte payload of an asynchronous characteristic notification.
    typedef std::function<void(std::vector<BYTE> const& vecValue)> NotificationCallback_t;

public:
    class NotificationSubscription
    {
    private:
        friend class BLEDevice;

        explicit NotificationSubscription(_In_ std::shared_ptr<NotificationSubscriptionState> spState) noexcept;

    public:
        // Notification subscriptions are owned through shared pointers and cannot be copied or moved.
        NotificationSubscription(_In_ NotificationSubscription const&) = delete;
        NotificationSubscription(_Inout_ NotificationSubscription&&) = delete;

        // Releases this subscription; the final owner unregisters the notification callback.
        ~NotificationSubscription() noexcept;

        // Assignment is disabled because a subscription's registration is immutable.
        NotificationSubscription& operator=(_In_ NotificationSubscription const&) = delete;
        NotificationSubscription& operator=(_Inout_ NotificationSubscription&&) = delete;

        // Returns the connection generation on which this subscription was created.
        UINT64 GetConnectionGeneration() const noexcept;

    private:
        std::shared_ptr<NotificationSubscriptionState> m_spState;
    };

public:
    class Operation
    {
    private:
        friend class BLEDevice;

        Operation(_In_ std::shared_ptr<Impl> spImpl, _In_ BLEOperationContext const& context,
                  _In_ UINT64 ullConnectionGeneration) noexcept;

    public:
        // Operations are owned through shared pointers and cannot be copied or moved.
        Operation(_In_ Operation const&) = delete;
        Operation(_Inout_ Operation&&) = delete;

        // Releases the exclusive GATT-operation gate held by this operation.
        ~Operation() noexcept;

        // Assignment is disabled because an operation owns exclusive GATT access.
        Operation& operator=(_In_ Operation const&) = delete;
        Operation& operator=(_Inout_ Operation&&) = delete;

        _Success_(return == S_OK)
        // Reads the current uncached value of the requested characteristic.
        HRESULT Read(_In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid, _Out_ std::vector<BYTE>& vecValue) noexcept;

        _Success_(return == S_OK)
        // Writes bytes to the requested characteristic and optionally waits for the ATT acknowledgement.
        HRESULT Write(_In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid, _In_reads_bytes_opt_(nDataLen) LPCVOID lpData,
                      _In_ SIZE_T nDataLen, _In_ BOOL bWaitForAck = TRUE) noexcept;

        _Must_inspect_result_ _Success_(return == S_OK)
        // Registers a notification callback and returns an object that keeps it registered.
        // By default this also enables the CCCD; callers can retain an existing device-side notification setup.
        HRESULT Subscribe(_In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid,  _In_ NotificationCallback_t const& callback,
                          _Out_ std::shared_ptr<NotificationSubscription>& spSubscription,
                          _In_ BOOL bEnableNotifications = TRUE) noexcept;

        // Returns the connection generation reserved for this operation.
        UINT64 GetConnectionGeneration() const noexcept;

    private:
        HRESULT GetCharacteristic(_In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid, _Out_ void* pCharacteristic) noexcept;
        void InvalidateConnection() noexcept;

    private:
        std::shared_ptr<Impl> m_spImpl;
        BLEOperationContext m_context;
        UINT64 m_ullConnectionGeneration;
        BOOL m_bOwnsGate{ TRUE };
    };

private:
    BLEDevice(_In_ std::wstring&& strDeviceName, _In_reads_(6) BYTE const byAddressDevice[6], _In_ AddressType_t addressTypeDevice,
              _In_ INT16 nDeviceRssi) noexcept;
public:
    // BLEDevice instances cannot be copied or moved.
    BLEDevice(_In_ BLEDevice const&) = delete;
    BLEDevice(_Inout_ BLEDevice&&) = delete;

    // Releases the device and its shared connection state.
    ~BLEDevice() noexcept;

    // Creates a device descriptor from an advertised BLE device.
    static BLEDevice* New(_In_z_ LPCWSTR szDeviceNameW, _In_reads_(6) BYTE const byAddressDevice[6], _In_ AddressType_t addressTypeDevice,
                          _In_ INT16 nDeviceRssi) noexcept;

    // Assignment is disabled because a device descriptor's identity is immutable.
    BLEDevice& operator=(_In_ BLEDevice const&) = delete;
    BLEDevice& operator=(_Inout_ BLEDevice&&) = delete;

    _Must_inspect_result_ _Success_(return == S_OK)
    // Scans for nearby BLE advertisers and returns one descriptor per device address.
    static HRESULT EnumerateBleDevices(_Out_ std::vector<std::shared_ptr<BLEDevice>>& vecDevices,
                                       _In_ BLEOperationContext const& context = BLEOperationContext()) noexcept;

    _Must_inspect_result_ _Success_(return == S_OK)
    // Enumerates the device's GATT services and their characteristics.
    HRESULT EnumerateServices(_Out_ std::vector<Service_t>& vecServices,
                              _In_ BLEOperationContext const& context = BLEOperationContext()) noexcept;

    _Success_(return == S_OK)
    // Reads the requested characteristic in a serialized GATT operation.
    HRESULT Read(_In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid, _Out_ std::vector<BYTE>& vecValue,
                 _In_ BLEOperationContext const& context = BLEOperationContext()) noexcept;

    _Success_(return == S_OK)
    // Writes bytes to the requested characteristic and optionally waits for the ATT acknowledgement.
    HRESULT Write(_In_ GUID const& serviceUuid, _In_ GUID const& characteristicUuid, _In_reads_bytes_opt_(nDataLen) LPCVOID lpData,
                  _In_ SIZE_T nDataLen, _In_ BOOL bWaitForAck = TRUE,
                  _In_ BLEOperationContext const& context = BLEOperationContext()) noexcept;

    _Must_inspect_result_ _Success_(return == S_OK)
    // Starts an exclusive GATT operation that can read, write, or subscribe atomically.
    HRESULT BeginOperation(_Out_ std::shared_ptr<Operation>& spOperation,
                           _In_ BLEOperationContext const& context = BLEOperationContext()) noexcept;

    // Returns the advertised local name of the device.
    LPCWSTR Name() const noexcept
    {
        return m_strName.c_str();
    }

    // Copies the advertised 48-bit BLE address in conventional MAC-address byte order.
    VOID Address(_Out_writes_(6) BYTE byAddress[6]) const noexcept
    {
        CopyMemory(byAddress, m_byAddress, sizeof(m_byAddress));
    }

    // Returns whether the device address is public, random, or unspecified.
    AddressType_t TypeOfAddress() const noexcept
    {
        return m_addressType;
    }

    // Returns the most recently observed advertisement signal strength in dBm.
    INT16 Rssi() const noexcept
    {
        return m_nRssi;
    }

private:
    HRESULT GetOrCreateImpl(_Out_ std::shared_ptr<Impl>& spImpl) noexcept;

private:
    std::mutex m_mutex;
    std::shared_ptr<Impl> m_spImpl;
    std::wstring m_strName;
    BYTE m_byAddress[6];
    AddressType_t m_addressType;
    INT16 m_nRssi;
};
