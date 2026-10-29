#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

// Carries a borrowed cancellation event and an optional absolute operation deadline.
class BLEOperationContext
{
public:
    // Creates a context that borrows its cancellation event and starts the optional timeout budget.
    BLEOperationContext(_In_opt_ HANDLE hCancellationEvent = nullptr, _In_ DWORD dwTimeoutMs = INFINITE) noexcept;
    BLEOperationContext(_In_ BLEOperationContext const&) noexcept = default;
    BLEOperationContext(_Inout_ BLEOperationContext&&) noexcept = default;
    ~BLEOperationContext() noexcept = default;

    BLEOperationContext& operator=(_In_ BLEOperationContext const&) noexcept = default;
    BLEOperationContext& operator=(_Inout_ BLEOperationContext&&) noexcept = default;

    // Returns a copy that borrows the specified cancellation event.
    BLEOperationContext WithCancellationEvent(_In_ HANDLE hCancellationEvent) const noexcept;

    // Returns a copy with a deadline measured from this call, or no deadline for INFINITE.
    BLEOperationContext WithDeadline(_In_ DWORD dwTimeoutMs) const noexcept;

    // Returns the borrowed event that requests cancellation, or nullptr when cancellation is not supported.
    HANDLE CancellationEvent() const noexcept;

    // Returns whether this context has a finite deadline.
    BOOL HasDeadline() const noexcept;

    // Returns the absolute GetTickCount64 deadline, or zero when the deadline is infinite.
    ULONGLONG Deadline() const noexcept;

    // Returns the time remaining before the deadline, or INFINITE when no deadline is set.
    DWORD RemainingMilliseconds() const noexcept;

    // Returns cancellation or timeout status, giving cancellation precedence when both apply.
    HRESULT Check() const noexcept;

    // Waits for a normal handle while honoring this context's cancellation event and deadline.
    HRESULT Wait(_In_reads_(dwHandleCount) HANDLE const* phHandles, _In_ DWORD dwHandleCount,
                 _Out_ DWORD* pdwSignaledIndex) const noexcept;

private:
    HANDLE m_hCancellationEvent;
    ULONGLONG m_ullDeadline;
    BOOL m_bHasDeadline;
};
