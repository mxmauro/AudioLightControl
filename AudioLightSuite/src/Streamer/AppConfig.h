#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include "Crypto\P256.h"

// -----------------------------------------------------------------------------

class CConfig
{
public:
    typedef enum class DeviceType_e {
        Strip5050 = 0
    } DeviceType;

    typedef struct Device_s {
        std::wstring strName;
        DeviceType eType{ DeviceType::Strip5050 };
        std::wstring strHost;
        std::string strUserName;
        BYTE aPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES]{};
    } Device;

public:
    std::vector<Device> m_vecDevices;
};

// -----------------------------------------------------------------------------

HRESULT ReadAppConfig();

const CConfig& GetAppConfig();
