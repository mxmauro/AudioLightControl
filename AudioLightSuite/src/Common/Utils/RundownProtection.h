#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

VOID RundownProt_Initialize(_Inout_ _Interlocked_operand_ LONG volatile *m_lpnValue);
BOOL RundownProt_Acquire(_Inout_ _Interlocked_operand_ LONG volatile *m_lpnValue);
VOID RundownProt_Release(_Inout_ _Interlocked_operand_ LONG volatile *m_lpnValue);
VOID RundownProt_WaitForRelease(_Inout_ _Interlocked_operand_ LONG volatile *m_lpnValue);
BOOL RundownProt_IsTearingDown(_Inout_ _Interlocked_operand_ LONG volatile *m_lpnValue);

// -----------------------------------------------------------------------------

class CAutoRundownProtection
{
public:
    CAutoRundownProtection() = delete;
    explicit CAutoRundownProtection(_Inout_ _Interlocked_operand_ LONG volatile *_lpnValue) noexcept
    {
        Acquire(_lpnValue);
        return;
    };
    CAutoRundownProtection(_In_ const CAutoRundownProtection &) = delete;
    CAutoRundownProtection(_Inout_ CAutoRundownProtection &&) = delete;

    ~CAutoRundownProtection() noexcept
    {
        Release();
        return;
    };

    CAutoRundownProtection &operator=(_In_ const CAutoRundownProtection &) = delete;
    CAutoRundownProtection &operator=(_Inout_ CAutoRundownProtection &&) = delete;

    bool operator!() const noexcept
    {
        return (!m_lpnValue) ? true : false;
    };

    operator bool() const noexcept
    {
        return (m_lpnValue) ? true : false;
    };

    BOOL IsAcquired() const noexcept
    {
        return (m_lpnValue) ? TRUE : FALSE;
    };

    BOOL Acquire(_Inout_ _Interlocked_operand_ LONG volatile *_lpnValue) noexcept
    {
        m_lpnValue = (RundownProt_Acquire(_lpnValue) != FALSE) ? _lpnValue : nullptr;
        return (m_lpnValue) ? TRUE : FALSE;
    };

    VOID Release() noexcept
    {
        if (m_lpnValue)
        {
            LONG volatile *lpnValueOrig = m_lpnValue;

            m_lpnValue = nullptr;
            RundownProt_Release(lpnValueOrig);
        }
    };

private:
    LONG volatile *m_lpnValue;
};
