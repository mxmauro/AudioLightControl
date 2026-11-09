#pragma once

#include "IotComm\IotCommClient.h"
#include "Utils\Mutex.h"
#include "Utils\Threads.h"
#include "Utils\WindowsEvent.h"
#include <vector>

// -----------------------------------------------------------------------------

class CAudioLightNodeDevice;

class IAudioLightNodeLighting
{
public:
    virtual ~IAudioLightNodeLighting() noexcept = default;

    virtual HRESULT ValidateNodeLightingConfiguration(_In_ const CIotCommClient::LightingOutput_t &sLighting,
                                                      _Out_writes_z_(nReasonLen) LPWSTR szReasonW,
                                                      _In_ SIZE_T nReasonLen) noexcept = 0;
    virtual VOID OnNodeConnectionStateChanged(_In_ BOOL bConnected, _In_ HRESULT hRes) noexcept = 0;
};

class CNodeLightingSubscription
{
public:
    CNodeLightingSubscription() = default;
    CNodeLightingSubscription(_In_ const CNodeLightingSubscription &) = delete;
    CNodeLightingSubscription(_Inout_ CNodeLightingSubscription &&other) noexcept;
    ~CNodeLightingSubscription() noexcept;

    CNodeLightingSubscription &operator=(_In_ const CNodeLightingSubscription &) = delete;
    CNodeLightingSubscription &operator=(_Inout_ CNodeLightingSubscription &&other) noexcept;

    VOID Reset() noexcept;

private:
    CAudioLightNodeDevice *m_lpNode{ nullptr };
    SIZE_T m_nSubscriptionId{ 0 };

    friend class CAudioLightNodeDevice;
};

// -----------------------------------------------------------------------------

class CAudioLightNodeDevice
{
public:
    CAudioLightNodeDevice() noexcept;
    CAudioLightNodeDevice(_In_ const CAudioLightNodeDevice &) = delete;
    CAudioLightNodeDevice(_Inout_ CAudioLightNodeDevice &&) = delete;
    ~CAudioLightNodeDevice() noexcept;

    CAudioLightNodeDevice &operator=(_In_ const CAudioLightNodeDevice &) = delete;
    CAudioLightNodeDevice &operator=(_Inout_ CAudioLightNodeDevice &&) = delete;

    HRESULT SetupCommInfo(_In_z_ LPCWSTR szHostNameW, _In_z_ LPCSTR szUserNameA,
                          _In_ const BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES],
                          _In_ BYTE byLightingCount) noexcept;
    HRESULT Subscribe(_In_ BYTE byLightingSlot, _In_ IAudioLightNodeLighting *lpLighting,
                      _Out_ CNodeLightingSubscription &subscription) noexcept;
    HRESULT Init() noexcept;
    VOID Deinit() noexcept;

    BOOL IsConnected() const noexcept;
    HRESULT SendCommand(_In_ BYTE byLightingSlot, _In_ USHORT nCmd, _In_reads_bytes_(nPayloadLen) LPCVOID lpPayload,
                        _In_ SIZE_T nPayloadLen, _In_ BOOL bReliable) noexcept;
    HRESULT SendRawDmxChannelData(_In_ BYTE byLightingSlot, _In_ USHORT nStartChannel,
                                  _In_reads_bytes_(nDataLen) LPCVOID lpData, _In_ SIZE_T nDataLen) noexcept;
    HRESULT SendRawWs281xRgbData(_In_ BYTE byLightingSlot, _In_ USHORT nFirstLed, _In_ USHORT nLedCount,
                                 _In_reads_bytes_(nRgbDataLen) LPCVOID lpRgbData, _In_ SIZE_T nRgbDataLen) noexcept;

private:
    typedef struct Subscriber_s {
        SIZE_T nId{ 0 };
        BYTE byLightingSlot{ 0 };
        IAudioLightNodeLighting *lpLighting{ nullptr };
    } Subscriber_t;

    VOID Unsubscribe(_In_ SIZE_T nSubscriptionId) noexcept;
    VOID IotThreadProc() noexcept;
    VOID NotifyConnectionState(_In_ BOOL bConnected, _In_ HRESULT hRes) noexcept;
    HRESULT ValidateLightingConfiguration(_In_ const CIotCommClient::LightingConfig_t &sConfig,
                                          _Out_writes_z_(nReasonLen) LPWSTR szReasonW,
                                          _In_ SIZE_T nReasonLen) noexcept;
    VOID OnIotClose(_In_ USHORT nCode, _In_opt_z_ LPCSTR szReasonA, _In_ HRESULT hrInternalReason) noexcept;

private:
    mutable LONG volatile m_lRundownProt{ 0 };
    mutable LONG volatile m_iConnected{ 0 };
    CMutex m_cSubscribersMutex;
    std::vector<Subscriber_t> m_vecSubscribers;
    SIZE_T m_nNextSubscriptionId{ 1 };

    struct {
        CThread cThread;
        CIotCommClient cClient;
        CWindowsEvent cClientClosedEvent;
        LPWSTR szHostNameW{ nullptr };
        LPSTR szUserNameA{ nullptr };
        BYTE aRawPrivateKey[CECDsaKeyPair::RAW_PRIVATE_KEY_BYTES]{};
        BYTE byLightingCount{ 0 };
    } m_sIot{};

    friend class CNodeLightingSubscription;
};
