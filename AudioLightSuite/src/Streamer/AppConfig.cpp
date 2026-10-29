#include "AppConfig.h"
#include "CommandLine.h"
#include "Crypto\Base64.h"
#include "Devices\DmxIotDevice.h"
#include "Devices\GoveeStripIotDevice.h"
#include "IotComm\IotCommClient.h"
#pragma warning(disable : 26819)
#include "ThirdParty\nlohmann_json\json.hpp"
#pragma warning(default : 26819)
#include "Utils\MessageBoxUtils.h"
#include "Utils\WindowsHandle.h"
#include <windns.h>
#include <WS2tcpip.h>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#pragma comment(lib, "Dnsapi.lib")
#pragma comment(lib, "Ws2_32.lib")

// -----------------------------------------------------------------------------

static CConfig cAppConfig;

// -----------------------------------------------------------------------------

static VOID ReadWholeFile(_In_z_ LPCWSTR szFilePathW, _Out_ std::vector<char> &data);
static VOID Utf8ToWide(_In_reads_(nUtf8Len) LPCSTR szUtf8, _In_ SIZE_T nUtf8Len, _Out_ std::wstring &strOut);
static BOOL IsValidHostname(_In_z_ LPCWSTR szHostNameW);
static BOOL IsValidHostnameAndPort(_In_z_ LPCWSTR szHostNameAndPortW);
static CConfig::DeviceEffect ParseEffect(_In_ const std::string &strEffect);
static VOID ParseEffects(_In_ const nlohmann::json &jValue, _Out_ std::vector<CConfig::DeviceEffect> &vecEffects);
static VOID TrimWhitespace(_Inout_ std::wstring &strValue);
static BOOL IsCustomDmxFixtureType(_In_z_ LPCWSTR szFixtureTypeW);
static BOOL IsAutomaticDmxFixtureType(_In_z_ LPCWSTR szFixtureTypeW);
static VOID ParseDmxFixtureChannels(_In_ const nlohmann::json &jValue, _Out_ std::vector<USHORT> &vecChannelMappings);
static USHORT ParseDmxFixtureChannel(_In_ const std::string &strChannel);
[[noreturn]] static VOID ThrowConfigurationError(_In_z_ LPCSTR szReason);
[[noreturn]] static VOID ThrowDeviceConfigurationError(_In_ SIZE_T nDeviceIndex, _In_z_ LPCSTR szReason);
[[noreturn]] static VOID ThrowFixtureConfigurationError(_In_ SIZE_T nDeviceIndex, _In_ SIZE_T nFixtureIndex, _In_z_ LPCSTR szReason);
[[noreturn]] static VOID ThrowWin32Error(_In_ DWORD dwError, _In_z_ LPCSTR szAction);
static VOID ShowConfigurationError(_In_ const std::exception &exception) noexcept;
static VOID ClearLoadedConfig() noexcept;

// -----------------------------------------------------------------------------

HRESULT ReadAppConfig()
{
    std::vector<char> fileData;
    std::vector<BYTE> decodedPrivateKey;
    nlohmann::json j;
    HRESULT hRes = S_OK;

    // Discard stale settings and private keys before loading a new configuration.
    ClearLoadedConfig();

    try
    {
        ReadWholeFile(GetCmdLimeParameters().strConfigPathW.c_str(), fileData);

        // Decode UTF-8 JSON while accepting an optional UTF-8 BOM and rejecting UTF-16 input.
        if (fileData.size() >= 3 &&
            static_cast<unsigned char>(fileData[0]) == 0xEF &&
            static_cast<unsigned char>(fileData[1]) == 0xBB &&
            static_cast<unsigned char>(fileData[2]) == 0xBF)
        {
            j = nlohmann::json::parse(fileData.begin() + 3, fileData.end(), nullptr, true, true);
        }
        else if (fileData.size() >= 2 &&
                 ((static_cast<unsigned char>(fileData[0]) == 0xFF &&
                   static_cast<unsigned char>(fileData[1]) == 0xFE) ||
                  (static_cast<unsigned char>(fileData[0]) == 0xFE &&
                   static_cast<unsigned char>(fileData[1]) == 0xFF)))
        {
            ThrowConfigurationError("The configuration file is UTF-16. Save it as UTF-8.");
        }
        else
        {
            j = nlohmann::json::parse(fileData.begin(), fileData.end(), nullptr, true, true);
        }

        // The root device list is the required application configuration entry point.
        if (!(j.is_object() && j.contains("devices") && j["devices"].is_array()))
        {
            ThrowConfigurationError("The root object must contain a \"devices\" array.");
        }
        if (j["devices"].empty())
        {
            ThrowConfigurationError("The \"devices\" array must contain at least one device.");
        }

        for (SIZE_T nDeviceIndex = 0; nDeviceIndex < j["devices"].size(); nDeviceIndex++)
        {
            const nlohmann::json &jDevice = j["devices"][nDeviceIndex];
            CConfig::Device device;
            std::string strType, strHost, strUserName, strPrivateKey, strName;
            BOOL bRequiresAudioLightNodeCredentials = FALSE;

            if (!(jDevice.is_object() &&
                  jDevice.contains("name") && jDevice["name"].is_string() &&
                  jDevice.contains("type") && jDevice["type"].is_string() &&
                  jDevice.contains("host") && jDevice["host"].is_string()))
            {
                ThrowDeviceConfigurationError(nDeviceIndex, "must define string name, type, and host fields.");
            }

            strName = jDevice["name"].get<std::string>();
            strType = jDevice["type"].get<std::string>();
            strHost = jDevice["host"].get<std::string>();

            if (strType == "rgb5050")
            {
                if (!jDevice.contains("effect"))
                {
                    ThrowDeviceConfigurationError(nDeviceIndex, "with type \"rgb5050\" requires an effect.");
                }

                ParseEffects(jDevice["effect"], device.vecEffects);
                device.eType = CConfig::DeviceType::Rgb5050;
                bRequiresAudioLightNodeCredentials = TRUE;
            }
            else if (strType == "dmx")
            {
                // Named fixtures are reconciled with the device later; only custom fixtures carry a local DMX layout.
                if (!(jDevice.contains("fixtures") && jDevice["fixtures"].is_array() && !jDevice["fixtures"].empty()))
                {
                    ThrowDeviceConfigurationError(nDeviceIndex, "with type \"dmx\" requires a non-empty fixtures array.");
                }

                device.eType = CConfig::DeviceType::Dmx;
                bRequiresAudioLightNodeCredentials = TRUE;
                for (SIZE_T nFixtureIndex = 0; nFixtureIndex < jDevice["fixtures"].size(); nFixtureIndex++)
                {
                    const nlohmann::json &jFixture = jDevice["fixtures"][nFixtureIndex];
                    CConfig::DmxFixture fixture;
                    std::string strFixtureType;
                    UINT64 ullStartChannel;

                    if (!(jFixture.is_object() &&
                          jFixture.contains("type") && jFixture["type"].is_string() &&
                          jFixture.contains("effect")))
                    {
                        ThrowFixtureConfigurationError(nDeviceIndex, nFixtureIndex, "must define string type and effect fields.");
                    }

                    strFixtureType = jFixture["type"].get<std::string>();
                    Utf8ToWide(strFixtureType.c_str(), strFixtureType.length(), fixture.strType);
                    TrimWhitespace(fixture.strType);
                    if (fixture.strType.empty())
                    {
                        ThrowFixtureConfigurationError(nDeviceIndex, nFixtureIndex, "has an empty type.");
                    }

                    ParseEffects(jFixture["effect"], fixture.vecEffects);
                    fixture.bCustom = IsCustomDmxFixtureType(fixture.strType.c_str());
                    fixture.bAutomatic = IsAutomaticDmxFixtureType(fixture.strType.c_str());
                    if (fixture.bCustom != FALSE)
                    {
                        // A custom fixture bypasses device type matching and must describe its entire DMX channel range.
                        if (!(jFixture.contains("startChannel") && jFixture["startChannel"].is_number_unsigned() &&
                              jFixture.contains("channels")))
                        {
                            ThrowFixtureConfigurationError(nDeviceIndex, nFixtureIndex,
                                                           "with type \"custom\" requires an unsigned startChannel and a channels array.");
                        }

                        ullStartChannel = jFixture["startChannel"].get<UINT64>();
                        if (ullStartChannel == 0 || ullStartChannel > CDmxIotDevice::DMX_UNIVERSE_SIZE)
                        {
                            ThrowFixtureConfigurationError(nDeviceIndex, nFixtureIndex,
                                                           "has a startChannel outside the DMX universe range of 1 through 128.");
                        }

                        ParseDmxFixtureChannels(jFixture["channels"], fixture.vecChannelMappings);
                        if (ullStartChannel + fixture.vecChannelMappings.size() - 1 > CDmxIotDevice::DMX_UNIVERSE_SIZE)
                        {
                            ThrowFixtureConfigurationError(nDeviceIndex, nFixtureIndex, "extends past DMX universe channel 128.");
                        }

                        fixture.nStartChannel = static_cast<USHORT>(ullStartChannel);
                    }
                    else if (jFixture.contains("startChannel") || jFixture.contains("channels"))
                    {
                        ThrowFixtureConfigurationError(nDeviceIndex, nFixtureIndex,
                                                       "must not define startChannel or channels unless its type is \"custom\".");
                    }

                    device.vecDmxFixtures.push_back(std::move(fixture));
                }
            }
            else if (strType == "govee-strip")
            {
                BOOL bSupportsRazer;

                if (!(jDevice.contains("supportsRazer") && jDevice["supportsRazer"].is_boolean() &&
                      jDevice.contains("effect")))
                {
                    ThrowDeviceConfigurationError(nDeviceIndex,
                                                  "with type \"govee-strip\" requires a boolean supportsRazer and an effect.");
                }

                bSupportsRazer = jDevice["supportsRazer"].get<bool>() ? TRUE : FALSE;
                if (bSupportsRazer != FALSE)
                {
                    UINT64 ullStripLength;

                    if (!(jDevice.contains("stripLength") && jDevice["stripLength"].is_number_unsigned()))
                    {
                        ThrowDeviceConfigurationError(nDeviceIndex,
                                                      "with supportsRazer set to true requires an unsigned stripLength.");
                    }

                    ullStripLength = jDevice["stripLength"].get<UINT64>();
                    if (ullStripLength == 0 || ullStripLength > CGoveeStripIotDevice::GOVEE_MAX_STRIP_LENGTH)
                    {
                        ThrowDeviceConfigurationError(nDeviceIndex, "has a stripLength outside the range of 1 through 255.");
                    }

                    device.nStripLength = static_cast<USHORT>(ullStripLength);
                }
                else if (jDevice.contains("stripLength"))
                {
                    ThrowDeviceConfigurationError(nDeviceIndex, "must not define stripLength unless supportsRazer is true.");
                }

                ParseEffects(jDevice["effect"], device.vecEffects);
                device.eType = CConfig::DeviceType::GoveeStrip;
                device.bSupportsRazer = bSupportsRazer;
            }
            else
            {
                ThrowDeviceConfigurationError(nDeviceIndex, "has an unsupported type. Use \"rgb5050\", \"dmx\", or \"govee-strip\".");
            }

            Utf8ToWide(strName.c_str(), strName.length(), device.strName);
            Utf8ToWide(strHost.c_str(), strHost.length(), device.strHost);

            if (!IsValidHostnameAndPort(device.strHost.c_str()))
            {
                ThrowDeviceConfigurationError(nDeviceIndex, "has an invalid host name or port.");
            }
            if (bRequiresAudioLightNodeCredentials != FALSE)
            {
                if (!(jDevice.contains("username") && jDevice["username"].is_string() &&
                      jDevice.contains("privateKey") && jDevice["privateKey"].is_string()))
                {
                    ThrowDeviceConfigurationError(nDeviceIndex, "must define string username and privateKey fields.");
                }
                strUserName = jDevice["username"].get<std::string>();
                strPrivateKey = jDevice["privateKey"].get<std::string>();

                if (strUserName.empty() || strUserName.length() > 255)
                {
                    ThrowDeviceConfigurationError(nDeviceIndex, "has an empty or overly long username.");
                }
                for (char ch : strUserName)
                {
                    if (static_cast<unsigned char>(ch) > 127)
                    {
                        ThrowDeviceConfigurationError(nDeviceIndex, "has a username containing non-ASCII characters.");
                    }
                }
                device.strUserName = strUserName;

                hRes = Base64::Decode(strPrivateKey.c_str(), strPrivateKey.length(), FALSE, decodedPrivateKey);
                if (FAILED(hRes))
                {
                    ThrowDeviceConfigurationError(nDeviceIndex, "has a privateKey that is not valid Base64 data.");
                }
                if (decodedPrivateKey.size() != CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES)
                {
                    ThrowDeviceConfigurationError(nDeviceIndex, "has a privateKey with an invalid decoded length.");
                }
                memcpy(device.aPrivateKey, decodedPrivateKey.data(), CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES);
                SecureZeroMemory(decodedPrivateKey.data(), decodedPrivateKey.size());
                decodedPrivateKey.clear();
            }

            cAppConfig.m_vecDevices.push_back(std::move(device));
            SecureZeroMemory(device.aPrivateKey, sizeof(device.aPrivateKey));
        }

        return S_OK;
    }
    catch (const std::system_error &exception)
    {
        hRes = HRESULT_FROM_WIN32(static_cast<DWORD>(exception.code().value()));
        ShowConfigurationError(exception);
    }
    catch (const std::bad_alloc &)
    {
        hRes = E_OUTOFMEMORY;
        ErrorMessageBoxW(nullptr, L"Not enough memory while reading application configuration.", L"Audio Light Streamer", hRes);
    }
    catch (const std::exception &exception)
    {
        hRes = E_INVALIDARG;
        ShowConfigurationError(exception);
    }
    catch (...)
    {
        hRes = E_UNEXPECTED;
        ErrorMessageBoxW(nullptr, L"Unexpected error while reading application configuration.", L"Audio Light Streamer", hRes);
    }

    if (!decodedPrivateKey.empty())
    {
        SecureZeroMemory(decodedPrivateKey.data(), decodedPrivateKey.size());
    }
    ClearLoadedConfig();
    return hRes;
}

const CConfig &GetAppConfig()
{
    return cAppConfig;
}

// -----------------------------------------------------------------------------

[[noreturn]] static VOID ThrowConfigurationError(_In_z_ LPCSTR szReason)
{
    throw std::invalid_argument(szReason);
}

[[noreturn]] static VOID ThrowDeviceConfigurationError(_In_ SIZE_T nDeviceIndex, _In_z_ LPCSTR szReason)
{
    throw std::invalid_argument("Device #" + std::to_string(nDeviceIndex + 1) + " " + szReason);
}

[[noreturn]] static VOID ThrowFixtureConfigurationError(_In_ SIZE_T nDeviceIndex, _In_ SIZE_T nFixtureIndex, _In_z_ LPCSTR szReason)
{
    throw std::invalid_argument("Device #" + std::to_string(nDeviceIndex + 1) + ", fixture #" + std::to_string(nFixtureIndex + 1) + " " +
                                szReason);
}

[[noreturn]] static VOID ThrowWin32Error(_In_ DWORD dwError, _In_z_ LPCSTR szAction)
{
    throw std::system_error(static_cast<int>(dwError), std::system_category(), szAction);
}

static VOID ShowConfigurationError(_In_ const std::exception &exception) noexcept
{
    WCHAR szReasonW[1024]{};
    WCHAR szMessageW[1200]{};

    if (::MultiByteToWideChar(CP_UTF8, 0, exception.what(), -1, szReasonW, _countof(szReasonW)) <= 0 ||
        swprintf_s(szMessageW, _countof(szMessageW), L"Unable to load application configuration.\n\n%s", szReasonW) < 0)
    {
        ::MessageBoxW(nullptr, L"Unable to load application configuration.", L"Audio Light Streamer", MB_OK | MB_ICONERROR);
        return;
    }

    ::MessageBoxW(nullptr, szMessageW, L"Audio Light Streamer", MB_OK | MB_ICONERROR);
}

static VOID ClearLoadedConfig() noexcept
{
    for (CConfig::Device &device : cAppConfig.m_vecDevices)
    {
        SecureZeroMemory(device.aPrivateKey, sizeof(device.aPrivateKey));
    }
    cAppConfig.m_vecDevices.clear();
}

static VOID ReadWholeFile(_In_z_ LPCWSTR szFilePathW, _Out_ std::vector<char> &data)
{
    CWindowsHandle cFile;
    LARGE_INTEGER fileSize;
    DWORD dwBytesRead = 0;

    data.clear();

    cFile.Attach(::CreateFileW(szFilePathW, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (cFile == INVALID_HANDLE_VALUE)
    {
        ThrowWin32Error(::GetLastError(), "Unable to open the application configuration file");
    }

    if (!::GetFileSizeEx(cFile, &fileSize))
    {
        ThrowWin32Error(::GetLastError(), "Unable to read the application configuration file size");
    }
    if (fileSize.QuadPart < 0 || static_cast<ULONGLONG>(fileSize.QuadPart) > MAXDWORD)
    {
        ThrowWin32Error(ERROR_FILE_TOO_LARGE, "The application configuration file is too large");
    }

    data.resize(static_cast<size_t>(fileSize.QuadPart));

    if (!data.empty())
    {
        if (!::ReadFile(cFile, data.data(), static_cast<DWORD>(data.size()), &dwBytesRead, nullptr))
        {
            data.clear();
            ThrowWin32Error(::GetLastError(), "Unable to read the application configuration file");
        }
        if (dwBytesRead != data.size())
        {
            data.clear();
            ThrowWin32Error(ERROR_HANDLE_EOF, "The application configuration file ended unexpectedly");
        }
    }
}

static VOID Utf8ToWide(_In_reads_(nUtf8Len) LPCSTR szUtf8, _In_ SIZE_T nUtf8Len, _Out_ std::wstring &strOut)
{
    int nWideLen;

    strOut.clear();

    if (!szUtf8)
    {
        ThrowConfigurationError("A required configuration string is missing.");
    }
    if (nUtf8Len == 0)
    {
        return;
    }
    if (nUtf8Len > static_cast<SIZE_T>(INT_MAX))
    {
        ThrowConfigurationError("A configuration string is too long to decode as UTF-8.");
    }

    nWideLen = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, szUtf8, static_cast<int>(nUtf8Len), nullptr, 0);
    if (nWideLen <= 0)
    {
        ThrowWin32Error(::GetLastError(), "A configuration string is not valid UTF-8");
    }

    strOut.resize(static_cast<size_t>(nWideLen));

    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, szUtf8, static_cast<int>(nUtf8Len), strOut.data(), nWideLen) != nWideLen)
    {
        strOut.clear();
        ThrowWin32Error(::GetLastError(), "A configuration string is not valid UTF-8");
    }
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

static CConfig::DeviceEffect ParseEffect(_In_ const std::string &strEffect)
{
    if (strEffect == "spectrum-color")
    {
        return CConfig::DeviceEffect::SpectrumColor;
    }
    if (strEffect == "treble-strobe")
    {
        return CConfig::DeviceEffect::TrebleStrobe;
    }
    if (strEffect == "band-spectrum")
    {
        return CConfig::DeviceEffect::BandSpectrum;
    }
    ThrowConfigurationError("An effect must be \"spectrum-color\", \"treble-strobe\", or \"band-spectrum\".");
}

static VOID ParseEffects(_In_ const nlohmann::json &jValue, _Out_ std::vector<CConfig::DeviceEffect> &vecEffects)
{
    vecEffects.clear();

    if (jValue.is_string())
    {
        vecEffects.push_back(ParseEffect(jValue.get<std::string>()));
        return;
    }

    if (!jValue.is_array() || jValue.empty())
    {
        ThrowConfigurationError("An effect must be a string or a non-empty array of effect strings.");
    }

    for (const nlohmann::json &jEffect : jValue)
    {
        if (!jEffect.is_string())
        {
            ThrowConfigurationError("Every effect in an effects array must be a string.");
        }
        vecEffects.push_back(ParseEffect(jEffect.get<std::string>()));
    }
}

static VOID TrimWhitespace(_Inout_ std::wstring &strValue)
{
    SIZE_T nBegin = 0;
    SIZE_T nEnd = strValue.length();

    while (nBegin < nEnd && iswspace(strValue[nBegin]))
    {
        nBegin++;
    }
    while (nEnd > nBegin && iswspace(strValue[nEnd - 1]))
    {
        nEnd--;
    }

    strValue = strValue.substr(nBegin, nEnd - nBegin);
}

static BOOL IsCustomDmxFixtureType(_In_z_ LPCWSTR szFixtureTypeW)
{
    return (::CompareStringOrdinal(szFixtureTypeW, -1, L"custom", -1, TRUE) == CSTR_EQUAL) ? TRUE : FALSE;
}

static BOOL IsAutomaticDmxFixtureType(_In_z_ LPCWSTR szFixtureTypeW)
{
    return (::CompareStringOrdinal(szFixtureTypeW, -1, L"auto", -1, TRUE) == CSTR_EQUAL ||
            ::CompareStringOrdinal(szFixtureTypeW, -1, L"automatic", -1, TRUE) == CSTR_EQUAL) ? TRUE : FALSE;
}

static VOID ParseDmxFixtureChannels(_In_ const nlohmann::json &jValue, _Out_ std::vector<USHORT> &vecChannelMappings)
{
    // Preserve DMX channel order while translating the JSON text representation to protocol entries.
    if (!jValue.is_array() || jValue.empty() || jValue.size() > 32)
    {
        ThrowConfigurationError("A custom fixture channels array must contain between 1 and 32 entries.");
    }

    vecChannelMappings.clear();
    vecChannelMappings.reserve(jValue.size());
    for (const nlohmann::json &jChannel : jValue)
    {
        if (!jChannel.is_string())
        {
            ThrowConfigurationError("Every custom fixture channel must be a string.");
        }

        vecChannelMappings.push_back(ParseDmxFixtureChannel(jChannel.get<std::string>()));
    }
}

static USHORT ParseDmxFixtureChannel(_In_ const std::string &strChannel)
{
    SIZE_T nBegin = 0;
    SIZE_T nEnd = strChannel.length();
    std::string strToken;

    while (nBegin < nEnd && (strChannel[nBegin] == ' ' || (strChannel[nBegin] >= '\t' && strChannel[nBegin] <= '\r')))
    {
        nBegin++;
    }
    while (nEnd > nBegin && (strChannel[nEnd - 1] == ' ' || (strChannel[nEnd - 1] >= '\t' && strChannel[nEnd - 1] <= '\r')))
    {
        nEnd--;
    }
    if (nBegin == nEnd)
    {
        ThrowConfigurationError("A custom fixture channel name cannot be empty.");
    }

    // Dynamic names are ASCII case-insensitive; fixed values remain exactly two hexadecimal digits.
    strToken.assign(strChannel.data() + nBegin, nEnd - nBegin);
    for (char &ch : strToken)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }

    if (strToken == "dimmer" || strToken == "dimmer.coarse")
    {
        return CIotCommClient::DmxFixtureChannelMappingDimmer;
    }
    if (strToken == "dimmer.fine")
    {
        return CIotCommClient::DmxFixtureChannelMappingDimmerFine;
    }
    if (strToken == "color.red")
    {
        return CIotCommClient::DmxFixtureChannelMappingColorRed;
    }
    if (strToken == "color.red16")
    {
        return CIotCommClient::DmxFixtureChannelMappingColorRed16;
    }
    if (strToken == "color.green")
    {
        return CIotCommClient::DmxFixtureChannelMappingColorGreen;
    }
    if (strToken == "color.green16")
    {
        return CIotCommClient::DmxFixtureChannelMappingColorGreen16;
    }
    if (strToken == "color.blue")
    {
        return CIotCommClient::DmxFixtureChannelMappingColorBlue;
    }
    if (strToken == "color.blue16")
    {
        return CIotCommClient::DmxFixtureChannelMappingColorBlue16;
    }
    if (strToken == "color.white")
    {
        return CIotCommClient::DmxFixtureChannelMappingColorWhite;
    }
    if (strToken == "color.white16")
    {
        return CIotCommClient::DmxFixtureChannelMappingColorWhite16;
    }
    if (strToken == "color.uv")
    {
        return CIotCommClient::DmxFixtureChannelMappingColorUV;
    }
    if (strToken == "color.uv16")
    {
        return CIotCommClient::DmxFixtureChannelMappingColorUV16;
    }
    if (strToken == "pan" || strToken == "pan.coarse")
    {
        return CIotCommClient::DmxFixtureChannelMappingPan;
    }
    if (strToken == "pan.fine")
    {
        return CIotCommClient::DmxFixtureChannelMappingPanFine;
    }
    if (strToken == "tilt" || strToken == "tilt.coarse")
    {
        return CIotCommClient::DmxFixtureChannelMappingTilt;
    }
    if (strToken == "tilt.fine")
    {
        return CIotCommClient::DmxFixtureChannelMappingTiltFine;
    }
    if (strToken == "strobe")
    {
        return CIotCommClient::DmxFixtureChannelMappingStrobe;
    }

    if (strToken.length() == 2)
    {
        UINT nValue = 0;

        for (char ch : strToken)
        {
            UINT nDigit;

            if (ch >= '0' && ch <= '9')
            {
                nDigit = ch - '0';
            }
            else if (ch >= 'a' && ch <= 'f')
            {
                nDigit = ch - 'a' + 10;
            }
            else
            {
                ThrowConfigurationError("A fixed custom fixture channel value must use two hexadecimal digits.");
            }
            nValue = (nValue << 4) | nDigit;
        }
        return static_cast<USHORT>(nValue);
    }

    ThrowConfigurationError("A custom fixture channel name is not supported.");
}
