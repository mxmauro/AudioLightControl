#pragma once

#include "Threads.h"

// -----------------------------------------------------------------------------

class CTimedEvent : private CThread
{
public:
    CTimedEvent() noexcept = default;
    CTimedEvent(_In_ const CTimedEvent &) = delete;
    CTimedEvent(_Inout_ CTimedEvent &&) = delete;

    ~CTimedEvent() noexcept
    {
        Stop();
    };

    CTimedEvent &operator=(_In_ const CTimedEvent &) = delete;
    CTimedEvent &operator=(_Inout_ CTimedEvent &&) = delete;

    VOID Reset() noexcept
    {
        Stop();
    };

    HRESULT Set(_In_ DWORD _dwTimeoutMs, _In_ std::function<VOID()> _cCallback)
    {
        DWORD dwTimeoutMs;
        std::function<VOID()> cCallback;

        if (!_cCallback)
        {
            return E_INVALIDARG;
        }
        Stop();

        dwTimeoutMs = _dwTimeoutMs;
        cCallback = std::move(_cCallback);

        return Start([this, dwTimeoutMs, cCallback]() -> DWORD
        {
            if (::WaitForSingleObject(GetExitEvent(), dwTimeoutMs) == WAIT_TIMEOUT)
            {
                cCallback();
            }
            return 0;
        });

        // Done
        return S_OK;
    };
};
