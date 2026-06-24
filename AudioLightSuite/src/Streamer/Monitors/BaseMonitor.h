#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Audio\AudioAnalysis.h"
#include "TUI\TUiWindow.h"
#include "Utils\PersistantSettings.h"
#include <memory>
#include <vector>

// -----------------------------------------------------------------------------

class CBaseMonitor : public CTUiWindow, public CPersistantSettings
{
protected:
    CBaseMonitor() noexcept = default;
    CBaseMonitor(_In_ const CBaseMonitor &) = delete;
    CBaseMonitor(_Inout_ CBaseMonitor &&) = delete;
public:
    ~CBaseMonitor() noexcept = default;

    CBaseMonitor &operator=(_In_ const CBaseMonitor &) = delete;
    CBaseMonitor &operator=(_Inout_ CBaseMonitor &&) = delete;

    ULONGLONG GetID() const noexcept final;

    virtual LPCSTR GetType() const noexcept = 0;

    virtual VOID FirstRender() noexcept = 0;

    virtual VOID Process(_In_ const CAudioAnalyzer::Frame_t &sFrame) noexcept = 0;

    virtual VOID TimerTick() noexcept;
};

typedef std::vector<std::unique_ptr<CBaseMonitor>> CMonitorList;
