#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

class CMutex
{
public:
    CMutex() noexcept
    {
#pragma warning(suppress : 6031)
        ::InitializeCriticalSectionAndSpinCount(&m_cs, 4000);
    };
    CMutex(_In_ const CMutex &) noexcept = delete;
    CMutex(_Inout_ CMutex &&) noexcept = delete;

    ~CMutex() noexcept
    {
        ::DeleteCriticalSection(&m_cs);
    };

    CMutex &operator=(_In_ const CMutex &) noexcept = delete;
    CMutex &operator=(_Inout_ CMutex &&) noexcept = delete;

public:
    class CAutoLock
    {
    public:
        CAutoLock(_In_ CMutex &_cMutex) noexcept : m_cMutex(_cMutex)
        {
            ::EnterCriticalSection(&m_cMutex.m_cs);
        };

        ~CAutoLock() noexcept
        {
            ::LeaveCriticalSection(&m_cMutex.m_cs);
        };

    private:
        CMutex &m_cMutex;
    };
private:
    CRITICAL_SECTION m_cs;
};
