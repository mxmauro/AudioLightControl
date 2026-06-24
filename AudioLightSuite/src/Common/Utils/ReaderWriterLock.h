#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

class CReaderWriterMutex
{
public:
    CReaderWriterMutex() noexcept = default;
    CReaderWriterMutex(_In_ const CReaderWriterMutex &) = delete;
    CReaderWriterMutex(_Inout_ CReaderWriterMutex &&) = delete;
    ~CReaderWriterMutex() noexcept = default;

    CReaderWriterMutex &operator=(_In_ const CReaderWriterMutex &) = delete;
    CReaderWriterMutex &operator=(_Inout_ CReaderWriterMutex &&) = delete;

public:
    class CAutoSharedLock
    {
    public:
        CAutoSharedLock(_In_ CReaderWriterMutex &_cRwMutex) noexcept : m_cRwMutex(_cRwMutex)
        {
            ::AcquireSRWLockShared(&m_cRwMutex.m_sSRWLock);
        };
        CAutoSharedLock(_In_ const CAutoSharedLock &) = delete;
        CAutoSharedLock(_Inout_ CAutoSharedLock &&) = delete;

        ~CAutoSharedLock() noexcept
        {
            ::ReleaseSRWLockShared(&m_cRwMutex.m_sSRWLock);
        };

        CAutoSharedLock &operator=(_In_ const CAutoSharedLock &) = delete;
        CAutoSharedLock &operator=(_Inout_ CAutoSharedLock &&) = delete;

    private:
        CReaderWriterMutex &m_cRwMutex;
    };

    class CAutoExclusiveLock
    {
    public:
        CAutoExclusiveLock(_In_ CReaderWriterMutex &_cRwMutex) noexcept : m_cRwMutex(_cRwMutex)
        {
            ::AcquireSRWLockExclusive(&m_cRwMutex.m_sSRWLock);
        };
        CAutoExclusiveLock(_In_ const CAutoExclusiveLock &) = delete;
        CAutoExclusiveLock(_Inout_ CAutoExclusiveLock &&) = delete;

        ~CAutoExclusiveLock() noexcept
        {
            ReleaseSRWLockExclusive(&m_cRwMutex.m_sSRWLock);
        };

        CAutoExclusiveLock &operator=(_In_ const CAutoExclusiveLock &) = delete;
        CAutoExclusiveLock &operator=(_Inout_ CAutoExclusiveLock &&) = delete;

    private:
        CReaderWriterMutex &m_cRwMutex;
    };

private:
    SRWLOCK m_sSRWLock SRWLOCK_INIT;
};
