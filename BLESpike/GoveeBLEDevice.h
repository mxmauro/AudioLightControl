#pragma once

#include "BLEDevice.h"
#include <memory>
#include <mutex>

// -----------------------------------------------------------------------------

class GoveeBLEDevice
{
public:
    typedef enum class EncryptionType_e {
        None,
        AesRc4Psk,
        AesGcm,
    } EncryptionType_t;

    typedef struct Endpoint_s {
        GUID m_serviceUuid;
        GUID m_writeCharacteristicUuid;
        GUID m_notificationCharacteristicUuid;
    } Endpoint_t;

public:
    // Creates a Govee protocol wrapper that uses the standard service and characteristic UUIDs.
    GoveeBLEDevice(_In_ std::shared_ptr<BLEDevice> spDevice, _In_ EncryptionType_t encryptionType) noexcept;
    // Creates a Govee protocol wrapper for a caller-supplied BLE endpoint.
    GoveeBLEDevice(_In_ std::shared_ptr<BLEDevice> spDevice, _In_ EncryptionType_t encryptionType,
                   _In_ Endpoint_t const& endpoint) noexcept;
    GoveeBLEDevice(_In_ GoveeBLEDevice const&) = delete;
    GoveeBLEDevice(_Inout_ GoveeBLEDevice&&) = delete;
    ~GoveeBLEDevice() noexcept;

    GoveeBLEDevice& operator=(_In_ GoveeBLEDevice const&) = delete;
    GoveeBLEDevice& operator=(_Inout_ GoveeBLEDevice&&) = delete;

    _Success_(return == S_OK)
    // Sends one plain or encrypted Govee frame over an exclusive BLE operation.
    HRESULT SendCommand(_In_reads_bytes_opt_(nDataLen) LPCVOID lpData, _In_ SIZE_T nDataLen,
                        _In_ BOOL bWaitForAck = TRUE,
                        _In_ BLEOperationContext const& context = BLEOperationContext()) noexcept;

    _Must_inspect_result_
    // Returns the standard Govee BLE service, write, and notification endpoint.
    static Endpoint_t GetDefaultEndpoint() noexcept;

private:
    class Impl;

    _Success_(return == S_OK)
    HRESULT GetOrCreateImpl(_Out_ std::shared_ptr<Impl>& spImpl) noexcept;

private:
    std::mutex m_mutex;
    std::shared_ptr<BLEDevice> m_spDevice;
    EncryptionType_t m_encryptionType;
    Endpoint_t m_endpoint;
    std::shared_ptr<Impl> m_spImpl;
};
