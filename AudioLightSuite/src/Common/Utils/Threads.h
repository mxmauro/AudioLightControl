#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <functional>
#include <thread>
#include <system_error>

// -----------------------------------------------------------------------------

class CThread
{
public:
    using ThreadRoutine = std::function<void(_In_ CThread &thread)>;

    CThread() noexcept = default;
    CThread(_In_ const CThread &) = delete;
    CThread(_Inout_ CThread &&) = delete;
    ~CThread() noexcept
    {
        Stop();
    };

    CThread &operator=(_In_ const CThread &) = delete;
    CThread &operator=(_Inout_ CThread &&) = delete;

    HRESULT Start(_In_ ThreadRoutine cThreadRoutine) noexcept
    {
        HRESULT hRes;

        if (!cThreadRoutine)
        {
            return E_INVALIDARG;
        }
        Stop();

        m_hThreadExitEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_hThreadExitEvent)
        {
            hRes = HRESULT_FROM_WIN32(::GetLastError());
            Stop();
            return hRes;
        }

        try
        {
            cThreadRoutine = std::move(cThreadRoutine);
            m_thread = std::thread([this, cThreadRoutine]()
            {
                cThreadRoutine(*this);
                ::SetEvent(m_hThreadExitEvent);
            });
        }
        catch (const std::system_error &ex)
        {
            Stop();
            return (ex.code().value() == std::_RESOURCE_UNAVAILABLE_TRY_AGAIN) ? E_OUTOFMEMORY : E_FAIL;
        }
        catch (const std::bad_alloc &)
        {
            Stop();
            return E_OUTOFMEMORY;
        }

        // Done
        return S_OK;
    };

    VOID Stop() noexcept
    {
        if (m_thread.joinable())
        {
            ::SetEvent(m_hThreadExitEvent);
            m_thread.join();
        }
        if (m_hThreadExitEvent)
        {
            ::CloseHandle(m_hThreadExitEvent);
            m_hThreadExitEvent = nullptr;
        }
    };

    DWORD GetThreadId() const noexcept
    {
        return (m_thread.joinable()) ? ::GetThreadId(m_thread.native_handle()) : 0;
    };

    HANDLE GetThreadHandle() const noexcept
    {
        return (m_thread.joinable()) ? m_thread.native_handle() : nullptr;
    };

    HANDLE GetExitEvent() const noexcept
    {
        return m_hThreadExitEvent;
    };

    HRESULT CheckForAbort(_In_ DWORD dwTimeoutMs, _In_opt_ INT iHandleCount = 0, ...)
    {
        HANDLE h[64];
        va_list argptr;
        DWORD dw;

        if (iHandleCount < 0 || iHandleCount > 63)
        {
            return E_INVALIDARG;
        }
        h[0] = m_hThreadExitEvent;
        va_start(argptr, iHandleCount);
        for (INT i = 0; i < iHandleCount; i++)
        {
            h[i + 1] = va_arg(argptr, HANDLE);
        }
        va_end(argptr);
        dw = ::WaitForMultipleObjects((DWORD)iHandleCount + 1, h, FALSE, dwTimeoutMs);
        if (dw == WAIT_TIMEOUT)
        {
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        if (dw >= WAIT_OBJECT_0 && dw <= WAIT_OBJECT_0 + (DWORD)iHandleCount)
        {
            return (HRESULT)(dw - WAIT_OBJECT_0);
        }
        return E_FAIL;
    };

private:
    mutable std::thread m_thread;
    HANDLE m_hThreadExitEvent{ nullptr };
};
