#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

template <class F>
HRESULT TranslateToHresult(F &&f) noexcept
{
    try
    {
        std::forward<F>(f)();
        return S_OK;
    }
    catch (HRESULT hr)
    {
        return hr;
    }
    catch (const std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_UNEXPECTED;
    }
}
