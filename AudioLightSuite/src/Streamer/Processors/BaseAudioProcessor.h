#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Audio\AudioAnalysis.h"
#include "TUI\TUiWindow.h"
#include "Utils\PersistantSettings.h"
#include <memory>
#include <vector>

// -----------------------------------------------------------------------------

class CBaseAudioProcessor : public CTUiWindow, public CPersistantSettings
{
protected:
    CBaseAudioProcessor() noexcept = default;
    CBaseAudioProcessor(_In_ const CBaseAudioProcessor &) = delete;
    CBaseAudioProcessor(_Inout_ CBaseAudioProcessor &&) = delete;
public:
    ~CBaseAudioProcessor() noexcept = default;

    CBaseAudioProcessor &operator=(_In_ const CBaseAudioProcessor &) = delete;
    CBaseAudioProcessor &operator=(_Inout_ CBaseAudioProcessor &&) = delete;

    ULONGLONG GetID() const noexcept final;

    virtual LPCSTR GetType() const noexcept = 0;

    virtual VOID FirstRender() noexcept = 0;

    virtual VOID Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept = 0;

    virtual VOID TimerTick() noexcept;
};

typedef std::vector<std::unique_ptr<CBaseAudioProcessor>> CAudioProcessorList;
