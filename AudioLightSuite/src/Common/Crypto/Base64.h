#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------

namespace Base64 {

HRESULT Encode(_In_reads_bytes_(nDataLen) LPCVOID lpData, _In_ SIZE_T nDataLen, _In_ BOOL bIsUrl, _Out_ std::string &encoded);
HRESULT Decode(_In_reads_(nDataLen) LPCSTR szDataA, _In_ SIZE_T nDataLen, _In_ BOOL bIsUrl, _Out_ std::vector<BYTE> &decoded);

} // namespace Base64
