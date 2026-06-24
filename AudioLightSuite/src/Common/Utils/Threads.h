#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <functional>

// -----------------------------------------------------------------------------

class CThread
{
public:
    using ThreadRoutine = std::function<DWORD(_In_ CThread *lpThread)>;

    CThread() noexcept = default;
    CThread(_In_ const CThread &) = delete;
    CThread(_Inout_ CThread &&) = delete;
    ~CThread() noexcept
    {
        Stop();
    };

    CThread &operator=(_In_ const CThread &) = delete;
    CThread &operator=(_Inout_ CThread &&) = delete;

    HRESULT Start(_In_ ThreadRoutine cThreadRoutine, _In_opt_ BOOL bPaused = FALSE) noexcept
    {
        HRESULT hRes;

        if (!cThreadRoutine)
        {
            return E_INVALIDARG;
        }
        Stop();

        m_cThreadRoutine = std::move(cThreadRoutine);

        m_hThreadExitEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_hThreadExitEvent)
        {
            hRes = HRESULT_FROM_WIN32(::GetLastError());
            Stop();
            return hRes;
        }
        m_hThread = ::CreateThread(nullptr, 0, CThread::ThreadProc, this, (bPaused != FALSE) ? CREATE_SUSPENDED : 0, &m_dwTid);
        if (!m_hThread)
        {
            hRes = HRESULT_FROM_WIN32(::GetLastError());
            Stop();
            return hRes;
        }

        // Done
        return S_OK;
    };

    VOID Wait() noexcept
    {
        if (m_hThread)
        {
            ::WaitForSingleObject(m_hThread, INFINITE);
        }
    };

    VOID Stop() noexcept
    {
        if (m_hThread)
        {
            ::SetEvent(m_hThreadExitEvent);
            ::WaitForSingleObject(m_hThread, INFINITE);
            ::CloseHandle(m_hThread);
            m_hThread = nullptr;
        }
        if (m_hThreadExitEvent)
        {
            ::CloseHandle(m_hThreadExitEvent);
            m_hThreadExitEvent = nullptr;
        }
        m_dwTid = 0;
        m_cThreadRoutine = nullptr;
    };

    VOID Kill() noexcept
    {
        if (m_hThread)
        {
#pragma warning(suppress : 6258)
            ::TerminateThread(m_hThread, ERROR_OPERATION_ABORTED);
            ::CloseHandle(m_hThread);
            m_hThread = nullptr;
        }
        if (m_hThreadExitEvent)
        {
            ::CloseHandle(m_hThreadExitEvent);
            m_hThreadExitEvent = nullptr;
        }
        m_dwTid = 0;
        m_cThreadRoutine = nullptr;
    };

    VOID StopAsync() noexcept
    {
        if (m_hThread)
        {
            ::SetEvent(m_hThreadExitEvent);
        }
    };

    VOID Resume() noexcept
    {
        if (m_hThread)
        {
            ::ResumeThread(m_hThread);
        }
    };

    VOID Detach() noexcept
    {
        if (m_hThread)
        {
            ::CloseHandle(m_hThread);
            m_hThread = nullptr;
        }
        if (m_hThreadExitEvent)
        {
            ::CloseHandle(m_hThreadExitEvent);
            m_hThreadExitEvent = nullptr;
        }
        m_dwTid = 0;
        m_cThreadRoutine = nullptr;
    };

    DWORD GetThreadId() const noexcept
    {
        return m_dwTid;
    };

    HANDLE GetThreadHandle() const noexcept
    {
        return m_hThread;
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
    static DWORD WINAPI ThreadProc(_In_ LPVOID lpThreadParam)
    {
        CThread *lpThread = reinterpret_cast<CThread *>(lpThreadParam);
        ThreadRoutine cThreadRoutine = lpThread->m_cThreadRoutine;
        HANDLE hThreadExitEvent = lpThread->m_hThreadExitEvent;
        DWORD dwExitCode;

        dwExitCode = cThreadRoutine(lpThread);
        ::SetEvent(hThreadExitEvent);
        return dwExitCode;
    };

private:
    HANDLE m_hThread{ nullptr };
    HANDLE m_hThreadExitEvent{ nullptr };
    DWORD m_dwTid{ 0 };
    ThreadRoutine m_cThreadRoutine;
};
