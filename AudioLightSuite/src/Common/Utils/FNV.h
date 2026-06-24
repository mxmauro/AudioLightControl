#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

namespace Hash {

ULONGLONG Fnv1A64(_In_ LPCVOID lpData, _In_ SIZE_T nDataLen, _In_opt_ ULONGLONG ullHash = 0xCBF29CE484222325ULL);

} // namespace Hash
