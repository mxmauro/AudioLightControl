#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <combaseapi.h>

#pragma comment(lib, "Ole32.lib")

// -----------------------------------------------------------------------------

class CComStart
{
public:
    CComStart() noexcept
    {
        m_hRes = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }

    CComStart(_In_ const CComStart &) = delete;
    CComStart(_Inout_ CComStart &&) = delete;

    ~CComStart() noexcept
    {
        if (SUCCEEDED(m_hRes))
        {
            ::CoUninitialize();
        }
    }

    CComStart &operator=(_In_ const CComStart &) = delete;
    CComStart &operator=(_Inout_ CComStart &&) = delete;

    HRESULT HResult() const noexcept
    {
        return m_hRes;
    }

    BOOL Succeeded() const noexcept
    {
        return (SUCCEEDED(m_hRes)) ? TRUE : FALSE;
    }

private:
    HRESULT m_hRes{ E_FAIL };
};
