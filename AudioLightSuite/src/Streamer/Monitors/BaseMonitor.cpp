#include "BaseMonitor.h"
#include "Utils\FNV.h"

// -----------------------------------------------------------------------------

ULONGLONG CBaseMonitor::GetID() const noexcept
{
    LPCSTR szTypeA = GetType();

    return Hash::Fnv1A64(szTypeA, strlen(szTypeA));
}

VOID CBaseMonitor::TimerTick() noexcept
{
}
