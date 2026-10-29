#include "GoveeBLEDevice.h"
#include <bcrypt.h>
#include <algorithm>
#include <deque>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

// -----------------------------------------------------------------------------

static constexpr BYTE GOVEE_HANDSHAKE = 0xe7;
static constexpr BYTE GOVEE_REQUEST_SESSION_KEY = 0x01;
static constexpr BYTE GOVEE_CONFIRM_SESSION_KEY = 0x02;
static constexpr BYTE GOVEE_GCM_HANDSHAKE = 0x11;
static constexpr BYTE GOVEE_GCM_SUCCESS = 0x00;
static constexpr SIZE_T GOVEE_FRAME_LENGTH = 20;
static constexpr SIZE_T GOVEE_KEY_LENGTH = 16;
static constexpr SIZE_T GOVEE_GCM_IV_LENGTH = 12;
static constexpr SIZE_T GOVEE_GCM_IV_BASE_LENGTH = 8;
static constexpr SIZE_T GOVEE_GCM_TAG_LENGTH = 12;
static constexpr SIZE_T GOVEE_GCM_DEVICE_INFO_LENGTH = 11;
static constexpr SIZE_T GOVEE_GCM_REQUEST_LENGTH = 36;
static constexpr SIZE_T GOVEE_GCM_REPLY_LENGTH = 46;

static BYTE const GOVEE_PSK[GOVEE_KEY_LENGTH]{ 'M', 'a', 'k', 'i', 'n', 'g', 'L', 'i', 'f', 'e', 'S', 'm', 'a', 'r', 't', 'e' };
static BYTE const GOVEE_GCM_HANDSHAKE_KEY[GOVEE_KEY_LENGTH]{
    0xfc, 0x03, 0x78, 0x3c, 0x7c, 0x42, 0xcb, 0x83, 0xe2, 0x02, 0xa1, 0x64, 0x36, 0x48, 0xaf, 0xf6,
};
static BYTE const GOVEE_GCM_DEVICE_KEY[GOVEE_KEY_LENGTH]{
    0xae, 0x02, 0x8b, 0x63, 0x0b, 0xae, 0x6e, 0xcc, 0x4b, 0xff, 0x1b, 0x24, 0x9e, 0x22, 0xf9, 0x55,
};

// -----------------------------------------------------------------------------

static BYTE CalculateChecksum(_In_reads_bytes_(nDataLen) BYTE const* lpData, _In_ SIZE_T nDataLen) noexcept;
static BOOL IsValidFrame(_In_ std::vector<BYTE> const& vecFrame) noexcept;
static void ClearCngKey(_Inout_ BCRYPT_KEY_HANDLE& hKey, _Inout_ std::vector<BYTE>& vecObject) noexcept;
static HRESULT CreateSymmetricKey(_In_ BCRYPT_ALG_HANDLE hAlgorithm, _In_reads_bytes_(cbKey) BYTE const* lpKey,
                                  _In_ ULONG cbKey, _Inout_ BCRYPT_KEY_HANDLE& hKey,
                                  _Inout_ std::vector<BYTE>& vecObject) noexcept;
static HRESULT AesEcbTransform(_In_ BCRYPT_KEY_HANDLE hKey, _In_reads_bytes_(GOVEE_KEY_LENGTH) BYTE const* lpInput,
                               _Out_writes_bytes_(GOVEE_KEY_LENGTH) BYTE* lpOutput, _In_ BOOL bEncrypt) noexcept;
static void Rc4Transform(_In_reads_(GOVEE_KEY_LENGTH) BYTE const byKey[GOVEE_KEY_LENGTH],
                         _In_reads_bytes_(nDataLen) BYTE const* lpInput, _Out_writes_bytes_(nDataLen) BYTE* lpOutput,
                         _In_ SIZE_T nDataLen) noexcept;
static HRESULT TransformFrame(_In_ std::vector<BYTE> const& vecInput, _In_ BCRYPT_KEY_HANDLE hEcbKey,
                              _In_reads_(GOVEE_KEY_LENGTH) BYTE const byRc4Key[GOVEE_KEY_LENGTH],
                              _Out_ std::vector<BYTE>& vecOutput, _In_ BOOL bEncrypt) noexcept;
static HRESULT BuildHandshakeFrame(_In_ BYTE byStep, _Out_ std::vector<BYTE>& vecFrame) noexcept;
static HRESULT GcmEncrypt(_In_ BCRYPT_KEY_HANDLE hKey, _In_reads_bytes_(cbNonce) BYTE const* lpNonce, _In_ ULONG cbNonce,
                          _In_reads_bytes_opt_(cbAad) BYTE const* lpAad, _In_ ULONG cbAad,
                          _In_reads_bytes_(cbInput) BYTE const* lpInput, _In_ ULONG cbInput,
                          _Out_writes_bytes_(cbInput) BYTE* lpOutput,
                          _Out_writes_(GOVEE_GCM_TAG_LENGTH) BYTE byTag[GOVEE_GCM_TAG_LENGTH]) noexcept;
static HRESULT GcmDecrypt(_In_ BCRYPT_KEY_HANDLE hKey, _In_reads_bytes_(cbNonce) BYTE const* lpNonce, _In_ ULONG cbNonce,
                          _In_reads_bytes_opt_(cbAad) BYTE const* lpAad, _In_ ULONG cbAad,
                          _In_reads_bytes_(cbInput) BYTE const* lpInput, _In_ ULONG cbInput,
                          _Out_writes_bytes_(cbInput) BYTE* lpOutput,
                          _In_reads_(GOVEE_GCM_TAG_LENGTH) BYTE const byTag[GOVEE_GCM_TAG_LENGTH]) noexcept;
static HRESULT BuildGcmHandshakeRequest(_In_ BCRYPT_KEY_HANDLE hHandshakeKey,
                                        _In_reads_(GOVEE_GCM_IV_BASE_LENGTH) BYTE const bySendIv[GOVEE_GCM_IV_BASE_LENGTH],
                                        _In_reads_(GOVEE_GCM_IV_LENGTH) BYTE const byNonce[GOVEE_GCM_IV_LENGTH],
                                        _Out_ std::vector<BYTE>& vecFrame) noexcept;
static HRESULT ParseGcmHandshakeReply(_In_ BCRYPT_KEY_HANDLE hHandshakeKey, _In_ std::vector<BYTE> const& vecFrame,
                                      _Out_writes_(GOVEE_GCM_DEVICE_INFO_LENGTH) BYTE byDeviceInfo[GOVEE_GCM_DEVICE_INFO_LENGTH]) noexcept;
static void BuildGcmNonce(_In_reads_(GOVEE_GCM_IV_BASE_LENGTH) BYTE const byIvBase[GOVEE_GCM_IV_BASE_LENGTH],
                          _In_ UINT32 ulCounter, _Out_writes_(GOVEE_GCM_IV_LENGTH) BYTE byNonce[GOVEE_GCM_IV_LENGTH]) noexcept;
#if defined(_DEBUG)
static HRESULT VerifyGcmPrimitives(_In_ BCRYPT_ALG_HANDLE hGcmAlgorithm, _In_ BCRYPT_KEY_HANDLE hHandshakeKey,
                                   _In_ BCRYPT_KEY_HANDLE hDeviceDerivationKey) noexcept;
#endif

// -----------------------------------------------------------------------------

class GoveeBLEDevice::Impl : public std::enable_shared_from_this<Impl>
{
public:
    Impl(_In_ std::shared_ptr<BLEDevice> spDevice, _In_ EncryptionType_t encryptionType, _In_ Endpoint_t const& endpoint) noexcept
        : m_spDevice(std::move(spDevice)), m_encryptionType(encryptionType), m_endpoint(endpoint)
    {
        // Create the notification wake-up event before initializing protocol cryptography.
        m_hNotificationEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_hrCrypto = InitializeCrypto();
    }

    ~Impl() noexcept
    {
        // Stop callbacks before releasing session keys and notification storage.
        m_spSubscription.reset();
        ClearSession();
        ClearNotifications();
        DestroyCrypto();
        if (m_hNotificationEvent)
        {
            CloseHandle(m_hNotificationEvent);
        }
    }

    void ClearSession() noexcept
    {
        // Wipe all connection-specific keys, nonce state, and generation tracking.
        ClearCngKey(m_hV1SessionKey, m_vecV1SessionObject);
        ClearCngKey(m_hV2DeviceKey, m_vecV2DeviceObject);
        SecureZeroMemory(m_bySessionKey, sizeof(m_bySessionKey));
        SecureZeroMemory(m_byGcmSendIv, sizeof(m_byGcmSendIv));
        m_ulGcmSendCounter = 0;
        m_bSessionReady = FALSE;
        m_ullSessionGeneration = 0;
    }

    void ClearNotifications() noexcept
    {
        // Discard queued protocol replies and return the queue event to its idle state.
        std::lock_guard lock(m_notificationMutex);
        m_vecNotifications.clear();
        if (m_hNotificationEvent)
        {
            ResetEvent(m_hNotificationEvent);
        }
    }

    void AddNotification(_In_ std::vector<BYTE> const& vecNotification) noexcept
    {
        try
        {
            std::lock_guard lock(m_notificationMutex);

            // Queue the received payload before waking a waiting handshake.
            m_vecNotifications.push_back(vecNotification);
            SetEvent(m_hNotificationEvent);
        }
        catch (...)
        {
        }
    }

    HRESULT EnsureSession(_Inout_ BLEDevice::Operation& operation, _In_ BLEOperationContext const& context) noexcept
    {
        if (m_bSessionReady && m_ullSessionGeneration == operation.GetConnectionGeneration())
        {
            return S_OK;
        }

        // A new BLE connection cannot reuse encryption or notification state from the previous generation.
        m_spSubscription.reset();
        ClearSession();
        ClearNotifications();
        return m_encryptionType == EncryptionType_t::AesRc4Psk ? EnsureV1Session(operation, context)
                                                               : EnsureGcmSession(operation, context);
    }

    HRESULT EncryptGcmCommand(_In_ std::vector<BYTE> const& vecPlaintext, _Out_ std::vector<BYTE>& vecCommand) noexcept
    {
        BYTE byCounter[4];
        BYTE byNonce[GOVEE_GCM_IV_LENGTH];
        HRESULT hr;

        if (!m_hV2DeviceKey || m_ulGcmSendCounter == 0 || vecPlaintext.size() != GOVEE_FRAME_LENGTH)
        {
            return m_ulGcmSendCounter == 0 ? HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW) : E_INVALIDARG;
        }

        try
        {
            // Prefix the protected payload with its monotonic counter and authenticate both together.
            byCounter[0] = static_cast<BYTE>(m_ulGcmSendCounter >> 24);
            byCounter[1] = static_cast<BYTE>(m_ulGcmSendCounter >> 16);
            byCounter[2] = static_cast<BYTE>(m_ulGcmSendCounter >> 8);
            byCounter[3] = static_cast<BYTE>(m_ulGcmSendCounter);
            BuildGcmNonce(m_byGcmSendIv, m_ulGcmSendCounter, byNonce);
            vecCommand.resize(sizeof(byCounter) + vecPlaintext.size() + GOVEE_GCM_TAG_LENGTH);
            CopyMemory(vecCommand.data(), byCounter, sizeof(byCounter));
            hr = GcmEncrypt(m_hV2DeviceKey, byNonce, sizeof(byNonce), byCounter, sizeof(byCounter), vecPlaintext.data(),
                            static_cast<ULONG>(vecPlaintext.size()), vecCommand.data() + sizeof(byCounter),
                            vecCommand.data() + sizeof(byCounter) + vecPlaintext.size());
            SecureZeroMemory(byNonce, sizeof(byNonce));
            if (FAILED(hr))
            {
                vecCommand.clear();
                return hr;
            }
            ++m_ulGcmSendCounter;
            return S_OK;
        }
        catch (std::bad_alloc const&)
        {
            SecureZeroMemory(byNonce, sizeof(byNonce));
            return E_OUTOFMEMORY;
        }
        catch (...)
        {
            SecureZeroMemory(byNonce, sizeof(byNonce));
            return E_FAIL;
        }
    }

public:
    std::shared_ptr<BLEDevice> m_spDevice;
    EncryptionType_t m_encryptionType;
    Endpoint_t m_endpoint;
    std::mutex m_notificationMutex;
    HANDLE m_hNotificationEvent{ nullptr };
    std::deque<std::vector<BYTE>> m_vecNotifications;
    std::shared_ptr<BLEDevice::NotificationSubscription> m_spSubscription;
    BCRYPT_ALG_HANDLE m_hEcbAlgorithm{ nullptr };
    BCRYPT_ALG_HANDLE m_hGcmAlgorithm{ nullptr };
    BCRYPT_KEY_HANDLE m_hV1PskKey{ nullptr };
    BCRYPT_KEY_HANDLE m_hV2HandshakeKey{ nullptr };
    BCRYPT_KEY_HANDLE m_hV2DeviceDerivationKey{ nullptr };
    BCRYPT_KEY_HANDLE m_hV1SessionKey{ nullptr };
    BCRYPT_KEY_HANDLE m_hV2DeviceKey{ nullptr };
    std::vector<BYTE> m_vecV1PskObject;
    std::vector<BYTE> m_vecV2HandshakeObject;
    std::vector<BYTE> m_vecV2DeviceDerivationObject;
    std::vector<BYTE> m_vecV1SessionObject;
    std::vector<BYTE> m_vecV2DeviceObject;
    BYTE m_bySessionKey[GOVEE_KEY_LENGTH]{};
    BYTE m_byGcmSendIv[GOVEE_GCM_IV_BASE_LENGTH]{};
    UINT32 m_ulGcmSendCounter{ 0 };
    UINT64 m_ullSessionGeneration{ 0 };
    BOOL m_bSessionReady{ FALSE };
    HRESULT m_hrCrypto{ E_FAIL };

private:
    HRESULT InitializeCrypto() noexcept
    {
        NTSTATUS status;
        HRESULT hr;

        // Configure the CNG providers required by the legacy and GCM protocol variants.
        status = BCryptOpenAlgorithmProvider(&m_hEcbAlgorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
        if (status >= 0)
        {
            status = BCryptSetProperty(m_hEcbAlgorithm, BCRYPT_CHAINING_MODE,
                                       reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
                                       sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
        }
        if (status >= 0)
        {
            status = BCryptOpenAlgorithmProvider(&m_hGcmAlgorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
        }
        if (status >= 0)
        {
            status = BCryptSetProperty(m_hGcmAlgorithm, BCRYPT_CHAINING_MODE,
                                       reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                                       sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        }
        if (status < 0)
        {
            DestroyCrypto();
            return HRESULT_FROM_NT(status);
        }

        // Materialize the fixed protocol keys in the provider and retain their CNG backing objects.
        hr = CreateSymmetricKey(m_hEcbAlgorithm, GOVEE_PSK, sizeof(GOVEE_PSK), m_hV1PskKey, m_vecV1PskObject);
        if (SUCCEEDED(hr))
        {
            hr = CreateSymmetricKey(m_hGcmAlgorithm, GOVEE_GCM_HANDSHAKE_KEY, sizeof(GOVEE_GCM_HANDSHAKE_KEY),
                                    m_hV2HandshakeKey, m_vecV2HandshakeObject);
        }
        if (SUCCEEDED(hr))
        {
            hr = CreateSymmetricKey(m_hEcbAlgorithm, GOVEE_GCM_DEVICE_KEY, sizeof(GOVEE_GCM_DEVICE_KEY),
                                    m_hV2DeviceDerivationKey, m_vecV2DeviceDerivationObject);
        }

#if defined(_DEBUG)
        // Exercise the GCM wire-format primitives before accepting encrypted commands in debug builds.
        if (SUCCEEDED(hr))
        {
            hr = VerifyGcmPrimitives(m_hGcmAlgorithm, m_hV2HandshakeKey, m_hV2DeviceDerivationKey);
        }
#endif

        if (FAILED(hr))
        {
            DestroyCrypto();
        }
        return hr;
    }

    void DestroyCrypto() noexcept
    {
        // Wipe persistent CNG keys before closing their algorithm providers.
        ClearCngKey(m_hV1PskKey, m_vecV1PskObject);
        ClearCngKey(m_hV2HandshakeKey, m_vecV2HandshakeObject);
        ClearCngKey(m_hV2DeviceDerivationKey, m_vecV2DeviceDerivationObject);
        if (m_hGcmAlgorithm)
        {
            BCryptCloseAlgorithmProvider(m_hGcmAlgorithm, 0);
            m_hGcmAlgorithm = nullptr;
        }
        if (m_hEcbAlgorithm)
        {
            BCryptCloseAlgorithmProvider(m_hEcbAlgorithm, 0);
            m_hEcbAlgorithm = nullptr;
        }
    }

    HRESULT Subscribe(_Inout_ BLEDevice::Operation& operation, _In_ BOOL bEnableNotifications) noexcept
    {
        // Use a weak callback capture so the BLE subscription cannot prolong this protocol object.
        std::weak_ptr<Impl> weakThis = weak_from_this();

        return operation.Subscribe(m_endpoint.m_serviceUuid, m_endpoint.m_notificationCharacteristicUuid,
                                   [weakThis](std::vector<BYTE> const& vecNotification)
        {
            if (std::shared_ptr<Impl> spThis = weakThis.lock())
            {
                spThis->AddNotification(vecNotification);
            }
        }, m_spSubscription, bEnableNotifications);
    }

    HRESULT TakeNotification(_Out_ std::vector<BYTE>& vecNotification, _In_ BLEOperationContext const& context) noexcept
    {
        HANDLE hNotificationEvent;
        DWORD dwSignaledIndex;
        HRESULT hr;

        for (;;)
        {
            hr = context.Check();
            if (FAILED(hr))
            {
                return hr;
            }

            {
                std::lock_guard lock(m_notificationMutex);

                // Consume the oldest queued reply while holding the queue lock.
                if (!m_vecNotifications.empty())
                {
                    vecNotification = std::move(m_vecNotifications.front());
                    m_vecNotifications.pop_front();
                    if (m_vecNotifications.empty())
                    {
                        ResetEvent(m_hNotificationEvent);
                    }
                    return S_OK;
                }
            }

            // Wait for a producer to add a reply while honoring the caller's cancellation and deadline.
            hNotificationEvent = m_hNotificationEvent;
            hr = context.Wait(&hNotificationEvent, 1, &dwSignaledIndex);
            if (FAILED(hr))
            {
                return hr;
            }
        }
    }

    HRESULT WaitForV1HandshakeReply(_In_ BYTE byStep, _Out_writes_opt_(GOVEE_KEY_LENGTH) BYTE bySessionKey[GOVEE_KEY_LENGTH],
                                    _In_ BLEOperationContext const& context) noexcept
    {
        std::vector<BYTE> vecEncrypted;
        std::vector<BYTE> vecPlaintext;
        HRESULT hr;

        for (;;)
        {
            vecEncrypted.clear();
            hr = TakeNotification(vecEncrypted, context);
            if (FAILED(hr))
            {
                return hr;
            }

            hr = TransformFrame(vecEncrypted, m_hV1PskKey, GOVEE_PSK, vecPlaintext, FALSE);
            if (FAILED(hr))
            {
                return hr;
            }

            if (!IsValidFrame(vecPlaintext) || vecPlaintext.size() < 2 || vecPlaintext[0] != GOVEE_HANDSHAKE ||
                vecPlaintext[1] != byStep)
            {
                // Ignore unrelated notifications until the expected encrypted handshake reply arrives.
                continue;
            }
            if (bySessionKey)
            {
                if (vecPlaintext.size() < 18)
                {
                    continue;
                }
                CopyMemory(bySessionKey, vecPlaintext.data() + 2, GOVEE_KEY_LENGTH);
            }
            return S_OK;
        }
    }

    HRESULT EnsureV1Session(_Inout_ BLEDevice::Operation& operation, _In_ BLEOperationContext const& context) noexcept
    {
        BYTE bySessionKey[GOVEE_KEY_LENGTH]{};
        std::vector<BYTE> vecPlaintext;
        std::vector<BYTE> vecEncrypted;
        HRESULT hr = Subscribe(operation, TRUE);

        // Request, receive, and retain the session key using the first legacy handshake step.
        if (SUCCEEDED(hr))
        {
            hr = BuildHandshakeFrame(GOVEE_REQUEST_SESSION_KEY, vecPlaintext);
        }
        if (SUCCEEDED(hr))
        {
            hr = TransformFrame(vecPlaintext, m_hV1PskKey, GOVEE_PSK, vecEncrypted, TRUE);
        }
        if (SUCCEEDED(hr))
        {
            hr = operation.Write(m_endpoint.m_serviceUuid, m_endpoint.m_writeCharacteristicUuid, vecEncrypted.data(), vecEncrypted.size());
        }
        if (SUCCEEDED(hr))
        {
            hr = WaitForV1HandshakeReply(GOVEE_REQUEST_SESSION_KEY, bySessionKey, context);
        }

        if (SUCCEEDED(hr))
        {
            // Clear stale replies before confirming that the device accepted the new session key.
            ClearNotifications();
            hr = BuildHandshakeFrame(GOVEE_CONFIRM_SESSION_KEY, vecPlaintext);
        }
        if (SUCCEEDED(hr))
        {
            hr = TransformFrame(vecPlaintext, m_hV1PskKey, GOVEE_PSK, vecEncrypted, TRUE);
        }
        if (SUCCEEDED(hr))
        {
            hr = operation.Write(m_endpoint.m_serviceUuid, m_endpoint.m_writeCharacteristicUuid, vecEncrypted.data(), vecEncrypted.size());
        }
        if (SUCCEEDED(hr))
        {
            hr = WaitForV1HandshakeReply(GOVEE_CONFIRM_SESSION_KEY, nullptr, context);
        }

        if (SUCCEEDED(hr))
        {
            hr = CreateSymmetricKey(m_hEcbAlgorithm, bySessionKey, sizeof(bySessionKey), m_hV1SessionKey, m_vecV1SessionObject);
        }
        if (SUCCEEDED(hr))
        {
            // Bind the new session to this BLE connection generation.
            CopyMemory(m_bySessionKey, bySessionKey, sizeof(m_bySessionKey));
            m_ullSessionGeneration = operation.GetConnectionGeneration();
            m_bSessionReady = TRUE;
        }

        SecureZeroMemory(bySessionKey, sizeof(bySessionKey));
        if (FAILED(hr))
        {
            ClearSession();
        }
        return hr;
    }

    HRESULT WaitForGcmHandshakeReply(_Out_writes_(GOVEE_GCM_DEVICE_INFO_LENGTH) BYTE byDeviceInfo[GOVEE_GCM_DEVICE_INFO_LENGTH],
                                     _In_ BLEOperationContext const& context) noexcept
    {
        std::vector<BYTE> vecNotification;
        HRESULT hr;

        for (;;)
        {
            vecNotification.clear();
            hr = TakeNotification(vecNotification, context);
            if (FAILED(hr))
            {
                return hr;
            }

            if (vecNotification.size() < 2 || vecNotification[0] != GOVEE_HANDSHAKE ||
                vecNotification[1] != GOVEE_GCM_HANDSHAKE)
            {
                // Ignore notifications from other protocol traffic while waiting for the GCM reply.
                continue;
            }
            return ParseGcmHandshakeReply(m_hV2HandshakeKey, vecNotification, byDeviceInfo);
        }
    }

    HRESULT EnsureGcmSession(_Inout_ BLEDevice::Operation& operation, _In_ BLEOperationContext const& context) noexcept
    {
        BYTE byNonce[GOVEE_GCM_IV_LENGTH]{};
        BYTE byDeviceInfo[GOVEE_GCM_DEVICE_INFO_LENGTH]{};
        BYTE byKeyMaterial[GOVEE_KEY_LENGTH]{};
        std::vector<BYTE> vecRequest;
        NTSTATUS status;
        HRESULT hr = Subscribe(operation, FALSE);

        // Generate fresh IV material and encode the authenticated GCM handshake request.
        if (SUCCEEDED(hr))
        {
            status = BCryptGenRandom(nullptr, m_byGcmSendIv, sizeof(m_byGcmSendIv), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            hr = status < 0 ? HRESULT_FROM_NT(status) : S_OK;
        }
        if (SUCCEEDED(hr))
        {
            status = BCryptGenRandom(nullptr, byNonce, sizeof(byNonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            hr = status < 0 ? HRESULT_FROM_NT(status) : S_OK;
        }
        if (SUCCEEDED(hr))
        {
            hr = BuildGcmHandshakeRequest(m_hV2HandshakeKey, m_byGcmSendIv, byNonce, vecRequest);
        }
        SecureZeroMemory(byNonce, sizeof(byNonce));
        if (SUCCEEDED(hr))
        {
            hr = operation.Write(m_endpoint.m_serviceUuid, m_endpoint.m_writeCharacteristicUuid, vecRequest.data(), vecRequest.size());
        }
        if (SUCCEEDED(hr))
        {
            hr = WaitForGcmHandshakeReply(byDeviceInfo, context);
        }

        if (SUCCEEDED(hr))
        {
            // Derive and retain the per-device AES-GCM session key from the handshake reply.
            CopyMemory(byKeyMaterial, byDeviceInfo, sizeof(byDeviceInfo));
            hr = AesEcbTransform(m_hV2DeviceDerivationKey, byKeyMaterial, byKeyMaterial, TRUE);
        }
        if (SUCCEEDED(hr))
        {
            hr = CreateSymmetricKey(m_hGcmAlgorithm, byKeyMaterial, sizeof(byKeyMaterial), m_hV2DeviceKey, m_vecV2DeviceObject);
        }
        if (SUCCEEDED(hr))
        {
            // Start the first command counter and bind the session to the active BLE connection.
            m_ulGcmSendCounter = 1;
            m_ullSessionGeneration = operation.GetConnectionGeneration();
            m_bSessionReady = TRUE;
        }

        SecureZeroMemory(byDeviceInfo, sizeof(byDeviceInfo));
        SecureZeroMemory(byKeyMaterial, sizeof(byKeyMaterial));
        if (FAILED(hr))
        {
            ClearSession();
        }
        return hr;
    }
};

// -----------------------------------------------------------------------------

GoveeBLEDevice::GoveeBLEDevice(_In_ std::shared_ptr<BLEDevice> spDevice, _In_ EncryptionType_t encryptionType) noexcept
    : GoveeBLEDevice(std::move(spDevice), encryptionType, GetDefaultEndpoint())
{
}

GoveeBLEDevice::GoveeBLEDevice(_In_ std::shared_ptr<BLEDevice> spDevice, _In_ EncryptionType_t encryptionType,
                               _In_ Endpoint_t const& endpoint) noexcept
    : m_spDevice(std::move(spDevice)), m_encryptionType(encryptionType), m_endpoint(endpoint)
{
}

GoveeBLEDevice::~GoveeBLEDevice() noexcept = default;

_Success_(return == S_OK)
HRESULT GoveeBLEDevice::GetOrCreateImpl(_Out_ std::shared_ptr<Impl>& spImpl) noexcept
{
    spImpl.reset();

    try
    {
        std::lock_guard lock(m_mutex);

        // Lazily create the shared protocol state so construction remains non-throwing.
        if (!m_spImpl)
        {
            m_spImpl = std::make_shared<Impl>(m_spDevice, m_encryptionType, m_endpoint);
        }
        spImpl = m_spImpl;
        return S_OK;
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

_Must_inspect_result_
GoveeBLEDevice::Endpoint_t GoveeBLEDevice::GetDefaultEndpoint() noexcept
{
    return Endpoint_t{
        { 0x00010203, 0x0405, 0x0607, { 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x19, 0x10 } },
        { 0x00010203, 0x0405, 0x0607, { 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x2b, 0x10 } },
        { 0x00010203, 0x0405, 0x0607, { 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x2b, 0x11 } },
    };
}

_Success_(return == S_OK)
HRESULT GoveeBLEDevice::SendCommand(_In_reads_bytes_opt_(nDataLen) LPCVOID lpData, _In_ SIZE_T nDataLen, _In_ BOOL bWaitForAck,
                                    _In_ BLEOperationContext const& context) noexcept
{
    std::shared_ptr<Impl> spImpl;
    std::shared_ptr<BLEDevice::Operation> spOperation;
    std::vector<BYTE> vecCommand;
    HRESULT hr;

    if ((!lpData && nDataLen != 0) || nDataLen > MAXUINT32)
    {
        return E_INVALIDARG;
    }

    // Acquire the persistent protocol state and reject unusable initialization results.
    hr = GetOrCreateImpl(spImpl);
    if (FAILED(hr))
    {
        return hr;
    }

    if (!spImpl || !spImpl->m_spDevice || !spImpl->m_hNotificationEvent)
    {
        return E_OUTOFMEMORY;
    }
    if (spImpl->m_encryptionType != EncryptionType_t::None && FAILED(spImpl->m_hrCrypto))
    {
        return spImpl->m_hrCrypto;
    }

    try
    {
        // Serialize the complete session and command exchange on one BLE connection.
        hr = spImpl->m_spDevice->BeginOperation(spOperation, context);
        if (FAILED(hr))
        {
            return hr;
        }

        if (spImpl->m_encryptionType == EncryptionType_t::None)
        {
            // Unencrypted devices write the caller payload directly.
            return spOperation->Write(spImpl->m_endpoint.m_serviceUuid, spImpl->m_endpoint.m_writeCharacteristicUuid,
                                      lpData, nDataLen, bWaitForAck);
        }

        // Establish or reuse a session before transforming the command payload.
        hr = spImpl->EnsureSession(*spOperation, context);
        if (FAILED(hr))
        {
            return hr;
        }

        if (nDataLen != 0)
        {
            // Copy into owned storage because encryption transforms the command in place.
            BYTE const* lpBytes = static_cast<BYTE const*>(lpData);
            vecCommand.assign(lpBytes, lpBytes + nDataLen);
        }

        if (spImpl->m_encryptionType == EncryptionType_t::AesRc4Psk)
        {
            // Legacy devices use AES-ECB blocks with an RC4 tail.
            hr = TransformFrame(vecCommand, spImpl->m_hV1SessionKey, spImpl->m_bySessionKey, vecCommand, TRUE);
        }
        else
        {
            // GCM devices use the connection-specific key and monotonically increasing nonce.
            hr = spImpl->EncryptGcmCommand(vecCommand, vecCommand);
        }

        if (FAILED(hr))
        {
            // A failed transform invalidates the session's key or counter state.
            spImpl->ClearSession();
            return hr;
        }
        hr = spOperation->Write(spImpl->m_endpoint.m_serviceUuid, spImpl->m_endpoint.m_writeCharacteristicUuid,
                                vecCommand.data(), vecCommand.size(), bWaitForAck);
        if (FAILED(hr))
        {
            // A failed write may leave device and local session state out of sync.
            spImpl->ClearSession();
        }
        return hr;
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

// -----------------------------------------------------------------------------

static BYTE CalculateChecksum(_In_reads_bytes_(nDataLen) BYTE const* lpData, _In_ SIZE_T nDataLen) noexcept
{
    // Calculate the protocol's XOR checksum over the frame excluding its checksum byte.
    BYTE byChecksum = 0;
    for (SIZE_T n = 0; n < nDataLen; ++n)
    {
        byChecksum ^= lpData[n];
    }
    return byChecksum;
}

static BOOL IsValidFrame(_In_ std::vector<BYTE> const& vecFrame) noexcept
{
    return vecFrame.size() == GOVEE_FRAME_LENGTH && CalculateChecksum(vecFrame.data(), vecFrame.size() - 1) == vecFrame.back();
}

static void ClearCngKey(_Inout_ BCRYPT_KEY_HANDLE& hKey, _Inout_ std::vector<BYTE>& vecObject) noexcept
{
    // Destroy the CNG key and wipe its provider object buffer before releasing storage.
    if (hKey)
    {
        BCryptDestroyKey(hKey);
        hKey = nullptr;
    }
    if (!vecObject.empty())
    {
        SecureZeroMemory(vecObject.data(), vecObject.size());
        vecObject.clear();
    }
}

static HRESULT CreateSymmetricKey(_In_ BCRYPT_ALG_HANDLE hAlgorithm, _In_reads_bytes_(cbKey) BYTE const* lpKey,
                                  _In_ ULONG cbKey, _Inout_ BCRYPT_KEY_HANDLE& hKey,
                                  _Inout_ std::vector<BYTE>& vecObject) noexcept
{
    DWORD cbObject = 0;
    DWORD cbResult = 0;
    NTSTATUS status;

    if (!hAlgorithm || !lpKey || cbKey == 0)
    {
        return E_INVALIDARG;
    }

    ClearCngKey(hKey, vecObject);
    status = BCryptGetProperty(hAlgorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&cbObject), sizeof(cbObject), &cbResult, 0);
    if (status < 0)
    {
        return HRESULT_FROM_NT(status);
    }

    try
    {
        // Allocate the provider-specific key object before importing the symmetric key bytes.
        vecObject.resize(cbObject);
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
    status = BCryptGenerateSymmetricKey(hAlgorithm, &hKey, vecObject.data(), static_cast<ULONG>(vecObject.size()),
                                        const_cast<PUCHAR>(lpKey), cbKey, 0);
    if (status < 0)
    {
        ClearCngKey(hKey, vecObject);
        return HRESULT_FROM_NT(status);
    }
    return S_OK;
}

static HRESULT AesEcbTransform(_In_ BCRYPT_KEY_HANDLE hKey, _In_reads_bytes_(GOVEE_KEY_LENGTH) BYTE const* lpInput,
                               _Out_writes_bytes_(GOVEE_KEY_LENGTH) BYTE* lpOutput, _In_ BOOL bEncrypt) noexcept
{
    DWORD cbResult = 0;
    NTSTATUS status;

    if (!hKey || !lpInput || !lpOutput)
    {
        return E_INVALIDARG;
    }

    status = bEncrypt ? BCryptEncrypt(hKey, const_cast<PUCHAR>(lpInput), GOVEE_KEY_LENGTH, nullptr, nullptr, 0, lpOutput,
                                      GOVEE_KEY_LENGTH, &cbResult, 0) :
                         BCryptDecrypt(hKey, const_cast<PUCHAR>(lpInput), GOVEE_KEY_LENGTH, nullptr, nullptr, 0, lpOutput,
                                      GOVEE_KEY_LENGTH, &cbResult, 0);
    return status < 0 ? HRESULT_FROM_NT(status) : (cbResult == GOVEE_KEY_LENGTH ? S_OK : E_FAIL);
}

static void Rc4Transform(_In_reads_(GOVEE_KEY_LENGTH) BYTE const byKey[GOVEE_KEY_LENGTH],
                         _In_reads_bytes_(nDataLen) BYTE const* lpInput, _Out_writes_bytes_(nDataLen) BYTE* lpOutput,
                         _In_ SIZE_T nDataLen) noexcept
{
    BYTE byState[256]{};
    BYTE i = 0;
    BYTE j = 0;

    for (UINT n = 0; n < _countof(byState); ++n)
    {
        // Initialize the RC4 permutation with the supplied session key.
        byState[n] = static_cast<BYTE>(n);
        j = static_cast<BYTE>(j + byState[n] + byKey[n % GOVEE_KEY_LENGTH]);
        std::swap(byState[n], byState[j]);
    }
    j = 0;
    for (SIZE_T n = 0; n < nDataLen; ++n)
    {
        // Generate and apply the RC4 keystream to the partial final block.
        i = static_cast<BYTE>(i + 1);
        j = static_cast<BYTE>(j + byState[i]);
        std::swap(byState[i], byState[j]);
        lpOutput[n] = lpInput[n] ^ byState[static_cast<BYTE>(byState[i] + byState[j])];
    }
    SecureZeroMemory(byState, sizeof(byState));
}

static HRESULT TransformFrame(_In_ std::vector<BYTE> const& vecInput, _In_ BCRYPT_KEY_HANDLE hEcbKey,
                              _In_reads_(GOVEE_KEY_LENGTH) BYTE const byRc4Key[GOVEE_KEY_LENGTH],
                              _Out_ std::vector<BYTE>& vecOutput, _In_ BOOL bEncrypt) noexcept
{
    HRESULT hr;

    if (!hEcbKey || !byRc4Key)
    {
        return E_INVALIDARG;
    }

    try
    {
        // Encrypt full 16-byte blocks with AES and use RC4 for a trailing partial block.
        SIZE_T cbBlocks = vecInput.size() / GOVEE_KEY_LENGTH;
        SIZE_T cbRemainder = vecInput.size() % GOVEE_KEY_LENGTH;
        vecOutput.resize(vecInput.size());
        for (SIZE_T n = 0; n < cbBlocks; ++n)
        {
            hr = AesEcbTransform(hEcbKey, vecInput.data() + n * GOVEE_KEY_LENGTH,
                                 vecOutput.data() + n * GOVEE_KEY_LENGTH, bEncrypt);
            if (FAILED(hr))
            {
                vecOutput.clear();
                return hr;
            }
        }
        if (cbRemainder != 0)
        {
            Rc4Transform(byRc4Key, vecInput.data() + cbBlocks * GOVEE_KEY_LENGTH,
                         vecOutput.data() + cbBlocks * GOVEE_KEY_LENGTH, cbRemainder);
        }
        return S_OK;
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

static HRESULT BuildHandshakeFrame(_In_ BYTE byStep, _Out_ std::vector<BYTE>& vecFrame) noexcept
{
    NTSTATUS status;
    try
    {
        // Fill the handshake body with random challenge bytes and its trailing checksum.
        vecFrame.resize(GOVEE_FRAME_LENGTH);
        vecFrame[0] = GOVEE_HANDSHAKE;
        vecFrame[1] = byStep;
        status = BCryptGenRandom(nullptr, vecFrame.data() + 2, static_cast<ULONG>(GOVEE_FRAME_LENGTH - 3),
                                 BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status < 0)
        {
            vecFrame.clear();
            return HRESULT_FROM_NT(status);
        }
        vecFrame.back() = CalculateChecksum(vecFrame.data(), vecFrame.size() - 1);
        return S_OK;
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

static HRESULT GcmEncrypt(_In_ BCRYPT_KEY_HANDLE hKey, _In_reads_bytes_(cbNonce) BYTE const* lpNonce, _In_ ULONG cbNonce,
                          _In_reads_bytes_opt_(cbAad) BYTE const* lpAad, _In_ ULONG cbAad,
                          _In_reads_bytes_(cbInput) BYTE const* lpInput, _In_ ULONG cbInput,
                          _Out_writes_bytes_(cbInput) BYTE* lpOutput,
                          _Out_writes_(GOVEE_GCM_TAG_LENGTH) BYTE byTag[GOVEE_GCM_TAG_LENGTH]) noexcept
{
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    DWORD cbResult = 0;
    NTSTATUS status;

    if (!hKey || !lpNonce || !lpInput || !lpOutput || !byTag)
    {
        return E_INVALIDARG;
    }

    BCRYPT_INIT_AUTH_MODE_INFO(info);
    // Bind the nonce, authenticated header, and output tag for the GCM operation.
    info.pbNonce = const_cast<PUCHAR>(lpNonce);
    info.cbNonce = cbNonce;
    info.pbAuthData = const_cast<PUCHAR>(lpAad);
    info.cbAuthData = cbAad;
    info.pbTag = byTag;
    info.cbTag = GOVEE_GCM_TAG_LENGTH;
    status = BCryptEncrypt(hKey, const_cast<PUCHAR>(lpInput), cbInput, &info, nullptr, 0, lpOutput, cbInput, &cbResult, 0);
    return status < 0 ? HRESULT_FROM_NT(status) : (cbResult == cbInput ? S_OK : E_FAIL);
}

static HRESULT GcmDecrypt(_In_ BCRYPT_KEY_HANDLE hKey, _In_reads_bytes_(cbNonce) BYTE const* lpNonce, _In_ ULONG cbNonce,
                          _In_reads_bytes_opt_(cbAad) BYTE const* lpAad, _In_ ULONG cbAad,
                          _In_reads_bytes_(cbInput) BYTE const* lpInput, _In_ ULONG cbInput,
                          _Out_writes_bytes_(cbInput) BYTE* lpOutput,
                          _In_reads_(GOVEE_GCM_TAG_LENGTH) BYTE const byTag[GOVEE_GCM_TAG_LENGTH]) noexcept
{
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    DWORD cbResult = 0;
    NTSTATUS status;

    if (!hKey || !lpNonce || !lpInput || !lpOutput || !byTag)
    {
        return E_INVALIDARG;
    }

    BCRYPT_INIT_AUTH_MODE_INFO(info);
    // Require the supplied authentication tag while decrypting the GCM payload.
    info.pbNonce = const_cast<PUCHAR>(lpNonce);
    info.cbNonce = cbNonce;
    info.pbAuthData = const_cast<PUCHAR>(lpAad);
    info.cbAuthData = cbAad;
    info.pbTag = const_cast<PUCHAR>(byTag);
    info.cbTag = GOVEE_GCM_TAG_LENGTH;
    status = BCryptDecrypt(hKey, const_cast<PUCHAR>(lpInput), cbInput, &info, nullptr, 0, lpOutput, cbInput, &cbResult, 0);
    return status < 0 ? HRESULT_FROM_NT(status) : (cbResult == cbInput ? S_OK : E_FAIL);
}

static HRESULT BuildGcmHandshakeRequest(_In_ BCRYPT_KEY_HANDLE hHandshakeKey,
                                        _In_reads_(GOVEE_GCM_IV_BASE_LENGTH) BYTE const bySendIv[GOVEE_GCM_IV_BASE_LENGTH],
                                        _In_reads_(GOVEE_GCM_IV_LENGTH) BYTE const byNonce[GOVEE_GCM_IV_LENGTH],
                                        _Out_ std::vector<BYTE>& vecFrame) noexcept
{
    HRESULT hr;
    if (!hHandshakeKey || !bySendIv || !byNonce)
    {
        return E_INVALIDARG;
    }

    try
    {
        // Encode the fixed GCM request header, then authenticate the local send IV.
        vecFrame.resize(GOVEE_GCM_REQUEST_LENGTH);
        vecFrame[0] = GOVEE_HANDSHAKE;
        vecFrame[1] = GOVEE_GCM_HANDSHAKE;
        vecFrame[2] = GOVEE_REQUEST_SESSION_KEY;
        CopyMemory(vecFrame.data() + 3, byNonce, GOVEE_GCM_IV_LENGTH);
        vecFrame[15] = GOVEE_GCM_TAG_LENGTH;
        hr = GcmEncrypt(hHandshakeKey, byNonce, GOVEE_GCM_IV_LENGTH, vecFrame.data(), 16, bySendIv,
                        GOVEE_GCM_IV_BASE_LENGTH, vecFrame.data() + 16, vecFrame.data() + 24);
        if (FAILED(hr))
        {
            vecFrame.clear();
        }
        return hr;
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

static HRESULT ParseGcmHandshakeReply(_In_ BCRYPT_KEY_HANDLE hHandshakeKey, _In_ std::vector<BYTE> const& vecFrame,
                                      _Out_writes_(GOVEE_GCM_DEVICE_INFO_LENGTH) BYTE byDeviceInfo[GOVEE_GCM_DEVICE_INFO_LENGTH]) noexcept
{
    BYTE byReply[19]{};
    HRESULT hr;

    if (!hHandshakeKey || !byDeviceInfo || vecFrame.size() != GOVEE_GCM_REPLY_LENGTH || vecFrame[0] != GOVEE_HANDSHAKE ||
        vecFrame[1] != GOVEE_GCM_HANDSHAKE || vecFrame[2] != GOVEE_GCM_SUCCESS)
    {
        return E_INVALIDARG;
    }

    hr = GcmDecrypt(hHandshakeKey, vecFrame.data() + 3, GOVEE_GCM_IV_LENGTH, vecFrame.data(), 15, vecFrame.data() + 15,
                    19, byReply, vecFrame.data() + vecFrame.size() - GOVEE_GCM_TAG_LENGTH);
    if (SUCCEEDED(hr))
    {
        // Extract the device-specific derivation material from the authenticated reply.
        CopyMemory(byDeviceInfo, byReply + GOVEE_GCM_IV_BASE_LENGTH, GOVEE_GCM_DEVICE_INFO_LENGTH);
    }
    SecureZeroMemory(byReply, sizeof(byReply));
    return hr;
}

static void BuildGcmNonce(_In_reads_(GOVEE_GCM_IV_BASE_LENGTH) BYTE const byIvBase[GOVEE_GCM_IV_BASE_LENGTH],
                          _In_ UINT32 ulCounter, _Out_writes_(GOVEE_GCM_IV_LENGTH) BYTE byNonce[GOVEE_GCM_IV_LENGTH]) noexcept
{
    // Form the 96-bit GCM nonce from the session IV prefix and big-endian command counter.
    CopyMemory(byNonce, byIvBase, GOVEE_GCM_IV_BASE_LENGTH);
    byNonce[8] = static_cast<BYTE>(ulCounter >> 24);
    byNonce[9] = static_cast<BYTE>(ulCounter >> 16);
    byNonce[10] = static_cast<BYTE>(ulCounter >> 8);
    byNonce[11] = static_cast<BYTE>(ulCounter);
}

#if defined(_DEBUG)
static HRESULT VerifyGcmPrimitives(_In_ BCRYPT_ALG_HANDLE hGcmAlgorithm, _In_ BCRYPT_KEY_HANDLE hHandshakeKey,
                                   _In_ BCRYPT_KEY_HANDLE hDeviceDerivationKey) noexcept
{
    BCRYPT_KEY_HANDLE hDeviceKey = nullptr;
    std::vector<BYTE> vecDeviceObject;
    std::vector<BYTE> vecFrame;
    BYTE bySendIv[GOVEE_GCM_IV_BASE_LENGTH]{ 1, 2, 3, 4, 5, 6, 7, 8 };
    BYTE byDeviceInfo[GOVEE_GCM_DEVICE_INFO_LENGTH]{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    BYTE byHandshakeNonce[GOVEE_GCM_IV_LENGTH]{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    BYTE byHandshakePlaintext[GOVEE_GCM_IV_BASE_LENGTH]{};
    BYTE byKeyMaterial[GOVEE_KEY_LENGTH]{};
    BYTE byNonce[GOVEE_GCM_IV_LENGTH];
    BYTE byCounter[]{ 0x00, 0x00, 0x00, 0x01 };
    BYTE byPlaintext[GOVEE_FRAME_LENGTH]{};
    BYTE byCiphertext[GOVEE_FRAME_LENGTH]{};
    BYTE byTag[GOVEE_GCM_TAG_LENGTH]{};
    BYTE byRoundTrip[GOVEE_FRAME_LENGTH]{};
    HRESULT hr;

    for (UINT n = 0; n < _countof(byPlaintext); ++n)
    {
        byPlaintext[n] = static_cast<BYTE>(n);
    }

    // Verify the expected handshake framing and authenticated IV payload.
    hr = BuildGcmHandshakeRequest(hHandshakeKey, bySendIv, byHandshakeNonce, vecFrame);
    if (SUCCEEDED(hr) && (vecFrame.size() != GOVEE_GCM_REQUEST_LENGTH || vecFrame[0] != GOVEE_HANDSHAKE ||
                          vecFrame[1] != GOVEE_GCM_HANDSHAKE || vecFrame[2] != GOVEE_REQUEST_SESSION_KEY ||
                          vecFrame[15] != GOVEE_GCM_TAG_LENGTH))
    {
        hr = E_FAIL;
    }
    if (SUCCEEDED(hr))
    {
        hr = GcmDecrypt(hHandshakeKey, byHandshakeNonce, sizeof(byHandshakeNonce), vecFrame.data(), 16,
                        vecFrame.data() + 16, GOVEE_GCM_IV_BASE_LENGTH, byHandshakePlaintext, vecFrame.data() + 24);
    }
    if (SUCCEEDED(hr) && memcmp(byHandshakePlaintext, bySendIv, sizeof(bySendIv)) != 0)
    {
        hr = E_FAIL;
    }

    if (SUCCEEDED(hr))
    {
        // Tampering with the authentication tag must cause decryption to fail.
        vecFrame[24] ^= 0x01;
        if (SUCCEEDED(GcmDecrypt(hHandshakeKey, byHandshakeNonce, sizeof(byHandshakeNonce), vecFrame.data(), 16,
                                 vecFrame.data() + 16, GOVEE_GCM_IV_BASE_LENGTH, byHandshakePlaintext, vecFrame.data() + 24)))
        {
            hr = E_FAIL;
        }
        vecFrame[24] ^= 0x01;
    }
    if (SUCCEEDED(hr))
    {
        // Derive a device key and verify one protected command round trip.
        CopyMemory(byKeyMaterial, byDeviceInfo, sizeof(byDeviceInfo));
        hr = AesEcbTransform(hDeviceDerivationKey, byKeyMaterial, byKeyMaterial, TRUE);
    }
    if (SUCCEEDED(hr))
    {
        hr = CreateSymmetricKey(hGcmAlgorithm, byKeyMaterial, sizeof(byKeyMaterial), hDeviceKey, vecDeviceObject);
    }

    if (SUCCEEDED(hr))
    {
        BuildGcmNonce(bySendIv, 1, byNonce);
        hr = GcmEncrypt(hDeviceKey, byNonce, sizeof(byNonce), byCounter, sizeof(byCounter), byPlaintext,
                        sizeof(byPlaintext), byCiphertext, byTag);
    }
    if (SUCCEEDED(hr))
    {
        hr = GcmDecrypt(hDeviceKey, byNonce, sizeof(byNonce), byCounter, sizeof(byCounter), byCiphertext,
                        sizeof(byCiphertext), byRoundTrip, byTag);
    }
    if (SUCCEEDED(hr) && memcmp(byRoundTrip, byPlaintext, sizeof(byPlaintext)) != 0)
    {
        hr = E_FAIL;
    }

    // Dispose of all temporary test keys and plaintext buffers.
    ClearCngKey(hDeviceKey, vecDeviceObject);
    SecureZeroMemory(byHandshakePlaintext, sizeof(byHandshakePlaintext));
    SecureZeroMemory(byKeyMaterial, sizeof(byKeyMaterial));
    SecureZeroMemory(byNonce, sizeof(byNonce));
    SecureZeroMemory(byCiphertext, sizeof(byCiphertext));
    SecureZeroMemory(byTag, sizeof(byTag));
    SecureZeroMemory(byRoundTrip, sizeof(byRoundTrip));
    return hr;
}
#endif
