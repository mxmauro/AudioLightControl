#pragma once

#include "BLEOperationContext.h"
#include <memory>
#include <winrt/base.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Foundation.h>

// -----------------------------------------------------------------------------

namespace BLEDeviceInternals {

HRESULT GetGattStatusError(_In_ winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCommunicationStatus status) noexcept;

BOOL IsConnectionFailure(_In_ HRESULT hr) noexcept;

template <typename TResult>
HRESULT WaitForOperation(_In_ winrt::Windows::Foundation::IAsyncOperation<TResult> const& operation, _Out_ TResult* pResult,
                         _In_ BLEOperationContext const& context) noexcept
{
    std::shared_ptr<winrt::handle> spCompletionEvent;
    DWORD dwSignaledIndex;
    HANDLE hEvent;
    HRESULT hr;

    if (!pResult)
    {
        return E_POINTER;
    }
    try
    {
        spCompletionEvent = std::make_shared<winrt::handle>();
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
    hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!hEvent)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    spCompletionEvent->attach(hEvent);

    try
    {
        operation.Completed([spCompletionEvent](auto const&, winrt::Windows::Foundation::AsyncStatus) noexcept
        {
            SetEvent(spCompletionEvent->get());
        });

        hr = context.Wait(&hEvent, 1, &dwSignaledIndex);
        if (SUCCEEDED(hr))
        {
            *pResult = operation.GetResults();
            return S_OK;
        }

        operation.Cancel();
        return hr;
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

} // namespace BLEDeviceInternals
