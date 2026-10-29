#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

class CRwMutex
{
public:
    CRwMutex() noexcept
    {
        ::InitializeSRWLock(&m_rwLock);
    };
    CRwMutex(_In_ const CRwMutex &) noexcept = delete;
    CRwMutex(_Inout_ CRwMutex &&) noexcept = delete;

    ~CRwMutex() noexcept = default;

    CRwMutex &operator=(_In_ const CRwMutex &) noexcept = delete;
    CRwMutex &operator=(_Inout_ CRwMutex &&) noexcept = delete;

public:
    class CAutoExclusiveLock
    {
    public:
        CAutoExclusiveLock(_In_ CRwMutex &_cRwMutex) noexcept : m_cRwMutex(_cRwMutex)
        {
            ::AcquireSRWLockExclusive(&m_cRwMutex.m_rwLock);
        };

        ~CAutoExclusiveLock() noexcept
        {
            ::ReleaseSRWLockExclusive(&m_cRwMutex.m_rwLock);
        };

    private:
        CRwMutex &m_cRwMutex;
    };

    class CAutoSharedLock
    {
    public:
        CAutoSharedLock(_In_ CRwMutex &_cRwMutex) noexcept : m_cRwMutex(_cRwMutex)
        {
            ::AcquireSRWLockShared(&m_cRwMutex.m_rwLock);
        };

        ~CAutoSharedLock() noexcept
        {
            ::ReleaseSRWLockShared(&m_cRwMutex.m_rwLock);
        };

    private:
        CRwMutex &m_cRwMutex;
    };

private:
    SRWLOCK m_rwLock;
};
