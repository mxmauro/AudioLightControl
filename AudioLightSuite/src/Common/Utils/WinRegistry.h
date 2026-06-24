#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// -----------------------------------------------------------------------------

class CWindowsRegistry
{
public:
    CWindowsRegistry() noexcept = default;
    CWindowsRegistry(_In_ const CWindowsRegistry &) = delete;
    CWindowsRegistry(_Inout_ CWindowsRegistry &&) = delete;
    ~CWindowsRegistry() noexcept;

    CWindowsRegistry &operator=(_In_ const CWindowsRegistry &) = delete;
    CWindowsRegistry &operator=(_Inout_ CWindowsRegistry &&) = delete;

    HRESULT CreateKey(_In_ HKEY hBaseKey, _In_z_ LPCWSTR szSubKeyW) noexcept;
    HRESULT OpenKey(_In_ HKEY hBaseKey, _In_z_ LPCWSTR szSubKeyW, _In_ BOOL bReadOnly) noexcept;

    HRESULT ReadDWord(_In_z_ LPCWSTR szValueNameW, _Out_ DWORD &dwValue) noexcept;
    HRESULT WriteDWord(_In_z_ LPCWSTR szValueNameW, _In_ DWORD dwValue) noexcept;

    HRESULT ReadString(_In_z_ LPCWSTR szValueNameW, _Out_ std::wstring &strValueW) noexcept;
    HRESULT WriteString(_In_z_ LPCWSTR szValueNameW, _In_z_ LPCWSTR szValueW) noexcept;

    HRESULT DeleteValue(_In_z_ LPCWSTR szValueNameW) noexcept;
    HRESULT DeleteKey(_In_z_ LPCWSTR szSubKeyW) noexcept;

    VOID Close() noexcept;

    HKEY GetKey() const noexcept
    {
        return m_hKey;
    };

    operator HKEY() const noexcept
    {
        return m_hKey;
    };

private:
    HKEY m_hKey{ nullptr };
};
