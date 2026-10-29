#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <roapi.h>

// -----------------------------------------------------------------------------

class WindowsRuntimeScope
{
public:
    WindowsRuntimeScope() noexcept = default;
    WindowsRuntimeScope(_In_ WindowsRuntimeScope const&) = delete;
    WindowsRuntimeScope(_Inout_ WindowsRuntimeScope &&) = delete;
    ~WindowsRuntimeScope() noexcept
    {
        if (m_bIsInitialized)
        {
            RoUninitialize();
        }
    }

    WindowsRuntimeScope &operator=(_In_ WindowsRuntimeScope const &) = delete;
    WindowsRuntimeScope &operator=(_Inout_ WindowsRuntimeScope &&) = delete;

    _Must_inspect_result_ _Success_(return == S_OK)
    HRESULT Initialize() noexcept
    {
        HRESULT hr;

        if (m_bIsInitialized)
        {
            return E_UNEXPECTED;
        }

        hr = RoInitialize(RO_INIT_MULTITHREADED);
        if (FAILED(hr))
        {
            return hr;
        }

        m_bIsInitialized = TRUE;
        return S_OK;
    }

private:
    BOOL m_bIsInitialized{ FALSE };
};
