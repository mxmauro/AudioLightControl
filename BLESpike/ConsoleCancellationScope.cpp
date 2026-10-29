#include "ConsoleCancellationScope.h"
#include <atomic>

// -----------------------------------------------------------------------------

static std::atomic<HANDLE> g_hCancellationEvent = nullptr;

// -----------------------------------------------------------------------------

static BOOL WINAPI ConsoleControlHandler(_In_ DWORD controlType) noexcept;

// -----------------------------------------------------------------------------

ConsoleCancellationScope::~ConsoleCancellationScope() noexcept
{
    if (m_bIsHandlerInstalled)
    {
        SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
    }

    g_hCancellationEvent.store(nullptr);
    if (m_hEvent)
    {
        CloseHandle(m_hEvent);
    }
}

HRESULT ConsoleCancellationScope::Initialize() noexcept
{
    if (m_hEvent || m_bIsHandlerInstalled)
    {
        return E_UNEXPECTED;
    }

    m_hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_hEvent == nullptr)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    g_hCancellationEvent.store(m_hEvent);
    if (!SetConsoleCtrlHandler(ConsoleControlHandler, TRUE))
    {
        HRESULT hr;

        hr = HRESULT_FROM_WIN32(GetLastError());
        g_hCancellationEvent.store(nullptr);
        CloseHandle(m_hEvent);
        m_hEvent = nullptr;
        return hr;
    }

    m_bIsHandlerInstalled = TRUE;
    return S_OK;
}

_Ret_maybenull_
HANDLE ConsoleCancellationScope::GetEvent() const noexcept
{
    return m_hEvent;
}

static BOOL WINAPI ConsoleControlHandler(_In_ DWORD dwControlType) noexcept
{
    switch (dwControlType)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            {
                HANDLE hCancellationEvent;

                hCancellationEvent = g_hCancellationEvent.load();
                if (hCancellationEvent)
                {
                    SetEvent(hCancellationEvent);
                }

            }
            return TRUE;

        default:
            return FALSE;
    }
}
