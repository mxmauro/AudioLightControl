#include "BLEDeviceInternals.h"

// -----------------------------------------------------------------------------

namespace BLEDeviceInternals {

HRESULT GetGattStatusError(_In_ winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCommunicationStatus status) noexcept
{
    using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCommunicationStatus;

    switch (status)
    {
        case GattCommunicationStatus::AccessDenied:
            return E_ACCESSDENIED;

        case GattCommunicationStatus::Unreachable:
            return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);

        case GattCommunicationStatus::Success:
        case GattCommunicationStatus::ProtocolError:
        default:
            return E_FAIL;
    }
}

BOOL IsConnectionFailure(_In_ HRESULT hr) noexcept
{
    return hr == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
}

} // namespace BLEDeviceInternals
