#include "FNV.h"

// -----------------------------------------------------------------------------

namespace Hash {

ULONGLONG Fnv1A64(_In_ LPCVOID lpData, _In_ SIZE_T nDataLen, _In_opt_ ULONGLONG ullHash)
{
    const BYTE *s = static_cast<const BYTE *>(lpData);

    while (nDataLen > 0)
    {
        ullHash ^= (ULONGLONG)*s;
        ullHash *= 0x100000001b3ULL; // FNV-1a 64-bit prime
        s++;
        nDataLen--;
    }

    return ullHash;
}

} // namespace Hash
