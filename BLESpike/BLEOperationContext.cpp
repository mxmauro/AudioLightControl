#include "BLEOperationContext.h"

// -----------------------------------------------------------------------------

BLEOperationContext::BLEOperationContext(_In_opt_ HANDLE hCancellationEvent, _In_ DWORD dwTimeoutMs) noexcept
    : m_hCancellationEvent(hCancellationEvent), m_ullDeadline(0), m_bHasDeadline(dwTimeoutMs != INFINITE)
{
    if (m_bHasDeadline)
    {
        m_ullDeadline = GetTickCount64() + static_cast<ULONGLONG>(dwTimeoutMs);
    }
}

BLEOperationContext BLEOperationContext::WithCancellationEvent(_In_ HANDLE hCancellationEvent) const noexcept
{
    BLEOperationContext context(*this);

    context.m_hCancellationEvent = hCancellationEvent;
    return context;
}

BLEOperationContext BLEOperationContext::WithDeadline(_In_ DWORD dwTimeoutMs) const noexcept
{
    BLEOperationContext context(*this);

    context.m_bHasDeadline = (dwTimeoutMs != INFINITE) ? TRUE : FALSE;
    context.m_ullDeadline = context.m_bHasDeadline ? GetTickCount64() + static_cast<ULONGLONG>(dwTimeoutMs) : MAXULONGLONG;
    return context;
}

HANDLE BLEOperationContext::CancellationEvent() const noexcept
{
    return m_hCancellationEvent;
}

BOOL BLEOperationContext::HasDeadline() const noexcept
{
    return m_bHasDeadline;
}

ULONGLONG BLEOperationContext::Deadline() const noexcept
{
    return m_ullDeadline;
}

DWORD BLEOperationContext::RemainingMilliseconds() const noexcept
{
    ULONGLONG ullNow;

    if (!m_bHasDeadline)
    {
        return INFINITE;
    }

    ullNow = GetTickCount64();
    if (ullNow >= m_ullDeadline)
    {
        return 0;
    }

    return static_cast<DWORD>(m_ullDeadline - ullNow);
}

HRESULT BLEOperationContext::Check() const noexcept
{
    DWORD dwWaitResult;

    if (m_hCancellationEvent)
    {
        dwWaitResult = WaitForSingleObject(m_hCancellationEvent, 0);
        if (dwWaitResult == WAIT_OBJECT_0)
        {
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
        if (dwWaitResult == WAIT_FAILED)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }
    if (RemainingMilliseconds() == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    return S_OK;
}

HRESULT BLEOperationContext::Wait(_In_reads_(dwHandleCount) HANDLE const* phHandles, _In_ DWORD dwHandleCount,
                                  _Out_ DWORD* pdwSignaledIndex) const noexcept
{
    HANDLE ahWaitHandles[MAXIMUM_WAIT_OBJECTS];
    DWORD dwWaitResult;
    DWORD dwMaximumHandles;
    HRESULT hr;

    if (!pdwSignaledIndex)
    {
        return E_INVALIDARG;
    }
    *pdwSignaledIndex = MAXDWORD;

    if ((!phHandles) || dwHandleCount == 0)
    {
        return E_INVALIDARG;
    }

    hr = Check();
    if (FAILED(hr))
    {
        return hr;
    }

    dwMaximumHandles = CancellationEvent() ? MAXIMUM_WAIT_OBJECTS - 1 : MAXIMUM_WAIT_OBJECTS;
    if (dwHandleCount > dwMaximumHandles)
    {
        return E_INVALIDARG;
    }

    CopyMemory(ahWaitHandles, phHandles, sizeof(*phHandles) * dwHandleCount);
    if (CancellationEvent())
    {
        ahWaitHandles[dwHandleCount] = CancellationEvent();
    }

    dwWaitResult = WaitForMultipleObjects(dwHandleCount + (CancellationEvent() ? 1 : 0), ahWaitHandles, FALSE, RemainingMilliseconds());
    if (dwWaitResult >= WAIT_OBJECT_0 && dwWaitResult < WAIT_OBJECT_0 + dwHandleCount)
    {
        *pdwSignaledIndex = dwWaitResult - WAIT_OBJECT_0;
        hr = Check();
        return (SUCCEEDED(hr)) ? S_OK : hr;
    }

    if (CancellationEvent() && dwWaitResult == WAIT_OBJECT_0 + dwHandleCount)
    {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }

    if (dwWaitResult == WAIT_TIMEOUT)
    {
        hr = Check();
        if (FAILED(hr))
        {
            return hr;
        }
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }

    if (dwWaitResult == WAIT_FAILED)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return E_FAIL;
}
