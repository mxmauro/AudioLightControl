#include "RundownProtection.h"
#include <crtdbg.h>

#define RP_COUNTER_MASK      0x7FFFFFFFL
#define RP_TEARING_DOWN_MASK 0x80000000L

// -----------------------------------------------------------------------------

VOID RundownProt_Initialize(_Inout_ _Interlocked_operand_ LONG volatile *lpnValue)
{
    _InterlockedExchange(lpnValue, 0);
    return;
}

BOOL RundownProt_Acquire(_Inout_ _Interlocked_operand_ LONG volatile *lpnValue)
{
    LONG initVal, newVal;

    newVal = _InterlockedOr(lpnValue, 0);
    do
    {
        initVal = newVal;
        if ((initVal & RP_TEARING_DOWN_MASK) != 0)
            return FALSE;
        newVal = _InterlockedCompareExchange(lpnValue, initVal + 1, initVal);
    }
    while (newVal != initVal);
    return TRUE;
}

VOID RundownProt_Release(_Inout_ _Interlocked_operand_ LONG volatile *lpnValue)
{
    LONG initVal, newVal;

    newVal = _InterlockedOr(lpnValue, 0);
    do
    {
        initVal = newVal;
        _ASSERT((initVal & RP_COUNTER_MASK) != 0);
        newVal = (initVal & RP_TEARING_DOWN_MASK) | ((initVal & RP_COUNTER_MASK) - 1);
        newVal = _InterlockedCompareExchange(lpnValue, newVal, initVal);
    }
    while (newVal != initVal);
    return;
}

VOID RundownProt_WaitForRelease(_Inout_ _Interlocked_operand_ LONG volatile *lpnValue)
{
    //Mark rundown protection as shutting down
    _InterlockedOr(lpnValue, RP_TEARING_DOWN_MASK);

    //Wait until no locks active
    while (_InterlockedOr(lpnValue, 0) != RP_TEARING_DOWN_MASK)
    {
        ::Sleep(10);
    }
    return;
}

BOOL RundownProt_IsTearingDown(_Inout_ _Interlocked_operand_ LONG volatile *lpnValue)
{
    return (_InterlockedOr(lpnValue, 0) != RP_TEARING_DOWN_MASK) ? TRUE : FALSE;
}
