#include "WinRegistry.h"
#include "Exception2HR.h"

// -----------------------------------------------------------------------------

CWindowsRegistry::~CWindowsRegistry() noexcept
{
    Close();
}

HRESULT CWindowsRegistry::CreateKey(_In_ HKEY hBaseKey, _In_z_ LPCWSTR szSubKeyW) noexcept
{
    HKEY hCreatedKey;
    LONG lRes;

    if ((!hBaseKey) || (!szSubKeyW) || *szSubKeyW == 0)
    {
        return E_POINTER;
    }

    Close();

    hCreatedKey = nullptr;
    lRes = ::RegCreateKeyExW(hBaseKey, szSubKeyW, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &hCreatedKey, nullptr);
    if (lRes != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(lRes);
    }

    // Done
    m_hKey = hCreatedKey;
    return S_OK;
}

HRESULT CWindowsRegistry::OpenKey(_In_ HKEY hBaseKey, _In_z_ LPCWSTR szSubKeyW, _In_ BOOL bReadOnly) noexcept
{
    HKEY hOpenedKey = nullptr;
    LONG lRes;

    if ((!hBaseKey) || (!szSubKeyW) || *szSubKeyW == 0)
    {
        return E_POINTER;
    }

    Close();

    lRes = ::RegOpenKeyExW(hBaseKey, szSubKeyW, 0, (bReadOnly != FALSE) ? KEY_READ : KEY_ALL_ACCESS, &hOpenedKey);
    if (lRes != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(lRes);
    }

    // Done
    m_hKey = hOpenedKey;
    return S_OK;
}

HRESULT CWindowsRegistry::ReadDWord(_In_z_ LPCWSTR szValueNameW, _Out_ DWORD &dwValue) noexcept
{
    DWORD dwType, cbValue;
    LONG lRes;

    dwValue = 0;

    if ((!m_hKey) || (!szValueNameW))
    {
        return E_POINTER;
    }

    dwType = 0;
    cbValue = sizeof(dwValue);
    lRes = ::RegQueryValueExW(m_hKey, szValueNameW, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwValue), &cbValue);
    if (lRes != ERROR_SUCCESS)
    {
        dwValue = 0;
        return HRESULT_FROM_WIN32(lRes);
    }
    if (dwType != REG_DWORD || cbValue != sizeof(dwValue))
    {
        dwValue = 0;
        return HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH);
    }

    // Done
    return S_OK;
}

HRESULT CWindowsRegistry::WriteDWord(_In_z_ LPCWSTR szValueNameW, _In_ DWORD dwValue) noexcept
{
    LONG lRes;

    if ((!m_hKey) || (!szValueNameW))
    {
        return E_POINTER;
    }

    lRes = ::RegSetValueExW(m_hKey, szValueNameW, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&dwValue), (DWORD)sizeof(dwValue));
    if (lRes != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(lRes);
    }

    // Done
    return S_OK;
}

HRESULT CWindowsRegistry::ReadString(_In_z_ LPCWSTR szValueNameW, _Out_ std::wstring &strValueW) noexcept
{
    return TranslateToHresult([&]()
    {
        DWORD dwType = 0;
        DWORD cbValue = 0;
        LONG lRes;

        if (!m_hKey || !szValueNameW)
        {
            throw E_POINTER;
        }

        strValueW.clear();

        lRes = ::RegQueryValueExW(m_hKey, szValueNameW, nullptr, &dwType, nullptr, &cbValue);
        if (lRes != ERROR_SUCCESS)
        {
            throw HRESULT_FROM_WIN32(lRes);
        }
        if (dwType != REG_SZ)
        {
            throw HRESULT_FROM_WIN32(ERROR_DATATYPE_MISMATCH);
        }
        if ((cbValue % sizeof(WCHAR)) != 0)
        {
            throw E_UNEXPECTED;
        }
        if (cbValue == 0)
        {
            return;
        }

        strValueW.resize(cbValue / sizeof(WCHAR));

        lRes = ::RegQueryValueExW(m_hKey, szValueNameW, nullptr, nullptr, reinterpret_cast<LPBYTE>(strValueW.data()), &cbValue);
        if (lRes != ERROR_SUCCESS)
        {
            throw HRESULT_FROM_WIN32(lRes);
        }

        while (!strValueW.empty() && strValueW.back() == L'\0')
        {
            strValueW.pop_back();
        }
    });
}

HRESULT CWindowsRegistry::WriteString(_In_z_ LPCWSTR szValueNameW, _In_z_ LPCWSTR szValueW) noexcept
{
    DWORD cbValue;
    size_t cchValue;
    LONG lRes;

    if ((!m_hKey) || (!szValueNameW) || (!szValueW))
    {
        return E_POINTER;
    }

    cchValue = wcslen(szValueW) + 1;
    if (cchValue > (MAXDWORD / sizeof(WCHAR)))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    cbValue = static_cast<DWORD>(cchValue * sizeof(WCHAR));
    lRes = ::RegSetValueExW(m_hKey, szValueNameW, 0, REG_SZ, reinterpret_cast<const BYTE *>(szValueW), cbValue);
    if (lRes != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(lRes);
    }

    // Done
    return S_OK;
}

HRESULT CWindowsRegistry::DeleteKey(_In_z_ LPCWSTR szSubKeyW) noexcept
{
    LONG lRes;

    if ((!m_hKey) || (!szSubKeyW) || *szSubKeyW == 0)
    {
        return E_POINTER;
    }

    lRes = ::RegDeleteKeyW(m_hKey, szSubKeyW);
    if (lRes != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(lRes);
    }

    // Done
    return S_OK;
}

HRESULT CWindowsRegistry::DeleteValue(_In_z_ LPCWSTR szValueNameW) noexcept
{
    LONG lRes;

    if ((!m_hKey) || (!szValueNameW) || *szValueNameW == 0)
    {
        return E_POINTER;
    }

    lRes = ::RegDeleteValueW(m_hKey, szValueNameW);
    if (lRes != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(lRes);
    }

    // Done
    return S_OK;
}

VOID CWindowsRegistry::Close() noexcept
{
    if (m_hKey)
    {
        ::RegCloseKey(m_hKey);
        m_hKey = nullptr;
    }
}
