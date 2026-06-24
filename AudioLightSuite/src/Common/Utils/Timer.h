#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

// -----------------------------------------------------------------------------

ULONGLONG GetCurrentTimeMs();

// -----------------------------------------------------------------------------

class CDebugProfileTimer
{
public:
    CDebugProfileTimer(_In_z_ LPCSTR _szNameA) : m_szNameA(_szNameA)
    {
        m_ullStartTimeMs = GetCurrentTimeMs();
        _snprintf_s(m_szBufferA, sizeof(m_szBufferA), "%s | Start time: %llu ms.\n", m_szNameA, m_ullStartTimeMs);
        OutputDebugStringA(m_szBufferA);
    }

    ~CDebugProfileTimer()
    {
        ULONGLONG ullNowMs = GetCurrentTimeMs();

        _snprintf_s(m_szBufferA, sizeof(m_szBufferA), "%s | Elapsed time: %llu ms.\n", m_szNameA, ullNowMs - m_ullStartTimeMs);
        OutputDebugStringA(m_szBufferA);
    }

private:
    LPCSTR m_szNameA;
    ULONGLONG m_ullStartTimeMs;
    CHAR m_szBufferA[128];
};
