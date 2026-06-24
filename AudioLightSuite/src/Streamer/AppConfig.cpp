#include "AppConfig.h"
#include "CommandLine.h"
#include "Crypto\Base64.h"
#pragma warning(disable : 26819)
#include "ThirdParty\nlohmann_json\json.hpp"
#pragma warning(default : 26819)
#include "Utils\Exception2HR.h"
#include "Utils\MessageBoxUtils.h"
#include <windns.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <vector>

#pragma comment(lib, "Dnsapi.lib")
#pragma comment(lib, "Ws2_32.lib")

#define HRESULT_FROM_LASTERROR() HRESULT_FROM_WIN32(::GetLastError())
#define THROW_IF_FAILED_HR(hr) if (FAILED(hr)) { throw (HRESULT)(hr); }

// -----------------------------------------------------------------------------

static CConfig cAppConfig;

// -----------------------------------------------------------------------------

static HRESULT ReadWholeFile(_In_z_ LPCWSTR szFilePathW, _Out_ std::vector<char> &data);
static HRESULT Utf8ToWide(_In_reads_(nUtf8Len) LPCSTR szUtf8, _In_ SIZE_T nUtf8Len, _Out_ std::wstring &strOut);
static BOOL IsValidHostname(_In_z_ LPCWSTR szHostNameW);
static BOOL IsValidHostnameAndPort(_In_z_ LPCWSTR szHostNameAndPortW);

// -----------------------------------------------------------------------------

HRESULT ReadAppConfig()
{
    std::vector<char> fileData;
    std::vector<BYTE> decodedPrivateKey;
    nlohmann::json j;
    HRESULT hRes;

    cAppConfig.m_vecDevices.clear();

    hRes = ReadWholeFile(GetCmdLimeParameters().strConfigPathW.c_str(), fileData);
    if (SUCCEEDED(hRes))
    {
        hRes = TranslateToHresult([&]()
        {
            if (fileData.size() >= 3 &&
                static_cast<unsigned char>(fileData[0]) == 0xEF &&
                static_cast<unsigned char>(fileData[1]) == 0xBB &&
                static_cast<unsigned char>(fileData[2]) == 0xBF)
            {
                j = nlohmann::json::parse(fileData.begin() + 3, fileData.end());
            }
            else
            {
                j = nlohmann::json::parse(fileData.begin(), fileData.end());
            }

            if (!(j.is_object() && j.contains("devices") && j["devices"].is_array()))
            {
                throw (HRESULT)E_INVALIDARG;
            }

            for (const nlohmann::json &jDevice : j["devices"])
            {
                CConfig::Device device;
                std::string strType, strHost, strUserName, strPrivateKey, strName;

                if (!(jDevice.is_object() &&
                      jDevice.contains("name") && jDevice["name"].is_string() &&
                      jDevice.contains("type") && jDevice["type"].is_string() &&
                      jDevice.contains("host") && jDevice["host"].is_string() &&
                      jDevice.contains("username") && jDevice["username"].is_string() &&
                      jDevice.contains("privateKey") && jDevice["privateKey"].is_string()))
                {
                    throw (HRESULT)E_INVALIDARG;
                }

                strName = jDevice["name"].get<std::string>();
                strType = jDevice["type"].get<std::string>();
                strHost = jDevice["host"].get<std::string>();
                strUserName = jDevice["username"].get<std::string>();
                strPrivateKey = jDevice["privateKey"].get<std::string>();

                if (strType != "5050strip")
                {
                    throw (HRESULT)E_INVALIDARG;
                }
                device.eType = CConfig::DeviceType::Strip5050;

                THROW_IF_FAILED_HR(Utf8ToWide(strName.c_str(), strName.length(), device.strName));
                THROW_IF_FAILED_HR(Utf8ToWide(strHost.c_str(), strHost.length(), device.strHost));

                if (!IsValidHostnameAndPort(device.strHost.c_str()))
                {
                    throw (HRESULT)E_INVALIDARG;
                }
                if (strUserName.empty() || strUserName.length() > 255)
                {
                    throw (HRESULT)E_INVALIDARG;
                }
                for (char ch : strUserName)
                {
                    if (static_cast<unsigned char>(ch) > 127)
                    {
                        throw (HRESULT)E_INVALIDARG;
                    }
                }
                device.strUserName = strUserName;

                hRes = Base64::Decode(strPrivateKey.c_str(), strPrivateKey.length(), FALSE, decodedPrivateKey);
                THROW_IF_FAILED_HR(hRes);
                if (decodedPrivateKey.size() != CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES)
                {
                    throw (HRESULT)E_INVALIDARG;
                }
                memcpy(device.aPrivateKey, decodedPrivateKey.data(), CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES);
                SecureZeroMemory(decodedPrivateKey.data(), decodedPrivateKey.size());
                decodedPrivateKey.clear();

                cAppConfig.m_vecDevices.push_back(device);
            }
        });
    }

    if (SUCCEEDED(hRes))
    {
        if (cAppConfig.m_vecDevices.size() == 0)
        {
            MessageBoxW(nullptr, L"Error: No devices were configured.", L"Audio Light Streamer", MB_OK | MB_ICONERROR);
            return (int)E_FAIL;
        }
    }

    if (FAILED(hRes))
    {
        if (hRes == E_INVALIDARG)
        {
            MessageBoxW(nullptr, L"Error: Invalid application configuration.", L"Audio Light Streamer", MB_OK | MB_ICONERROR);
        }
        else
        {
            ErrorMessageBoxW(nullptr, L"Unexpected error while reading application configuration.", L"Audio Light Streamer", hRes);
        }
    }
    return hRes;
}

const CConfig& GetAppConfig()
{
    return cAppConfig;
}

// -----------------------------------------------------------------------------

static HRESULT ReadWholeFile(_In_z_ LPCWSTR szFilePathW, _Out_ std::vector<char> &data)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    LARGE_INTEGER fileSize;
    DWORD dwBytesRead = 0;
    HRESULT hRes;

    data.clear();

    hFile = ::CreateFileW(szFilePathW, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return HRESULT_FROM_LASTERROR();
    }

    if (!::GetFileSizeEx(hFile, &fileSize))
    {
        hRes = HRESULT_FROM_LASTERROR();
        ::CloseHandle(hFile);
        return hRes;
    }
    if (fileSize.QuadPart < 0 || static_cast<ULONGLONG>(fileSize.QuadPart) > MAXDWORD)
    {
        ::CloseHandle(hFile);
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }

    hRes = TranslateToHresult([&]()
    {
        data.resize(static_cast<size_t>(fileSize.QuadPart));
    });
    if (FAILED(hRes))
    {
        ::CloseHandle(hFile);
        return hRes;
    }

    if (!data.empty())
    {
        if (!::ReadFile(hFile, data.data(), static_cast<DWORD>(data.size()), &dwBytesRead, nullptr))
        {
            hRes = HRESULT_FROM_LASTERROR();
            ::CloseHandle(hFile);
            data.clear();
            return hRes;
        }
        if (dwBytesRead != data.size())
        {
            ::CloseHandle(hFile);
            data.clear();
            return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
        }
    }

    ::CloseHandle(hFile);
    return S_OK;
}

static HRESULT Utf8ToWide(_In_reads_(nUtf8Len) LPCSTR szUtf8, _In_ SIZE_T nUtf8Len, _Out_ std::wstring &strOut)
{
    int nWideLen;
    HRESULT hRes;

    strOut.clear();

    if (!szUtf8)
    {
        return E_INVALIDARG;
    }
    if (nUtf8Len == 0)
    {
        return S_OK;
    }
    if (nUtf8Len > static_cast<SIZE_T>(INT_MAX))
    {
        return E_INVALIDARG;
    }

    nWideLen = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, szUtf8, static_cast<int>(nUtf8Len), nullptr, 0);
    if (nWideLen <= 0)
    {
        return E_INVALIDARG;
    }

    hRes = TranslateToHresult([&]()
    {
        strOut.resize(static_cast<size_t>(nWideLen));
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, szUtf8, static_cast<int>(nUtf8Len), strOut.data(), nWideLen) != nWideLen)
    {
        strOut.clear();
        return E_INVALIDARG;
    }

    return S_OK;
}

static BOOL IsValidHostname(_In_z_ LPCWSTR szHostNameW)
{
    union {
        IN_ADDR sAddr4;
        IN6_ADDR sAddr6;
    } u;

    return (szHostNameW[0] != 0 && wcslen(szHostNameW) <= 255 &&
            (::InetPtonW(AF_INET, szHostNameW, &u.sAddr4) == 1 ||
             ::InetPtonW(AF_INET6, szHostNameW, &u.sAddr6) == 1 ||
             ::DnsValidateName_W(szHostNameW, DnsNameDomain) == ERROR_SUCCESS)) ? TRUE : FALSE;
}

static BOOL IsValidHostnameAndPort(_In_z_ LPCWSTR szHostNameAndPortW)
{
    WCHAR szTempBufW[256];
    LPCWSTR sW;
    SIZE_T nLen;
    DWORD dwPort;

    if (*szHostNameAndPortW == L'[')
    {
        for (sW = szHostNameAndPortW + 1; *sW != 0 && *sW != L']'; sW++);
        if (*sW == 0)
        {
            return FALSE;
        }

        nLen = (SIZE_T)(sW - szHostNameAndPortW) - 1;
        if (nLen > 255)
        {
            return FALSE;
        }

        sW++;

        memcpy(szTempBufW, szHostNameAndPortW + 1, nLen * sizeof(WCHAR));
        szTempBufW[nLen] = 0;

        if (!IsValidHostname(szTempBufW))
        {
            return FALSE;
        }

        if (*sW == 0)
        {
            return TRUE;
        }
        if (*sW != L':')
        {
            return FALSE;
        }
        sW++;
    }
    else
    {
        sW = wcschr(szHostNameAndPortW, L':');
        if (!sW)
        {
            return IsValidHostname(szHostNameAndPortW);
        }

        nLen = (SIZE_T)(sW - szHostNameAndPortW);
        if (nLen > 255)
        {
            return FALSE;
        }

        memcpy(szTempBufW, szHostNameAndPortW, nLen * sizeof(WCHAR));
        szTempBufW[nLen] = 0;

        if (!IsValidHostname(szTempBufW))
        {
            return FALSE;
        }

        sW++;
    }

    if (*sW == 0)
    {
        return FALSE;
    }
    dwPort = 0;
    while (*sW != 0)
    {
        if (*sW < L'0' || *sW > L'9')
        {
            return FALSE;
        }
        dwPort = dwPort * 10 + (DWORD)(*sW - L'0');
        if (dwPort > 65535)
        {
            return FALSE;
        }
        sW++;
    }
    return TRUE;
}
