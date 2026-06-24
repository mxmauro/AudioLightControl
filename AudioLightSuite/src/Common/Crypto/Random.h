#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

HRESULT RandomBytes(_Out_writes_bytes_(nLen) LPBYTE lpBytes, _In_ SIZE_T nLen);
