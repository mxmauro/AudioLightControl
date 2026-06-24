#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// -----------------------------------------------------------------------------

class CPersistantSettings
{
public:
    CPersistantSettings() noexcept = default;
    CPersistantSettings(_In_ const CPersistantSettings &) = delete;
    CPersistantSettings(_Inout_ CPersistantSettings &&) = delete;
    virtual ~CPersistantSettings() noexcept = default;

    CPersistantSettings &operator=(_In_ const CPersistantSettings &) = delete;
    CPersistantSettings &operator=(_Inout_ CPersistantSettings &&) = delete;

    virtual ULONGLONG GetID() const noexcept = 0;

    HRESULT LoadWindowPosition(_Out_ INT &iX, _Out_ INT &iY) noexcept;
    HRESULT SaveWindowPosition(_In_ INT iX, _In_ INT iY) noexcept;
};
