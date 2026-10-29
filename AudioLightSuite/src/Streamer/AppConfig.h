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
        Rgb5050 = 0,
        Dmx,
        GoveeStrip,
    } DeviceType;

    typedef enum class DeviceEffect_e {
        SpectrumColor = 0,
        TrebleStrobe,
        BandSpectrum,
    } DeviceEffect;

    static LPCSTR GetAudioProcessorType(_In_ DeviceEffect eEffect) noexcept
    {
        switch (eEffect)
        {
            case DeviceEffect::SpectrumColor:
                return "SpectrumColor";

            case DeviceEffect::TrebleStrobe:
                return "TrebleStrobe";

            case DeviceEffect::BandSpectrum:
                return "BandSpectrum";

            default:
                return nullptr;
        }
    }

    typedef struct DmxFixture_s {
        std::wstring strType;
        BOOL bCustom{ FALSE };
        BOOL bAutomatic{ FALSE };
        USHORT nStartChannel{ 0 };
        std::vector<USHORT> vecChannelMappings;
        std::vector<DeviceEffect> vecEffects;
    } DmxFixture;

    typedef struct Device_s {
        std::wstring strName;
        DeviceType eType{ DeviceType::Rgb5050 };
        std::wstring strHost;
        std::string strUserName;
        BYTE aPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES]{};
        std::vector<DeviceEffect> vecEffects;
        std::vector<DmxFixture> vecDmxFixtures;
        BOOL bSupportsRazer{ FALSE };
        USHORT nStripLength{ 0 };
    } Device;

public:
    std::vector<Device> m_vecDevices;
};

// -----------------------------------------------------------------------------

HRESULT ReadAppConfig();

const CConfig &GetAppConfig();
