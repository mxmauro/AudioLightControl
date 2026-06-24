#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

class CWindowsEvent
{
public:
    CWindowsEvent() noexcept = default;
    CWindowsEvent(_In_ const CWindowsEvent &) = delete;
    CWindowsEvent(_Inout_ CWindowsEvent &&) = delete;
    ~CWindowsEvent() noexcept
    {
        Close();
    };

    CWindowsEvent &operator=(_In_ const CWindowsEvent &) = delete;
    CWindowsEvent &operator=(_Inout_ CWindowsEvent &&) = delete;

    HRESULT Create(_In_ BOOL bManualReset) noexcept
    {
        Close();
        m_hEvent = ::CreateEventW(nullptr, bManualReset, FALSE, nullptr);
        if (!m_hEvent)
        {
            return HRESULT(::GetLastError());
        }

        // Done
        return S_OK;
    };

    DWORD Wait(_In_opt_ DWORD dwTimeoutMs = INFINITE) noexcept
    {
        return (m_hEvent) ? ::WaitForSingleObject(m_hEvent, INFINITE) : ERROR_INVALID_STATE;
    };

    BOOL Set() noexcept
    {
        return (m_hEvent && ::SetEvent(m_hEvent)) ? TRUE : FALSE;
    };

    BOOL Reset() noexcept
    {
        return (m_hEvent && ::ResetEvent(m_hEvent)) ? TRUE : FALSE;
    };

    VOID Close() noexcept
    {
        if (m_hEvent)
        {
            ::CloseHandle(m_hEvent);
            m_hEvent = nullptr;
        }
    };

    operator HANDLE() const noexcept
    {
        return m_hEvent;
    };

    HANDLE GetEvent() const noexcept
    {
        return m_hEvent;
    };

private:
    HANDLE m_hEvent{ nullptr };
};
