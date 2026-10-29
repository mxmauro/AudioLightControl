#include "DmxIotDevice.h"
#include "Utils\Exception2HR.h"
#include "Utils\RundownProtection.h"
#include <algorithm>
#include <cwctype>
#include <crtdbg.h>

// -----------------------------------------------------------------------------

CDmxIotDevice::CDmxIotDevice(_In_ const CAudioProcessors &cAudioProcessors) noexcept : CAudioLightNodeIotDevice(cAudioProcessors)
{
}

HRESULT CDmxIotDevice::SetupFixtures(_In_ const std::vector<CConfig::DmxFixture> &vecFixtures) noexcept
{
    HRESULT hRes;

    if (vecFixtures.empty())
    {
        return E_INVALIDARG;
    }

    // Allocate the complete initial state before replacing either active member.
    hRes = TranslateToHresult([&]()
    {
        m_vecConfigFixtures = vecFixtures;
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    // Done
    return S_OK;
}

VOID CDmxIotDevice::FirstRender() noexcept
{
    TimerTick();
}

VOID CDmxIotDevice::Process() noexcept
{
    CAutoRundownProtection cAutoRundownProt(&m_lRundownProt);
    BYTE uRed = 0, uGreen = 0, uBlue = 0;

    if (cAutoRundownProt.IsAcquired() == FALSE || IsIotConnected() == FALSE)
    {
        return;
    }

    {
        CRwMutex::CAutoExclusiveLock lock(m_cCurrentStateRwMutex);

        if (m_sCurrentState.bFixedIsActive != FALSE)
        {
            uRed = (BYTE)((USHORT)m_sCurrentState.uManualRed * (USHORT)m_sCurrentState.uManualBrightness / 255);
            uGreen = (BYTE)((USHORT)m_sCurrentState.uManualGreen * (USHORT)m_sCurrentState.uManualBrightness / 255);
            uBlue = (BYTE)((USHORT)m_sCurrentState.uManualBlue * (USHORT)m_sCurrentState.uManualBrightness / 255);
        }

        // Render the validated fixture map atomically so reconnect validation cannot race an audio update.
        memset(m_sCurrentState.aUniverse, 0, sizeof(m_sCurrentState.aUniverse));

        {
            CRwMutex::CAutoSharedLock lockFixtures(m_cResolvedFixturesRwMutex);

            for (const ResolvedFixture_t &sFixture : m_vecResolvedFixtures)
            {
                WriteFixture(sFixture, m_sCurrentState.bFixedIsActive, uRed, uGreen, uBlue);
            }
        }

        m_sIot.cClient.SetRawDmxChannelDataUdpCommand(1, m_sCurrentState.aUniverse, sizeof(m_sCurrentState.aUniverse));    }
}

VOID CDmxIotDevice::TimerTick() noexcept
{
    CBaseIotDevice::TimerTick();
    RenderUniversePreview();
}

HRESULT CDmxIotDevice::ValidateDeviceConfiguration(_In_ const CIotCommClient::CDeviceConfigCommandResult &cConfig,
                                                   _Out_writes_z_(nReasonLen) LPWSTR szReasonW, _In_ SIZE_T nReasonLen) noexcept
{
    const CIotCommClient::CDeviceConfigCommandResult::DeviceConfigDmx_t *lpDmxConfig;
    std::vector<ResolvedFixture_t> vecResolvedFixtures;
    BOOL aUsedChannels[DMX_UNIVERSE_SIZE]{};
    SIZE_T nDeviceFixture = 0;

    auto Fail = [&](LPCWSTR szReason) -> HRESULT
    {
        wcsncpy_s(szReasonW, nReasonLen, szReason, _TRUNCATE);
        return E_INVALIDARG;
    };

    if (cConfig.IsDmxControllerType() == FALSE)
    {
        return Fail(L"Configured as DMX but device output type differs");
    }
    if ((cConfig.CommandFlags() & CMD_FLAG_SET_RAW_DMX_CHANNEL_DATA) == 0)
    {
        return Fail(L"Device does not support raw DMX channel output");
    }

    lpDmxConfig = cConfig.DmxConfig();
    if ((!lpDmxConfig) || lpDmxConfig->vecFixtures.empty())
    {
        return Fail(L"Device has no DMX fixtures");
    }

    try
    {
        // Merge named fixtures supplied by the device with explicitly mapped custom fixtures from the application.
        vecResolvedFixtures.reserve(m_vecConfigFixtures.size());

        for (const CConfig::DmxFixture &sConfigFixture : m_vecConfigFixtures)
        {
            ResolvedFixture_t sFixture;
            BOOL bHasDimmer, bHasDimmerFine;
            BOOL bHasRed, bHasRedFine, bHasGreen, bHasGreenFine, bHasBlue, bHasBlueFine, bHasWhite, bHasWhiteFine;
            BOOL bHasUV, bHasUVFine;
            BOOL bHasPan, bHasPanFine, bHasTilt, bHasTiltFine;

            // Resolve the configured effect names once so the render path only sees processor capabilities.
            sFixture.vecAudioProcessors.reserve(sConfigFixture.vecEffects.size());
            for (CConfig::DeviceEffect eEffect : sConfigFixture.vecEffects)
            {
                CBaseAudioProcessor *lpAudioProcessor = m_cAudioProcessors.FindByType(CConfig::GetAudioProcessorType(eEffect));

                if (!lpAudioProcessor)
                {
                    return Fail(L"A configured audio processor is unavailable");
                }

                sFixture.vecAudioProcessors.push_back(lpAudioProcessor);
            }
            if (sConfigFixture.bCustom != FALSE)
            {
                sFixture.nStartChannel = sConfigFixture.nStartChannel;
                sFixture.vecChannelMappings = sConfigFixture.vecChannelMappings;
            }
            else
            {
                const CIotCommClient::CDeviceConfigCommandResult::DeviceConfigDmxFixture_t *lpDeviceFixture;
                WCHAR szDeviceTypeW[33];
                int iDeviceTypeLen;
                SIZE_T nBegin = 0, nEnd;

                if (nDeviceFixture >= lpDmxConfig->vecFixtures.size())
                {
                    return Fail(L"Application has more named fixtures than the device");
                }
                lpDeviceFixture = &lpDmxConfig->vecFixtures[nDeviceFixture];

                // Validate device fixture type
                iDeviceTypeLen = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, lpDeviceFixture->strFixtureType.data(),
                                                       static_cast<int>(lpDeviceFixture->strFixtureType.size()), szDeviceTypeW, 32);
                if (iDeviceTypeLen <= 0)
                {
                    return Fail(L"Device fixture type is invalid UTF-8");
                }

                // Automatic fixtures retain device order and channel layout while bypassing the configured type check.
                if (sConfigFixture.bAutomatic == FALSE)
                {
                    szDeviceTypeW[iDeviceTypeLen] = 0;
                    nEnd = static_cast<SIZE_T>(iDeviceTypeLen);
                    while (nBegin < nEnd && iswspace(szDeviceTypeW[nBegin]))
                    {
                        nBegin++;
                    }
                    while (nEnd > nBegin && iswspace(szDeviceTypeW[nEnd - 1]))
                    {
                        nEnd--;
                    }
                    if (::CompareStringOrdinal(sConfigFixture.strType.data(), static_cast<int>(sConfigFixture.strType.size()),
                        szDeviceTypeW + nBegin, static_cast<int>(nEnd - nBegin), TRUE) != CSTR_EQUAL)
                    {
                        return Fail(L"Fixture types do not match the device order");
                    }
                }

                sFixture.nStartChannel = lpDeviceFixture->nStartChannel;
                sFixture.vecChannelMappings = lpDeviceFixture->vecColoringEntries;
                nDeviceFixture++;
            }

            if (sFixture.nStartChannel == 0 || sFixture.vecChannelMappings.empty() ||
                sFixture.nStartChannel + sFixture.vecChannelMappings.size() - 1 > DMX_UNIVERSE_SIZE)
            {
                return Fail(L"Fixture channels are outside the DMX universe");
            }

            bHasDimmer = bHasDimmerFine = FALSE;
            bHasRed = bHasRedFine = bHasGreen = bHasGreenFine = bHasBlue = bHasBlueFine = bHasWhite = bHasWhiteFine = FALSE;
            bHasUV = bHasUVFine = FALSE;
            bHasPan = bHasPanFine = bHasTilt = bHasTiltFine = FALSE;

            for (USHORT nMapping : sFixture.vecChannelMappings)
            {
                // Cache semantic mappings and their fine-channel pairs in a single validation pass.
                switch (nMapping)
                {
                    case CIotCommClient::DmxFixtureChannelMappingDimmer:
                        sFixture.bHasDimmerMapping = TRUE;
                        bHasDimmer = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingDimmerFine:
                        sFixture.bHasDimmerMapping = TRUE;
                        bHasDimmerFine = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingColorRed:
                        sFixture.bHasRgbMapping = TRUE;
                        bHasRed = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingColorRed16:
                        sFixture.bHasRgbMapping = TRUE;
                        bHasRedFine = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingColorGreen:
                        sFixture.bHasRgbMapping = TRUE;
                        bHasGreen = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingColorGreen16:
                        sFixture.bHasRgbMapping = TRUE;
                        bHasGreenFine = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingColorBlue:
                        sFixture.bHasRgbMapping = TRUE;
                        bHasBlue = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingColorBlue16:
                        sFixture.bHasRgbMapping = TRUE;
                        bHasBlueFine = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingColorWhite:
                        sFixture.bHasWhiteMapping = TRUE;
                        bHasWhite = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingColorWhite16:
                        sFixture.bHasWhiteMapping = TRUE;
                        bHasWhiteFine = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingColorUV:
                        sFixture.bHasUVMapping = TRUE;
                        bHasUV = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingColorUV16:
                        sFixture.bHasUVMapping = TRUE;
                        bHasUVFine = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingPan:
                        sFixture.bHasPanMapping = TRUE;
                        bHasPan = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingPanFine:
                        sFixture.bHasPanMapping = TRUE;
                        bHasPanFine = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingTilt:
                        sFixture.bHasTiltMapping = TRUE;
                        bHasTilt = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingTiltFine:
                        sFixture.bHasTiltMapping = TRUE;
                        bHasTiltFine = TRUE;
                        break;

                    case CIotCommClient::DmxFixtureChannelMappingStrobe:
                        sFixture.bHasStrobeMapping = TRUE;
                        break;
                }
            }

            if (bHasDimmerFine != FALSE && bHasDimmer == FALSE)
            {
                return Fail(L"The Dimmer fine channel requires the Dimmer coarse channel");
            }
            if (bHasRedFine != FALSE && bHasRed == FALSE)
            {
                return Fail(L"The Red fine channel requires the Red coarse channel");
            }
            if (bHasGreenFine != FALSE && bHasGreen == FALSE)
            {
                return Fail(L"The Green fine channel requires the Green coarse channel");
            }
            if (bHasBlueFine != FALSE && bHasBlue == FALSE)
            {
                return Fail(L"The Blue fine channel requires the Blue coarse channel");
            }
            if (bHasWhiteFine != FALSE && bHasWhite == FALSE)
            {
                return Fail(L"The White fine channel requires the White coarse channel");
            }
            if (bHasUVFine != FALSE && bHasUV == FALSE)
            {
                return Fail(L"The UV fine channel requires the UV coarse channel");
            }
            if (bHasPanFine != FALSE && bHasPan == FALSE)
            {
                return Fail(L"The Pan fine channel requires the Pan coarse channel");
            }
            if (bHasTiltFine != FALSE && bHasTilt == FALSE)
            {
                return Fail(L"The Tilt fine channel requires the Tilt coarse channel");
            }
            
            for (SIZE_T nChannelOffset = 0; nChannelOffset < sFixture.vecChannelMappings.size(); nChannelOffset++)
            {
                SIZE_T nUniverseOffset = sFixture.nStartChannel - 1 + nChannelOffset;

                if (aUsedChannels[nUniverseOffset] != FALSE)
                {
                    return Fail(L"DMX fixture channels overlap");
                }
                aUsedChannels[nUniverseOffset] = TRUE;
            }
            vecResolvedFixtures.push_back(std::move(sFixture));
        }
    }
    catch (const std::bad_alloc &)
    {
        wcsncpy_s(szReasonW, nReasonLen, L"Not enough memory to validate DMX fixtures", _TRUNCATE);
        return E_OUTOFMEMORY;
    }

    if (nDeviceFixture != lpDmxConfig->vecFixtures.size())
    {
        return Fail(L"Application does not cover every device fixture");
    }

    {
        CRwMutex::CAutoExclusiveLock lock(m_cResolvedFixturesRwMutex);

        // Publish only the complete mapping; swapping default-allocator vectors does not allocate.
        m_vecResolvedFixtures.swap(vecResolvedFixtures);
    }

    // Done
    szReasonW[0] = 0;
    return S_OK;
}

VOID CDmxIotDevice::WriteFixture(_In_ const ResolvedFixture_t &sFixture, _In_ BOOL bFixed, _In_ BYTE uRed, _In_ BYTE uGreen,
                                 _In_ BYTE uBlue) noexcept
{
    USHORT nRed = 0, nGreen = 0, nBlue = 0, nWhite = 0, nUV = 0, nDimmer = USHRT_MAX;
    USHORT nPan = 0x7FFF, nTilt = 0x7FFF;
    BOOL bHasRGBColor = FALSE, bHasWhiteColor = FALSE, bHasUVColor = FALSE, bHasDimmer = FALSE;
    BOOL bHasPan = FALSE, bHasTilt = FALSE, bHasStrobe = FALSE, bStrobe = FALSE;
    CBaseAudioProcessor *lpRGBColorProcessor = nullptr, *lpWhiteColorProcessor = nullptr;
    SIZE_T nOffset = sFixture.nStartChannel - 1;

    if (bFixed != FALSE)
    {
        nRed = static_cast<USHORT>(uRed | (static_cast<USHORT>(uRed) << 8));
        nGreen = static_cast<USHORT>(uGreen | (static_cast<USHORT>(uGreen) << 8));
        nBlue = static_cast<USHORT>(uBlue | (static_cast<USHORT>(uBlue) << 8));
        bHasRGBColor = TRUE;
    }
    else
    {
        for (CBaseAudioProcessor *lpAudioProcessor : sFixture.vecAudioProcessors)
        {
            // Each semantic channel is claimed by the first configured processor that provides it.
            if (sFixture.bHasRgbMapping != FALSE || sFixture.bHasWhiteMapping != FALSE)
            {
                if (bHasRGBColor == FALSE && lpAudioProcessor->HasRGBColor() != FALSE)
                {
                    CBaseAudioProcessor::RGB sColor = lpAudioProcessor->GetRGBColor();

                    nRed = sColor.uRed;
                    nGreen = sColor.uGreen;
                    nBlue = sColor.uBlue;
                    bHasRGBColor = TRUE;
                    lpRGBColorProcessor = lpAudioProcessor;
                }
                if (bHasWhiteColor == FALSE && lpAudioProcessor->HasWhiteColor() != FALSE)
                {
                    nWhite = lpAudioProcessor->GetWhiteColor();
                    bHasWhiteColor = TRUE;
                    lpWhiteColorProcessor = lpAudioProcessor;
                }
            }
            if (sFixture.bHasUVMapping != FALSE && bHasUVColor == FALSE && lpAudioProcessor->HasUVColor() != FALSE)
            {
                nUV = lpAudioProcessor->GetUVColor();
                bHasUVColor = TRUE;
            }
            if (sFixture.bHasDimmerMapping != FALSE && bHasDimmer == FALSE && lpAudioProcessor->HasDimmer() != FALSE)
            {
                nDimmer = lpAudioProcessor->GetDimmer();
                bHasDimmer = TRUE;
            }
            if (sFixture.bHasPanMapping != FALSE && bHasPan == FALSE && lpAudioProcessor->HasPan() != FALSE)
            {
                nPan = static_cast<USHORT>(static_cast<INT32>(lpAudioProcessor->GetPan()) + 32768);
                bHasPan = TRUE;
            }
            if (sFixture.bHasTiltMapping != FALSE && bHasTilt == FALSE && lpAudioProcessor->HasTilt() != FALSE)
            {
                nTilt = static_cast<USHORT>(static_cast<INT32>(lpAudioProcessor->GetTilt()) + 32768);
                bHasTilt = TRUE;
            }
            if (sFixture.bHasStrobeMapping != FALSE && bHasStrobe == FALSE && lpAudioProcessor->HasStrobe() != FALSE)
            {
                bStrobe = lpAudioProcessor->GetStrobe();
                bHasStrobe = TRUE;
            }
        }

        // An RGBW fixture cannot combine RGB and White values from separate processors.
        if (sFixture.bHasRgbMapping != FALSE && sFixture.bHasWhiteMapping != FALSE && bHasRGBColor != FALSE &&
            bHasWhiteColor != FALSE && lpRGBColorProcessor != lpWhiteColorProcessor)
        {
            bHasWhiteColor = FALSE;
        }
    }

    if (bHasWhiteColor != FALSE && bHasRGBColor == FALSE)
    {
        nRed = nWhite;
        nGreen = nWhite;
        nBlue = nWhite;
        bHasRGBColor = TRUE;
    }
    if (sFixture.bHasWhiteMapping != FALSE && bHasWhiteColor == FALSE && bHasRGBColor != FALSE)
    {
        // Derive an unavailable White component from RGB using the same map-dependent rule as the device firmware.
        if (sFixture.bHasRgbMapping != FALSE)
        {
            nWhite = (std::min)({ nRed, nGreen, nBlue });
            nRed -= nWhite; nGreen -= nWhite; nBlue -= nWhite;
        }
        else
        {
            nWhite = static_cast<USHORT>((2126UL * nRed + 7152UL * nGreen + 722UL * nBlue + 5000UL) / 10000UL);
        }
    }

    for (USHORT nMapping : sFixture.vecChannelMappings)
    {
        // Emit each configured DMX entry from the resolved semantic components, preserving fixed values verbatim.
        BYTE uValue;

        switch (nMapping)
        {
            case CIotCommClient::DmxFixtureChannelMappingDimmer:
                uValue = static_cast<BYTE>(nDimmer >> 8);
                break;

            case CIotCommClient::DmxFixtureChannelMappingDimmerFine:
                uValue = static_cast<BYTE>(nDimmer);
                break;

            case CIotCommClient::DmxFixtureChannelMappingColorRed:
                uValue = static_cast<BYTE>(nRed >> 8);
                break;

            case CIotCommClient::DmxFixtureChannelMappingColorRed16:
                uValue = static_cast<BYTE>(nRed);
                break;

            case CIotCommClient::DmxFixtureChannelMappingColorGreen:
                uValue = static_cast<BYTE>(nGreen >> 8);
                break;

            case CIotCommClient::DmxFixtureChannelMappingColorGreen16:
                uValue = static_cast<BYTE>(nGreen);
                break;

            case CIotCommClient::DmxFixtureChannelMappingColorBlue:
                uValue = static_cast<BYTE>(nBlue >> 8);
                break;

            case CIotCommClient::DmxFixtureChannelMappingColorBlue16:
                uValue = static_cast<BYTE>(nBlue);
                break;

            case CIotCommClient::DmxFixtureChannelMappingColorWhite:
                uValue = static_cast<BYTE>(nWhite >> 8);
                break;

            case CIotCommClient::DmxFixtureChannelMappingColorWhite16:
                uValue = static_cast<BYTE>(nWhite);
                break;

            case CIotCommClient::DmxFixtureChannelMappingColorUV:
                uValue = static_cast<BYTE>(nUV >> 8);
                break;

            case CIotCommClient::DmxFixtureChannelMappingColorUV16:
                uValue = static_cast<BYTE>(nUV);
                break;

            case CIotCommClient::DmxFixtureChannelMappingPan:
                uValue = static_cast<BYTE>(nPan >> 8);
                break;

            case CIotCommClient::DmxFixtureChannelMappingPanFine:
                uValue = static_cast<BYTE>(nPan);
                break;

            case CIotCommClient::DmxFixtureChannelMappingTilt:
                uValue = static_cast<BYTE>(nTilt >> 8);
                break;

            case CIotCommClient::DmxFixtureChannelMappingTiltFine:
                uValue = static_cast<BYTE>(nTilt);
                break;

            case CIotCommClient::DmxFixtureChannelMappingStrobe:
                uValue = bStrobe ? 0xFF : 0x00;
                break;

            default:
                uValue = static_cast<BYTE>(nMapping);
                break;
        }

        m_sCurrentState.aUniverse[nOffset++] = uValue;
    }
}

HRESULT CDmxIotDevice::OnManualColorAction(_In_ const ManualColorAction_t &sAction) noexcept
{
    {
        CRwMutex::CAutoExclusiveLock lock(m_cCurrentStateRwMutex);

        switch (sAction.eType)
        {
            case ManualColorActionType_t::SetFixedPaletteColor:
                m_sCurrentState.bFixedIsActive = TRUE;
                m_sCurrentState.uManualRed = sAction.uRed;
                m_sCurrentState.uManualGreen = sAction.uGreen;
                m_sCurrentState.uManualBlue = sAction.uBlue;
                m_sCurrentState.uManualBrightness = 255;
                break;

            case ManualColorActionType_t::IncreaseBrightness:
                if (m_sCurrentState.bFixedIsActive == FALSE)
                {
                    m_sCurrentState.bFixedIsActive = TRUE;
                    m_sCurrentState.uManualRed = 255;
                    m_sCurrentState.uManualGreen = 255;
                    m_sCurrentState.uManualBlue = 255;
                    m_sCurrentState.uManualBrightness = 255;
                }
                else
                {
                    m_sCurrentState.uManualBrightness =
                        (m_sCurrentState.uManualBrightness < 240) ? (m_sCurrentState.uManualBrightness + 16) : 255;
                }
                break;

            case ManualColorActionType_t::DecreaseBrightness:
                if (m_sCurrentState.bFixedIsActive == FALSE)
                {
                    m_sCurrentState.bFixedIsActive = TRUE;
                    m_sCurrentState.uManualRed = 255;
                    m_sCurrentState.uManualGreen = 255;
                    m_sCurrentState.uManualBlue = 255;
                    m_sCurrentState.uManualBrightness = 255;
                }
                else
                {
                    m_sCurrentState.uManualBrightness =
                        (m_sCurrentState.uManualBrightness >= 16) ? (m_sCurrentState.uManualBrightness - 16) : 0;
                }
                break;

            case ManualColorActionType_t::ReturnToAutomaticMode:
                m_sCurrentState.bFixedIsActive = FALSE;
                break;

            default:
                return E_INVALIDARG;
        }
    }

    if (IsIotConnected() != FALSE)
    {
        Process();
    }
    return S_OK;
}

VOID CDmxIotDevice::RenderUniversePreview() noexcept
{
    SIZE_T nResolvedFixturesCount;
    BOOL bFixedIsActive;
    BYTE uRed = 0;
    BYTE uGreen = 0;
    BYTE uBlue = 0;

    {
        CRwMutex::CAutoSharedLock lockFixtures(m_cResolvedFixturesRwMutex);

        nResolvedFixturesCount = m_vecResolvedFixtures.size();
    }

    m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;1H\x1b[97mFixtures:\x1b[0m %u",
                                     static_cast<UINT>(nResolvedFixturesCount));


    {
        CRwMutex::CAutoSharedLock lock(m_cCurrentStateRwMutex);

        bFixedIsActive = m_sCurrentState.bFixedIsActive;
        if (bFixedIsActive != FALSE)
        {
            uRed = (BYTE)((USHORT)m_sCurrentState.uManualRed * (USHORT)m_sCurrentState.uManualBrightness / 255);
            uGreen = (BYTE)((USHORT)m_sCurrentState.uManualGreen * (USHORT)m_sCurrentState.uManualBrightness / 255);
            uBlue = (BYTE)((USHORT)m_sCurrentState.uManualBlue * (USHORT)m_sCurrentState.uManualBrightness / 255);
        }
    }

    if (bFixedIsActive != FALSE)
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;32H\x1b[48;2;%u;%u;%um   \x1b[0m", uRed, uGreen, uBlue);
    }
    else
    {
        m_lpTerminalControl->PrintRegion(GetClientRegionHandle(), L"\x1b[1;32H   ");
    }
}
