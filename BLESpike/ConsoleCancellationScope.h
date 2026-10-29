#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

class ConsoleCancellationScope
{
public:
    ConsoleCancellationScope() noexcept = default;
    ConsoleCancellationScope(_In_ ConsoleCancellationScope const&) = delete;
    ConsoleCancellationScope(_Inout_ ConsoleCancellationScope &&) = delete;
    ~ConsoleCancellationScope() noexcept;

    ConsoleCancellationScope& operator=(_In_ ConsoleCancellationScope const&) = delete;
    ConsoleCancellationScope& operator=(_Inout_ ConsoleCancellationScope &&) = delete;

    _Must_inspect_result_ _Success_(return == S_OK)
    HRESULT Initialize() noexcept;

    _Ret_maybenull_
    HANDLE GetEvent() const noexcept;

private:
    HANDLE m_hEvent = nullptr;
    BOOL m_bIsHandlerInstalled = FALSE;
};
