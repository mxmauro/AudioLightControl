#pragma once

#include "AudioLightNodeIotDevice.h"
#include "..\AppConfig.h"
#include "Utils\RwMutex.h"
#include <vector>

// -----------------------------------------------------------------------------

class CDmxIotDevice : public CAudioLightNodeIotDevice
{
public:
    static constexpr SIZE_T DMX_UNIVERSE_SIZE = 128;

public:
    CDmxIotDevice(_In_ const CAudioProcessors &cAudioProcessors) noexcept;
    CDmxIotDevice(_In_ const CDmxIotDevice &) = delete;
    CDmxIotDevice(_Inout_ CDmxIotDevice &&) = delete;
    ~CDmxIotDevice() noexcept = default;

    CDmxIotDevice &operator=(_In_ const CDmxIotDevice &) = delete;
    CDmxIotDevice &operator=(_Inout_ CDmxIotDevice &&) = delete;

    HRESULT SetupFixtures(_In_ const std::vector<CConfig::DmxFixture> &vecFixtures) noexcept;

    LPCSTR GetType() const noexcept final
    {
        return "Dmx";
    }

    VOID FirstRender() noexcept final;
    VOID Process() noexcept final;
    VOID TimerTick() noexcept final;

    INT GetDefaultWidth() const noexcept final
    {
        return 36;
    }

    INT GetDefaultHeight() const noexcept final
    {
        return DEVICE_FRAME_HEIGHT;
    }

private:
    typedef struct ResolvedFixture_s {
        USHORT nStartChannel{ 0 };
        std::vector<USHORT> vecChannelMappings;
        std::vector<CBaseAudioProcessor *> vecAudioProcessors;
        BOOL bHasRgbMapping{ FALSE };
        BOOL bHasWhiteMapping{ FALSE };
        BOOL bHasUVMapping{ FALSE };
        BOOL bHasDimmerMapping{ FALSE };
        BOOL bHasPanMapping{ FALSE };
        BOOL bHasTiltMapping{ FALSE };
        BOOL bHasStrobeMapping{ FALSE };
    } ResolvedFixture_t;

    typedef struct CurrentState_s {
        BYTE aUniverse[DMX_UNIVERSE_SIZE]{ 0 };
        BOOL bFixedIsActive{ FALSE };
        BYTE uManualRed{ 0 };
        BYTE uManualGreen{ 0 };
        BYTE uManualBlue{ 0 };
        BYTE uManualBrightness{ 255 };
    } CurrentState_t;

protected:
    HRESULT ValidateDeviceConfiguration(_In_ const CIotCommClient::CDeviceConfigCommandResult &cConfig,
                                        _Out_writes_z_(nReasonLen) LPWSTR szReasonW, _In_ SIZE_T nReasonLen) noexcept final;
    BOOL SupportsManualColorActions() const noexcept final
    {
        return TRUE;
    }
    HRESULT OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept final;

private:
    VOID RenderUniversePreview() noexcept;
    VOID WriteFixture(_In_ const ResolvedFixture_t &sFixture, _In_ BOOL bFixed, _In_ BYTE uRed, _In_ BYTE uGreen,
                      _In_ BYTE uBlue) noexcept;

private:
    std::vector<CConfig::DmxFixture> m_vecConfigFixtures;

    CRwMutex m_cResolvedFixturesRwMutex;
    std::vector<ResolvedFixture_t> m_vecResolvedFixtures;

    CRwMutex m_cCurrentStateRwMutex;
    CurrentState_t m_sCurrentState{};
};

