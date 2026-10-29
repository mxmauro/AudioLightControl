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
public:
    typedef struct _RGB {
        USHORT uRed{ 0 };
        USHORT uGreen{ 0 };
        USHORT uBlue{ 0 };
    } RGB, *LPRGB;

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

    virtual BOOL HasRGBColor() const noexcept
    {
        return FALSE;
    }

    virtual RGB GetRGBColor() const noexcept
    {
        return {};
    }

    virtual BOOL HasRGBStripColors() const noexcept
    {
        return FALSE;
    }

    virtual VOID GetRGBStripColors(_Out_writes_(nColorCount) LPRGB lpsColors, _In_ SIZE_T nColorCount) const noexcept
    {
        UNREFERENCED_PARAMETER(lpsColors);
        UNREFERENCED_PARAMETER(nColorCount);
    }

    virtual BOOL HasWhiteColor() const noexcept
    {
        return FALSE;
    }

    virtual USHORT GetWhiteColor() const noexcept
    {
        return 0;
    }

    virtual BOOL HasUVColor() const noexcept
    {
        return FALSE;
    }

    virtual USHORT GetUVColor() const noexcept
    {
        return 0;
    }

    virtual BOOL HasDimmer() const noexcept
    {
        return FALSE;
    }

    virtual USHORT GetDimmer() const noexcept
    {
        return 0;
    }

    virtual BOOL HasPan() const noexcept
    {
        return FALSE;
    }

    virtual SHORT GetPan() const noexcept
    {
        return 0;
    }

    virtual BOOL HasTilt() const noexcept
    {
        return FALSE;
    }

    virtual SHORT GetTilt() const noexcept
    {
        return 0;
    }

    virtual BOOL HasStrobe() const noexcept
    {
        return FALSE;
    }

    virtual BOOL GetStrobe() const noexcept
    {
        return FALSE;
    }

protected:
    INT GetOutputViewHeight() const noexcept;
    VOID RenderOutputView() noexcept;

    virtual BOOL HasCompactWhiteStrobeOutput() const noexcept
    {
        return FALSE;
    }
};

// -----------------------------------------------------------------------------

class CAudioProcessors
{
public:
    CAudioProcessors() noexcept = default;
    CAudioProcessors(_In_ const CAudioProcessors &) = delete;
    CAudioProcessors(_Inout_ CAudioProcessors &&) = delete;
    ~CAudioProcessors() noexcept = default;

    CAudioProcessors &operator=(_In_ const CAudioProcessors &) = delete;
    CAudioProcessors &operator=(_Inout_ CAudioProcessors &&) = delete;

    HRESULT Add(_In_ const std::shared_ptr<CBaseAudioProcessor> &cAudioProcessor) noexcept;
    CBaseAudioProcessor *FindByType(_In_z_ LPCSTR szTypeA) const noexcept;

    const std::vector<std::shared_ptr<CBaseAudioProcessor>> &All() const noexcept
    {
        return m_vecAll;
    }

    VOID Clear() noexcept;

private:
    std::vector<std::shared_ptr<CBaseAudioProcessor>> m_vecAll;
};
