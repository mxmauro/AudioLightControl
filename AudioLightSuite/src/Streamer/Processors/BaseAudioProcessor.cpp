#include "BaseAudioProcessor.h"
#include "Utils\FNV.h"

// -----------------------------------------------------------------------------

ULONGLONG CBaseAudioProcessor::GetID() const noexcept
{
    LPCSTR szTypeA = GetType();

    return Hash::Fnv1A64(szTypeA, strlen(szTypeA));
}

VOID CBaseAudioProcessor::TimerTick() noexcept
{
}
