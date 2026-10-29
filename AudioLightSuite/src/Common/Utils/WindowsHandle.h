#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

class CWindowsHandle
{
public:
    CWindowsHandle() noexcept = default;
    explicit CWindowsHandle(_In_ HANDLE hHandle) noexcept :
        m_hHandle(hHandle)
    {
    }

    CWindowsHandle(_In_ const CWindowsHandle &) = delete;
    CWindowsHandle(_Inout_ CWindowsHandle &&) = delete;

    ~CWindowsHandle() noexcept
    {
        Close();
    }

    CWindowsHandle &operator=(_In_ const CWindowsHandle &) = delete;
    CWindowsHandle &operator=(_Inout_ CWindowsHandle &&) = delete;

    // Replace the owned CloseHandle-compatible handle.
    VOID Attach(_In_ HANDLE hHandle) noexcept
    {
        if (m_hHandle != hHandle)
        {
            Close();
            m_hHandle = hHandle;
        }
    }

    // Transfer ownership to the caller without closing the handle.
    HANDLE Detach() noexcept
    {
        HANDLE hHandle = m_hHandle;

        m_hHandle = nullptr;
        return hHandle;
    }

    VOID Close() noexcept
    {
        if (m_hHandle && m_hHandle != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(m_hHandle);
        }
        m_hHandle = nullptr;
    }

    operator HANDLE() const noexcept
    {
        return m_hHandle;
    }

private:
    HANDLE m_hHandle{ nullptr };
};
