#include "BLEDevice.h"
#include "ConsoleCancellationScope.h"
#include "WindowsRuntimeScope.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

// -----------------------------------------------------------------------------

static std::wstring FormatBluetoothAddress(_In_reads_(6) BYTE const byAddress[6]);
static wchar_t const* GetAddressTypeName(_In_ BLEDevice::AddressType_t addressType) noexcept;
static void PrintDevice(_In_ BLEDevice const& device);
static void PrintServices(_In_ std::vector<BLEDevice::Service_t> const& vecServices);
static void PrintGuid(_In_ GUID const& guid);
static BOOL HasCharacteristic(_In_ BLEDevice::Service_t const& service, _In_ GUID const& uuid) noexcept;
static BOOL IsGoveeDevice(_In_ std::vector<BLEDevice::Service_t> const& vecServices) noexcept;
static void PrintHresult(_In_z_ wchar_t const* pwszOperation, _In_ HRESULT hr);

// -----------------------------------------------------------------------------

_Success_(return == 0)
int wmain()
{
    WindowsRuntimeScope runtimeScope;
    ConsoleCancellationScope cancellationScope;
    std::vector<std::shared_ptr<BLEDevice>> vecDevices;
    std::vector<BLEDevice::Service_t> vecServices;
    HRESULT hr;

    hr = runtimeScope.Initialize();
    if (FAILED(hr))
    {
        PrintHresult(L"Windows Runtime initialization", hr);
        return 1;
    }

    hr = cancellationScope.Initialize();
    if (FAILED(hr))
    {
        PrintHresult(L"console cancellation initialization", hr);
        return 1;
    }

    // -----------------------------------------------------------------------------

    hr = BLEDevice::EnumerateBleDevices(vecDevices,
                                        BLEOperationContext().WithCancellationEvent(cancellationScope.GetEvent()).WithDeadline(10'000));
    if (FAILED(hr))
    {
        PrintHresult(L"BLE device enumeration", hr);
        return 1;
    }

    for (std::shared_ptr<BLEDevice> const& spDevice : vecDevices)
    {
        PrintDevice(*spDevice);
    }

    std::wcout << L"Found " << vecDevices.size() << L" BLE device(s).\n";

    auto itGovee = std::find_if(vecDevices.begin(), vecDevices.end(), [](std::shared_ptr<BLEDevice> const& spDevice)
    {
        return wcsstr(spDevice->Name(), L"Govee") != nullptr;
    });
    if (itGovee == vecDevices.end())
    {
        std::wcout << L"No Govee BLE device found.\n";
        return 0;
    }

    hr = (*itGovee)->EnumerateServices(vecServices,
                                       BLEOperationContext().WithCancellationEvent(cancellationScope.GetEvent()).WithDeadline(10'000));
    if (FAILED(hr))
    {
        PrintHresult(L"GATT service enumeration", hr);
        return 1;
    }

    std::wcout << L"GATT services for " << (*itGovee)->Name() << L":\n";
    PrintServices(vecServices);
    std::wcout << L"Govee service signature: " << (IsGoveeDevice(vecServices) ? L"present" : L"not found") << L"\n";
    return 0;
}

// -----------------------------------------------------------------------------

static std::wstring FormatBluetoothAddress(_In_reads_(6) BYTE const byAddress[6])
{
    WCHAR wszText[18]{};

    swprintf_s(wszText, _countof(wszText), L"%02X:%02X:%02X:%02X:%02X:%02X",
               static_cast<UINT>(byAddress[0]), static_cast<UINT>(byAddress[1]), static_cast<UINT>(byAddress[2]),
               static_cast<UINT>(byAddress[3]), static_cast<UINT>(byAddress[4]), static_cast<UINT>(byAddress[5]));
    return std::wstring(wszText);
}

static wchar_t const* GetAddressTypeName(_In_ BLEDevice::AddressType_t addressType) noexcept
{
    switch (addressType)
    {
        case BLEDevice::AddressType_t::Public:
            return L"public";

        case BLEDevice::AddressType_t::Random:
            return L"random";

        default:
            return L"unspecified";
    }
}

static void PrintDevice(_In_ BLEDevice const& device)
{
    BYTE byAddress[6];

    device.Address(byAddress);
    std::wcout << L"Device: " << device.Name() << L"\n"
               << L"  Address: " << FormatBluetoothAddress(byAddress) << L" ("
               << GetAddressTypeName(device.TypeOfAddress()) << L")\n"
               << L"  RSSI: " << device.Rssi() << L" dBm\n";
}

static void PrintServices(_In_ std::vector<BLEDevice::Service_t> const& vecServices)
{
    for (BLEDevice::Service_t const& service : vecServices)
    {
        std::wcout << L"  Service UUID: ";
        PrintGuid(service.m_uuid);
        std::wcout << L"\n"
                   << L"    Handle: " << service.m_uAttributeHandle << L"\n";

        for (BLEDevice::Characteristic_t const& characteristic : service.m_vecCharacteristics)
        {
            std::wcout << L"    Characteristic UUID: ";
            PrintGuid(characteristic.m_uuid);
            std::wcout << L"\n"
                       << L"      Handle: " << characteristic.m_uAttributeHandle << L"\n"
                       << L"      Properties: 0x" << std::hex << characteristic.m_ulProperties << std::dec << L"\n";
        }
    }
}

static void PrintGuid(_In_ GUID const& guid)
{
    std::wcout << L'{' << std::hex << std::setfill(L'0') << std::setw(8) << guid.Data1 << L'-' << std::setw(4) << guid.Data2
               << L'-' << std::setw(4) << guid.Data3 << L'-' << std::setw(2) << static_cast<UINT>(guid.Data4[0])
               << std::setw(2) << static_cast<UINT>(guid.Data4[1]) << L'-' << std::setw(2) << static_cast<UINT>(guid.Data4[2])
               << std::setw(2) << static_cast<UINT>(guid.Data4[3]) << std::setw(2) << static_cast<UINT>(guid.Data4[4])
               << std::setw(2) << static_cast<UINT>(guid.Data4[5]) << std::setw(2) << static_cast<UINT>(guid.Data4[6])
               << std::setw(2) << static_cast<UINT>(guid.Data4[7]) << L'}' << std::dec << std::setfill(L' ');
}

static BOOL HasCharacteristic(_In_ BLEDevice::Service_t const& service, _In_ GUID const& uuid) noexcept
{
    return std::any_of(service.m_vecCharacteristics.begin(), service.m_vecCharacteristics.end(),
                       [&uuid](BLEDevice::Characteristic_t const& characteristic)
    {
        return InlineIsEqualGUID(characteristic.m_uuid, uuid);
    }) ? TRUE : FALSE;
}

static BOOL IsGoveeDevice(_In_ std::vector<BLEDevice::Service_t> const& vecServices) noexcept
{
    GUID const GoveeServiceUuid{ 0x00010203, 0x0405, 0x0607, { 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x19, 0x10 } };
    GUID const GoveeWriteCharacteristicUuid{ 0x00010203, 0x0405, 0x0607, { 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x2b, 0x10 } };
    GUID const GoveeNotifyCharacteristicUuid{ 0x00010203, 0x0405, 0x0607, { 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x2b, 0x11 } };
    GUID const GoveeService2Uuid{ 0x02f00000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00 } };
    GUID const GoveeService2CharacteristicUuids[]{
        { 0x02f00000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03 } },
        { 0x02f00000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x02 } },
        { 0x02f00000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00 } },
        { 0x02f00000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x01 } },
    };

    BOOL bHasFirstGoveeService = FALSE;
    BOOL bHasSecondGoveeService = FALSE;
    for (BLEDevice::Service_t const& service : vecServices)
    {
        if (InlineIsEqualGUID(service.m_uuid, GoveeServiceUuid))
        {
            bHasFirstGoveeService = HasCharacteristic(service, GoveeWriteCharacteristicUuid) &&
                                    HasCharacteristic(service, GoveeNotifyCharacteristicUuid);
        }
        else if (InlineIsEqualGUID(service.m_uuid, GoveeService2Uuid))
        {
            bHasSecondGoveeService = TRUE;
            for (GUID const& uuid : GoveeService2CharacteristicUuids)
            {
                if (!HasCharacteristic(service, uuid))
                {
                    bHasSecondGoveeService = FALSE;
                    break;
                }
            }
        }
    }

    return bHasFirstGoveeService && bHasSecondGoveeService;
}

static void PrintHresult(_In_z_ wchar_t const* pwszOperation, _In_ HRESULT hr)
{
    std::wcerr << pwszOperation << L" failed: 0x" << std::hex << static_cast<UINT>(hr) << std::dec << L"\n";
}
