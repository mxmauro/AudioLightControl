#include "CommandLine.h"
#include "FeatureExtractor.h"
#include "Audio\AudioAnalysis.h"
#include "Utils\Exception2HR.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl\client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdio.h>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "propsys.lib")

#define HRESULT_FROM_LASTERROR() HRESULT_FROM_WIN32(::GetLastError())
#define THROW_IF_FAILED_HR(hr) if (FAILED((hr))) { throw (HRESULT)(hr); }

// -----------------------------------------------------------------------------

using Microsoft::WRL::ComPtr;

static constexpr UINT32 FEATURE_ANALYSIS_RATE_MS = 100;
static constexpr UINT32 FEATURE_FILE_FORMAT_VERSION = 2;
static constexpr UINT32 FEATURE_MAX_BANDS = CAudioAnalyzer::BAND_COUNT;

typedef struct FeatureDecodedAudio_s {
    UINT32 ulSampleRate{ 0 };
    std::vector<float> vecMonoPcm;
} FeatureDecodedAudio_t;

typedef struct FeatureFrame_s {
    float fBands[FEATURE_MAX_BANDS]{};
    float fBandOnsets[FEATURE_MAX_BANDS]{};
    float fLowEnergy{ 0.0f };
    float fMidEnergy{ 0.0f };
    float fHighEnergy{ 0.0f };
    float fOverallEnergy{ 0.0f };
    float fNovelty{ 0.0f };
} FeatureFrame_t;

typedef struct FeatureBeatMarker_s {
    UINT32 ulFrameIndex{ 0 };
    BOOL bDownbeat{ FALSE };
} FeatureBeatMarker_t;

typedef enum FeatureSectionType_e {
    FeatureSectionTypeUnknown = 0,
    FeatureSectionTypeIntro = 1,
    FeatureSectionTypeBuild = 2,
    FeatureSectionTypeHighEnergy = 3,
    FeatureSectionTypeBreakdown = 4,
    FeatureSectionTypeOutro = 5
} FeatureSectionType_t;

typedef struct FeatureSectionMarker_s {
    UINT32 ulStartFrame{ 0 };
    UINT32 ulEndFrame{ 0 };
    FeatureSectionType_t eType{ FeatureSectionTypeUnknown };
    float fMeanEnergy{ 0.0f };
} FeatureSectionMarker_t;

typedef struct FeatureAnalysisResults_s {
    UINT32 ulCadenceMs{ FEATURE_ANALYSIS_RATE_MS };
    std::vector<FeatureFrame_t> vecFrames;
    std::vector<FeatureBeatMarker_t> vecBeats;
    std::vector<FeatureSectionMarker_t> vecSections;
} FeatureAnalysisResults_t;

constexpr char FEATURE_FILE_MAGIC[8] = { 'A', 'L', 'C', 'F', 'E', 'A', 'T', '\0' };

#pragma pack(push, 1)
typedef struct FeatureFileHeader_s {
    CHAR szMagic[8];
    UINT32 ulVersion;
    UINT32 ulCadenceMs;
    UINT32 ulSourceSampleRate;
    UINT32 ulBandCount;
    UINT32 ulFrameCount;
    UINT32 ulBeatCount;
    UINT32 ulSectionCount;
} FeatureFileHeader_t;

typedef struct FeatureFileBand_s {
    UINT32 ulGroup;
    float fCenterHz;
    float fWidthHz;
} FeatureFileBand_t;

typedef struct FeatureFileFrame_s {
    UINT32 ulFrameIndex;
    float fBands[FEATURE_MAX_BANDS];
    float fBandOnsets[FEATURE_MAX_BANDS];
    float fLowEnergy;
    float fMidEnergy;
    float fHighEnergy;
    float fOverallEnergy;
    float fNovelty;
} FeatureFileFrame_t;

typedef struct FeatureFileBeat_s {
    UINT32 ulFrameIndex;
    UINT32 ulFlags;
} FeatureFileBeat_t;

typedef struct FeatureFileSection_s {
    UINT32 ulStartFrame;
    UINT32 ulEndFrame;
    UINT32 ulType;
    float fMeanEnergy;
} FeatureFileSection_t;
#pragma pack(pop)

// -----------------------------------------------------------------------------

static float ConvertPcmSampleToFloat(_In_ const BYTE *lpData, _In_ UINT32 ulBitsPerSample, _In_ BOOL bFloat) noexcept;
static HRESULT ReadContiguousAudioBuffer(_In_ IMFSample *lpSample, _Out_ std::vector<BYTE> &vecBytes) noexcept;
static VOID NormalizeFrames(_Inout_ std::vector<FeatureFrame_t> &vecFrames) noexcept;
static VOID DetectBeats(_In_ const std::vector<FeatureFrame_t> &vecFrames, _In_ UINT32 ulCadenceHz,
                        _Out_ std::vector<FeatureBeatMarker_t> &vecBeats) noexcept;
static VOID DetectSections(_In_ const std::vector<FeatureFrame_t> &vecFrames, _In_ UINT32 ulCadenceHz,
                           _Out_ std::vector<FeatureSectionMarker_t> &vecSections) noexcept;
static HRESULT DecodeToMonoPCM(_In_z_ LPCWSTR szInputFilePathW, _Out_ FeatureDecodedAudio_t &sDecodedAudio) noexcept;
static HRESULT Analyze(_In_ const FeatureDecodedAudio_t &sDecodedAudio, _Out_ FeatureAnalysisResults_t &sAnalysisResults) noexcept;
static HRESULT BuildOutputPath(_In_z_ LPCWSTR szInputFilePathW, _Out_ std::wstring &strOutputFilePathW) noexcept;
static HRESULT WriteFeatureFile(_In_z_ LPCWSTR szOutputFilePathW, _In_ const FeatureDecodedAudio_t &sDecodedAudio,
                                _In_ const FeatureAnalysisResults_t &sAnalysisResults) noexcept;

// -----------------------------------------------------------------------------

HRESULT ExtractFeatures() noexcept
{
    FeatureDecodedAudio_t sDecodedAudio;
    FeatureAnalysisResults_t sAnalysisResults;
    std::wstring strOutputFilePathW;
    const CmdLimeParameters &sCmdLineParams = GetCmdLimeParameters();
    HRESULT hRes;

    if (!sCmdLineParams.strOutputFilePathW.empty())
    {
        strOutputFilePathW = sCmdLineParams.strOutputFilePathW;
    }
    else
    {
        hRes = BuildOutputPath(sCmdLineParams.strInputFilePathW.c_str(), strOutputFilePathW);
        if (FAILED(hRes))
        {
            fwprintf(stderr, L"Error 0x%08X: Unable to build output file path.\n", (unsigned int)hRes);
            return hRes;
        }
    }

    hRes = DecodeToMonoPCM(sCmdLineParams.strInputFilePathW.c_str(), sDecodedAudio);
    if (FAILED(hRes))
    {
        if (hRes == HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE))
        {
            fwprintf(stderr, L"Error 0x%08X: Unsupported input audio media format.\n", (unsigned int)hRes);
        }
        else
        {
            fwprintf(stderr, L"Error 0x%08X: Unable to decode input audio file.\n", (unsigned int)hRes);
        }
        return hRes;
    }
    if (sDecodedAudio.vecMonoPcm.empty())
    {
        fwprintf(stderr, L"Error 0x%08X: No audio data found in the input file.\n", (unsigned int)HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    hRes = Analyze(sDecodedAudio, sAnalysisResults);
    if (FAILED(hRes))
    {
        fwprintf(stderr, L"Error 0x%08X: Unable to analyze decoded audio.\n", (unsigned int)hRes);
        return hRes;
    }

    hRes = WriteFeatureFile(strOutputFilePathW.c_str(), sDecodedAudio, sAnalysisResults);
    if (FAILED(hRes))
    {
        fwprintf(stderr, L"Error 0x%08X: Unable to write feature file.\n", (unsigned int)hRes);
        return hRes;
    }

    // Done
    fwprintf(stdout, L"Feature file generated successfully.\n");
    return S_OK;
}

static HRESULT DecodeToMonoPCM(_In_z_ LPCWSTR szInputFilePathW, _Out_ FeatureDecodedAudio_t &sDecodedAudio) noexcept
{
    ComPtr<IMFSourceReader> cReader;
    ComPtr<IMFMediaType> cMediaType;
    ComPtr<IMFMediaType> cCurrentType;
    PWAVEFORMATEX lpWaveFormatEx = nullptr;
    GUID guidSubtype;
    BOOL bFloatSamples;
    SIZE_T nFrameSize;
    SIZE_T nSourceChannels;
    HRESULT hRes;

    // Decode through Media Foundation so the tool stays Windows-native and dependency-light.
    hRes = MFCreateSourceReaderFromURL(szInputFilePathW, nullptr, &cReader);
    if (FAILED(hRes))
    {
        return hRes;
    }

    hRes = ::MFCreateMediaType(&cMediaType);
    if (SUCCEEDED(hRes))
    {
        hRes = cMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hRes))
        {
            hRes = cMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
            if (SUCCEEDED(hRes))
            {
                hRes = cMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
                if (SUCCEEDED(hRes))
                {
                    hRes = cReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, cMediaType.Get());
                }
            }
        }
    }
    if (FAILED(hRes))
    {
        cMediaType.Reset();

        hRes = MFCreateMediaType(&cMediaType);
        if (SUCCEEDED(hRes))
        {
            hRes = cMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hRes))
            {
                hRes = cMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                if (SUCCEEDED(hRes))
                {
                    hRes = cMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                    if (SUCCEEDED(hRes))
                    {
                        hRes = cReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, cMediaType.Get());
                    }
                }
            }
        }
        if (FAILED(hRes))
        {
            return hRes;
        }
    }
    hRes = cReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cCurrentType);
    if (FAILED(hRes))
    {
        return hRes;
    }
    hRes = ::MFCreateWaveFormatExFromMFMediaType(cCurrentType.Get(), &lpWaveFormatEx, nullptr);
    if (FAILED(hRes))
    {
        return hRes;
    }
    hRes = cCurrentType->GetGUID(MF_MT_SUBTYPE, &guidSubtype);
    if (FAILED(hRes))
    {
        ::CoTaskMemFree(lpWaveFormatEx);
        return hRes;
    }
    if ((lpWaveFormatEx->wFormatTag != WAVE_FORMAT_PCM && lpWaveFormatEx->wFormatTag != WAVE_FORMAT_IEEE_FLOAT &&
         lpWaveFormatEx->wFormatTag != WAVE_FORMAT_EXTENSIBLE) ||
        lpWaveFormatEx->nBlockAlign == 0 || lpWaveFormatEx->nSamplesPerSec == 0 || lpWaveFormatEx->nChannels == 0)
    {
        ::CoTaskMemFree(lpWaveFormatEx);
        return HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
    }
    if (lpWaveFormatEx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        PWAVEFORMATEXTENSIBLE lpWaveFormatExt = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(lpWaveFormatEx);

        if (memcmp(&lpWaveFormatExt->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM, sizeof(GUID)) != 0 &&
            memcmp(&lpWaveFormatExt->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof(GUID)) != 0)
        {
            ::CoTaskMemFree(lpWaveFormatEx);
            return HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
        }
    }

    sDecodedAudio = {};
    sDecodedAudio.ulSampleRate = lpWaveFormatEx->nSamplesPerSec;
    nSourceChannels = static_cast<SIZE_T>(lpWaveFormatEx->nChannels);
    nFrameSize = static_cast<SIZE_T>(lpWaveFormatEx->nBlockAlign);
    bFloatSamples = (guidSubtype == MFAudioFormat_Float) ? TRUE : FALSE;

    hRes = TranslateToHresult([&]()
    {
        std::vector<BYTE> vecBytes;

        while (true)
        {
            ComPtr<IMFSample> cSample;
            DWORD dwFlags;

            THROW_IF_FAILED_HR(cReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &dwFlags, nullptr, &cSample));
            if ((dwFlags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            {
                break;
            }
            if (!cSample)
            {
                continue;
            }

            THROW_IF_FAILED_HR(ReadContiguousAudioBuffer(cSample.Get(), vecBytes));
            if (vecBytes.empty())
            {
                continue;
            }
            if ((vecBytes.size() % nFrameSize) != 0)
            {
                throw (HRESULT)E_UNEXPECTED;
            }

            const UINT32 ulFramesCount = static_cast<UINT32>(vecBytes.size() / nFrameSize);
            const UINT32 ulBitsPerSample = lpWaveFormatEx->wBitsPerSample;
            const BYTE *lpFrameData = vecBytes.data();

            // Downmix to mono here so every later stage works from a single deterministic stream.
            for (UINT32 i = 0; i < ulFramesCount; i++, lpFrameData += nFrameSize)
            {
                float fMono = 0.0f;
                const BYTE *lpChannelData = lpFrameData;

                for (SIZE_T nChannel = 0; nChannel < nSourceChannels; nChannel++, lpChannelData += ulBitsPerSample / 8)
                {
                    fMono += ConvertPcmSampleToFloat(lpChannelData, ulBitsPerSample, bFloatSamples);
                }
                fMono /= static_cast<float>(nSourceChannels);
                sDecodedAudio.vecMonoPcm.push_back(fMono);
            }
        }
    });

    // Done
    ::CoTaskMemFree(lpWaveFormatEx);
    return hRes;
}

static HRESULT Analyze(_In_ const FeatureDecodedAudio_t &sDecodedAudio, _Out_ FeatureAnalysisResults_t &sAnalysisResults) noexcept
{
    CAudioAnalyzer cAudioAnalyzer;
    SIZE_T nSamplesPerAnalysisFrame;
    SIZE_T nAnalysisFrameCount;
    SIZE_T nAnalysisFrameIndex;
    SIZE_T nSampleOffset;
    HRESULT hRes;

    sAnalysisResults = {};
    sAnalysisResults.ulCadenceMs = FEATURE_ANALYSIS_RATE_MS;

    cAudioAnalyzer.SetFrequency((float)sDecodedAudio.ulSampleRate);

    // Slice the full song into fixed-rate analysis frames so playback and devices can consume
    // a stable time grid later, regardless of callback sizes in the live app.
    nSamplesPerAnalysisFrame = (static_cast<SIZE_T>(sDecodedAudio.ulSampleRate) * FEATURE_ANALYSIS_RATE_MS) / 1000U;
    if (nSamplesPerAnalysisFrame < 1)
    {
        nSamplesPerAnalysisFrame = 1;
    }
    nAnalysisFrameCount = (sDecodedAudio.vecMonoPcm.size() + nSamplesPerAnalysisFrame - 1) / nSamplesPerAnalysisFrame;
    if (nAnalysisFrameCount < 1)
    {
        nAnalysisFrameCount = 1;
    }
    hRes = TranslateToHresult([&]()
    {
        sAnalysisResults.vecFrames.resize(nAnalysisFrameCount);
    });
    if (FAILED(hRes))
    {
        return hRes;
    }

    nAnalysisFrameIndex = 0;
    for (nSampleOffset = 0; nSampleOffset < sDecodedAudio.vecMonoPcm.size(); nSampleOffset += nSamplesPerAnalysisFrame)
    {
        SIZE_T nCurrentFrameSampleCount = sDecodedAudio.vecMonoPcm.size() - nSampleOffset;
        if (nCurrentFrameSampleCount > nSamplesPerAnalysisFrame)
        {
            nCurrentFrameSampleCount = nSamplesPerAnalysisFrame;
        }
        CAudioAnalyzer::Frame_t sAnalysisResult;
        FeatureFrame_t &sFrame = sAnalysisResults.vecFrames[nAnalysisFrameIndex];

        sFrame = {};
        cAudioAnalyzer.Analyze(sDecodedAudio.vecMonoPcm.data() + nSampleOffset, static_cast<UINT32>(nCurrentFrameSampleCount),
                               sAnalysisResult);
        memcpy(sFrame.fBands, sAnalysisResult.fBandRmsRaw, sizeof(sFrame.fBands));
        sFrame.fLowEnergy = sAnalysisResult.fLowEnergyRaw;
        sFrame.fMidEnergy = sAnalysisResult.fMidEnergyRaw;
        sFrame.fHighEnergy = sAnalysisResult.fHighEnergyRaw;
        sFrame.fOverallEnergy = (sFrame.fLowEnergy * 0.45f) + (sFrame.fMidEnergy * 0.30f) + (sFrame.fHighEnergy * 0.25f);

        nAnalysisFrameIndex++;
    }

    // Normalize after the full pass so each song gets a stable internal range while still leaving
    // room for later device-side adaptive gain if needed.
    NormalizeFrames(sAnalysisResults.vecFrames);

    // Derive simple song-level structure from the normalized frame stream.
    DetectBeats(sAnalysisResults.vecFrames, sAnalysisResults.ulCadenceMs, sAnalysisResults.vecBeats);
    DetectSections(sAnalysisResults.vecFrames, sAnalysisResults.ulCadenceMs, sAnalysisResults.vecSections);

    // Done
    return S_OK;
}

static HRESULT BuildOutputPath(_In_z_ LPCWSTR szInputFilePathW, _Out_ std::wstring &strOutputFilePathW) noexcept
{
    return TranslateToHresult([&]()
    {
        std::filesystem::path pathInput(szInputFilePathW);
        pathInput.replace_extension(L".alcfeat");
        strOutputFilePathW = pathInput.native();
    });
}

static HRESULT WriteFeatureFile(_In_z_ LPCWSTR szOutputFilePathW, _In_ const FeatureDecodedAudio_t &sDecodedAudio,
                                _In_ const FeatureAnalysisResults_t &sAnalysisResults) noexcept
{
    const CAudioAnalyzer::BandDefinition_t *lpsBandDefinitions = CAudioAnalyzer::GetBandDefinitions();

    return TranslateToHresult([&]()
    {
        FeatureFileHeader_t sHeader{};
        std::vector<FeatureFileBand_t> vecBands;
        std::vector<FeatureFileFrame_t> vecFrames;
        std::vector<FeatureFileBeat_t> vecBeats;
        std::vector<FeatureFileSection_t> vecSections;
        std::ofstream cFile(std::filesystem::path(szOutputFilePathW), std::ios::binary | std::ios::trunc);

        if (!cFile.is_open())
        {
            throw (HRESULT)HRESULT_FROM_LASTERROR();
        }

        // Write a compact, versioned sidecar that can be consumed later without rescanning audio.
        memcpy(sHeader.szMagic, FEATURE_FILE_MAGIC, sizeof(sHeader.szMagic));
        sHeader.ulVersion = FEATURE_FILE_FORMAT_VERSION;
        sHeader.ulCadenceMs = sAnalysisResults.ulCadenceMs;
        sHeader.ulSourceSampleRate = sDecodedAudio.ulSampleRate;
        sHeader.ulBandCount = CAudioAnalyzer::BAND_COUNT;
        sHeader.ulFrameCount = static_cast<UINT32>(sAnalysisResults.vecFrames.size());
        sHeader.ulBeatCount = static_cast<UINT32>(sAnalysisResults.vecBeats.size());
        sHeader.ulSectionCount = static_cast<UINT32>(sAnalysisResults.vecSections.size());

        vecBands.resize(CAudioAnalyzer::BAND_COUNT);
        for (UINT32 i = 0; i < CAudioAnalyzer::BAND_COUNT; i++)
        {
            vecBands[i].ulGroup = static_cast<UINT32>(lpsBandDefinitions[i].eGroup);
            vecBands[i].fCenterHz = lpsBandDefinitions[i].fCenterHz;
            vecBands[i].fWidthHz = lpsBandDefinitions[i].fWidthHz;
        }

        vecFrames.resize(sAnalysisResults.vecFrames.size());
        for (SIZE_T i = 0; i < sAnalysisResults.vecFrames.size(); i++)
        {
            vecFrames[i].ulFrameIndex = static_cast<UINT32>(i);
            memcpy(vecFrames[i].fBands, sAnalysisResults.vecFrames[i].fBands, sizeof(vecFrames[i].fBands));
            memcpy(vecFrames[i].fBandOnsets, sAnalysisResults.vecFrames[i].fBandOnsets, sizeof(vecFrames[i].fBandOnsets));
            vecFrames[i].fLowEnergy = sAnalysisResults.vecFrames[i].fLowEnergy;
            vecFrames[i].fMidEnergy = sAnalysisResults.vecFrames[i].fMidEnergy;
            vecFrames[i].fHighEnergy = sAnalysisResults.vecFrames[i].fHighEnergy;
            vecFrames[i].fOverallEnergy = sAnalysisResults.vecFrames[i].fOverallEnergy;
            vecFrames[i].fNovelty = sAnalysisResults.vecFrames[i].fNovelty;
        }

        // Keep beat and section tables separate from per-frame data so later playback code can
        // jump between macro structure and dense frame data efficiently.
        vecBeats.resize(sAnalysisResults.vecBeats.size());
        for (SIZE_T i = 0; i < sAnalysisResults.vecBeats.size(); i++)
        {
            vecBeats[i].ulFrameIndex = sAnalysisResults.vecBeats[i].ulFrameIndex;
            vecBeats[i].ulFlags = sAnalysisResults.vecBeats[i].bDownbeat ? 0x00000003U : 0x00000001U;
        }

        vecSections.resize(sAnalysisResults.vecSections.size());
        for (SIZE_T i = 0; i < sAnalysisResults.vecSections.size(); i++)
        {
            vecSections[i].ulStartFrame = sAnalysisResults.vecSections[i].ulStartFrame;
            vecSections[i].ulEndFrame = sAnalysisResults.vecSections[i].ulEndFrame;
            vecSections[i].ulType = static_cast<UINT32>(sAnalysisResults.vecSections[i].eType);
            vecSections[i].fMeanEnergy = sAnalysisResults.vecSections[i].fMeanEnergy;
        }

        cFile.write(reinterpret_cast<const char *>(&sHeader), sizeof(sHeader));
        cFile.write(reinterpret_cast<const char *>(vecBands.data()), (std::streamsize)(vecBands.size() * sizeof(vecBands[0])));
        cFile.write(reinterpret_cast<const char *>(vecFrames.data()), (std::streamsize)(vecFrames.size() * sizeof(vecFrames[0])));
        cFile.write(reinterpret_cast<const char *>(vecBeats.data()), (std::streamsize)(vecBeats.size() * sizeof(vecBeats[0])));
        cFile.write(reinterpret_cast<const char *>(vecSections.data()), (std::streamsize)(vecSections.size() * sizeof(vecSections[0])));

        if (!cFile.good())
        {
            throw HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        }
    });
}

static float ConvertPcmSampleToFloat(_In_ const BYTE *lpData, _In_ UINT32 ulBitsPerSample, _In_ BOOL bFloat) noexcept
{
    if (bFloat)
    {
        return *reinterpret_cast<const float *>(lpData);
    }

    switch (ulBitsPerSample)
    {
        case 8:
            return ((float)(*reinterpret_cast<const BYTE *>(lpData)) - 128.0f) / 128.0f;

        case 16:
            return (float)(*reinterpret_cast<const INT16 *>(lpData)) / 32768.0f;

        case 24:
            {
                INT32 nValue = ((INT32)lpData[0]) | ((INT32)lpData[1] << 8) | ((INT32)lpData[2] << 16);
                if ((nValue & 0x00800000) != 0)
                {
                    nValue |= ~0x00FFFFFF;
                }
                return (float)nValue / 8388608.0f;
            }

        case 32:
            return (float)(*reinterpret_cast<const INT32 *>(lpData)) / 2147483648.0f;
    }

    return 0.0f;
}

static HRESULT ReadContiguousAudioBuffer(_In_ IMFSample *lpSample, _Out_ std::vector<BYTE> &vecBytes) noexcept
{
    ComPtr<IMFMediaBuffer> cMediaBuffer;
    BYTE *lpData;
    DWORD dwMaxLen, dwCurrentLen;
    HRESULT hRes;

    vecBytes.clear();

    hRes = lpSample->ConvertToContiguousBuffer(&cMediaBuffer);
    if (FAILED(hRes))
    {
        return hRes;
    }

    hRes = cMediaBuffer->Lock(&lpData, &dwMaxLen, &dwCurrentLen);
    if (SUCCEEDED(hRes))
    {
        hRes = TranslateToHresult([&]()
        {
            vecBytes.resize(dwCurrentLen);
            if (dwCurrentLen > 0)
            {
                memcpy(vecBytes.data(), lpData, dwCurrentLen);
            }
        });

        cMediaBuffer->Unlock();
    }

    // Done
    return hRes;
}

static VOID NormalizeFrames(_Inout_ std::vector<FeatureFrame_t> &vecFrames) noexcept
{
    std::array<float, FEATURE_MAX_BANDS> aBandMax{};
    float fLowMax, fMidMax, fHighMax, fOverallMax;
    FeatureFrame_t sPrevFrame;

    fLowMax = MATH_EPSILON;
    fMidMax = MATH_EPSILON;
    fHighMax = MATH_EPSILON;
    fOverallMax = MATH_EPSILON;
    sPrevFrame = {};

    // First find song-wide maxima per band/group. This gives a stable per-song normalization
    // baseline similar in spirit to the live historical RMS adaptation.
    for (const auto &sFrame : vecFrames)
    {
        for (UINT32 i = 0; i < FEATURE_MAX_BANDS; i++)
        {
            if (sFrame.fBands[i] > aBandMax[i])
            {
                aBandMax[i] = sFrame.fBands[i];
            }
        }
        fLowMax = MathHelpers::Max(fLowMax, sFrame.fLowEnergy);
        fMidMax = MathHelpers::Max(fMidMax, sFrame.fMidEnergy);
        fHighMax = MathHelpers::Max(fHighMax, sFrame.fHighEnergy);
        fOverallMax = MathHelpers::Max(fOverallMax, sFrame.fOverallEnergy);
    }

    // Then compute per-frame normalized levels and positive deltas, which act as lightweight
    // onset detectors for later beat/cue derivation.
    for (auto &sFrame : vecFrames)
    {
        float fNoveltyAccum = 0.0f;

        for (UINT32 i = 0; i < FEATURE_MAX_BANDS; i++)
        {
            sFrame.fBands[i] /= MathHelpers::Max(aBandMax[i], MATH_EPSILON);
            sFrame.fBandOnsets[i] = MathHelpers::Max(0.0f, sFrame.fBands[i] - sPrevFrame.fBands[i]);
            fNoveltyAccum += sFrame.fBandOnsets[i];
        }

        sFrame.fLowEnergy /= fLowMax;
        sFrame.fMidEnergy /= fMidMax;
        sFrame.fHighEnergy /= fHighMax;
        sFrame.fOverallEnergy /= fOverallMax;
        sFrame.fNovelty = MathHelpers::Clamp(fNoveltyAccum / (float)FEATURE_MAX_BANDS);
        sPrevFrame = sFrame;
    }
}

static VOID DetectBeats(_In_ const std::vector<FeatureFrame_t> &vecFrames, _In_ UINT32 ulCadenceMs,
                        _Out_ std::vector<FeatureBeatMarker_t> &vecBeats) noexcept
{
    float fBestScore;
    UINT32 ulBestBpm, ulMinGapFrames, ulBeatCounter;

    vecBeats.clear();

    // This is intentionally simple: estimate a plausible BPM by scoring repeating low/onset
    // patterns, then extract sparse beat markers from local peaks.
    fBestScore = -1.0f;
    ulBestBpm = 120;
    for (UINT32 ulBpm = 70; ulBpm <= 180; ulBpm++)
    {
        float fFramesPerBeat = 60000.0f / ((float)ulBpm * (float)ulCadenceMs);
        float fScore = 0.0f;

        for (SIZE_T i = 0; i < vecFrames.size(); i++)
        {
            float fPhase = fmodf((float)i, fFramesPerBeat) / fFramesPerBeat;
            float fWeight = 1.0f - (2.0f * fabsf(fPhase - 0.5f));
            float fBeatEnergy = (vecFrames[i].fBandOnsets[0] * 0.30f) + (vecFrames[i].fBandOnsets[1] * 0.25f) +
                                (vecFrames[i].fBandOnsets[11] * 0.15f) + (vecFrames[i].fOverallEnergy * 0.30f);

            fScore += fBeatEnergy * MathHelpers::Max(0.0f, fWeight);
        }

        if (fScore > fBestScore)
        {
            fBestScore = fScore;
            ulBestBpm = ulBpm;
        }
    }

    ulMinGapFrames = static_cast<UINT32>(30000.0f / ((float)ulBestBpm * (float)ulCadenceMs));
    if (ulMinGapFrames < 1)
    {
        ulMinGapFrames = 1;
    }
    ulBeatCounter = 0;
    for (UINT32 i = 2; i + 2 < vecFrames.size(); i++)
    {
        float fBeatStrength = (vecFrames[i].fBandOnsets[0] * 0.35f) + (vecFrames[i].fBandOnsets[1] * 0.25f) +
                              (vecFrames[i].fBandOnsets[11] * 0.15f) + (vecFrames[i].fNovelty * 0.25f);

        if (fBeatStrength < 0.18f)
        {
            continue;
        }
        if (fBeatStrength < vecFrames[i - 1].fNovelty || fBeatStrength < vecFrames[i + 1].fNovelty)
        {
            continue;
        }
        if (!vecBeats.empty() && i - vecBeats.back().ulFrameIndex < ulMinGapFrames)
        {
            continue;
        }

        FeatureBeatMarker_t sBeat;
        sBeat.ulFrameIndex = i;
        sBeat.bDownbeat = ((ulBeatCounter % 4U) == 0U) ? TRUE : FALSE;
        vecBeats.push_back(sBeat);
        ulBeatCounter++;
    }
}

static VOID DetectSections(_In_ const std::vector<FeatureFrame_t> &vecFrames, _In_ UINT32 ulCadenceMs,
                           _Out_ std::vector<FeatureSectionMarker_t> &vecSections) noexcept
{
    SIZE_T nWindowFrames, nStartFrame;

    vecSections.clear();

    // Sectioning is coarse on purpose for v1: split the song into larger windows and classify
    // them from average energy/novelty rather than trying to infer exact musical semantics.
    nWindowFrames = 8000 / static_cast<SIZE_T>(ulCadenceMs);
    if (nWindowFrames < 1)
    {
        nWindowFrames = 1;
    }
    nStartFrame = 0;

    while (nStartFrame < vecFrames.size())
    {
        SIZE_T nEndFrame = vecFrames.size();
        if (nEndFrame > nStartFrame + nWindowFrames)
        {
            nEndFrame = nStartFrame + nWindowFrames;
        }
        double fEnergySum = 0.0;
        double fNoveltySum = 0.0;
        FeatureSectionMarker_t sSection;

        for (SIZE_T i = nStartFrame; i < nEndFrame; i++)
        {
            fEnergySum += vecFrames[i].fOverallEnergy;
            fNoveltySum += vecFrames[i].fNovelty;
        }

        double fFramesCount = (double)(nEndFrame - nStartFrame);
        if (fFramesCount < 1.0)
        {
            fFramesCount = 1.0;
        }
        sSection.ulStartFrame = static_cast<UINT32>(nStartFrame);
        sSection.ulEndFrame = static_cast<UINT32>(nEndFrame - 1);
        sSection.fMeanEnergy = (float)(fEnergySum / fFramesCount);

        const float fMeanNovelty = (float)(fNoveltySum / fFramesCount);
        if (nStartFrame == 0 && sSection.fMeanEnergy < 0.35f)
        {
            sSection.eType = FeatureSectionTypeIntro;
        }
        else if (nEndFrame >= vecFrames.size() && sSection.fMeanEnergy < 0.35f)
        {
            sSection.eType = FeatureSectionTypeOutro;
        }
        else if (sSection.fMeanEnergy > 0.65f)
        {
            sSection.eType = FeatureSectionTypeHighEnergy;
        }
        else if (fMeanNovelty > 0.18f)
        {
            sSection.eType = FeatureSectionTypeBuild;
        }
        else if (sSection.fMeanEnergy < 0.30f)
        {
            sSection.eType = FeatureSectionTypeBreakdown;
        }
        else
        {
            sSection.eType = FeatureSectionTypeUnknown;
        }

        vecSections.push_back(sSection);
        nStartFrame = nEndFrame;
    }
}
