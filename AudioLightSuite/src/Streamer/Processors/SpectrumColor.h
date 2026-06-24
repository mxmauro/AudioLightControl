#pragma once

#include "BaseAudioProcessor.h"

// -----------------------------------------------------------------------------

class CSpectrumColorAudioProcessor : public CBaseAudioProcessor
{
public:
    typedef struct RGB_s {
        BYTE r, g, b;
    } RGB;

    typedef struct Config_s {
        UINT32 ulSampleRate{ 48000 };

        // Gamma shaping (1.0 = no change)
        // <1 boosts low levels, >1 reduces low levels
        float fGamma{ 0.80f };

        float fSubKickThumpPowerThreshold{ 0.8f };
        float fKickBassPowerThreshold{ 0.8f };
        float fHiHatsSparkleAirPowerThreshold{ 0.8f };

        // Defaults tuned for “progressive house” feel @ ~10ms updates
        float fPowerGamma{ 0.80f };
        float fPowerMin{ 0.02f };
        float fPowerMax{ 0.95f };

        float fInitialValidityTimer{ 0.500f };
        float fInitialDecayTimer{ 0.100f };

        float fHueSpeedMin{ 0.04f }; // cycles/sec
        float fHueSpeedMax{ 0.22f }; // cycles/sec (faster when mids are active)

        float fMidBodySaturationBoost{ 0.0000025f };
        float fMidBodyValueBoost{ 0.00000018f };

        float fSparkGain{ 2.2f };
        float fSparkThreshold{ 0.08f };
        float fSparkAttack{ 0.65f };
        float fSparkDecay{ 0.18f };
        float fSparkWhiteMax{ 0.85f };

        float fMaster{ 1.0f };
    } Config;

public:
    CSpectrumColorAudioProcessor() noexcept = default;
    CSpectrumColorAudioProcessor(_In_ const CSpectrumColorAudioProcessor &) = delete;
    CSpectrumColorAudioProcessor(_Inout_ CSpectrumColorAudioProcessor &&) = delete;
    ~CSpectrumColorAudioProcessor() = default;

    CSpectrumColorAudioProcessor &operator=(_In_ const CSpectrumColorAudioProcessor &) = delete;
    CSpectrumColorAudioProcessor &operator=(_Inout_ CSpectrumColorAudioProcessor &&) = delete;

    VOID Initialize(_In_ Config &sConfig) noexcept;

    LPCSTR GetType() const noexcept final
    {
        return "SpectrumColor";
    }

    VOID FirstRender() noexcept final;

    VOID Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept final;

    HRESULT OnPostAttachEvent() noexcept final;

    static Config GetDefaultConfig()
    {
        Config sDefaultConfig;
        return sDefaultConfig;
    }

    RGB GetCurrentColor() const noexcept
    {
        return m_sCurrentRGB;
    }

    INT GetDefaultWidth() const noexcept final
    {
        return 42;
    }

    INT GetDefaultHeight() const noexcept final
    {
        return 20;
    }

private:
    VOID PaletteColor(_In_ float t, _Out_ float &r, _Out_ float &g, _Out_ float &b) noexcept;
    float NextRand() noexcept;

private:
    Config m_sConfig{};

    float m_fSubKickThumpValidityTimer{ 0.0f };
    float m_fSubKickThumpDecayTimer{ 0.0f };
    float m_fKickBassValidityTimer{ 0.0f };
    float m_fKickBassDecayTimer{ 0.0f };

    float m_fHiHatsSparkleAirValidityTimer{ 0.0f };
    float m_fHiHatsSparkleAirDecayTimer{ 0.0f };

    float m_fLightsHiHatsSparkleAirSmooth{ 0.0f };
    float m_fLightsSpark{ 0.0f };
    float m_fLightsHuePhase{ 0.0f };

    UINT32 m_ulRng{ 0xC0FFEEu };

    RGB m_sCurrentRGB{};
};
