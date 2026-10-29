#include "ir_codes.h"
#include "ir/receiver.h"
#include "iot_comm/utils/network.h"
#include "output/output_controller.h"
#include "utils/config.h"
#include "utils/hsv.h"
#include "utils/rgb.h"
#include <assert.h>
#include <inttypes.h>
#include <esp_mac.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <iot_comm/captive_portal/captive_portal.h>
#include <iot_comm/iot_comm.h>
#include <iot_comm/crypto/sha.h>
#include <iot_comm/ota/ota.h>
#include <iot_comm/provisioning/wifi.h>
#include <iot_comm/utils/binary.h>
#include <mutex.h>
#include <onboard_led.h>
#include <reset_button.h>
#include <rundown_protection.h>
#include <run_once.h>
#include <storage/nvs.h>
#include <stdlib.h>
#include <string.h>
#include <task.h>

static const char* TAG = "MAIN";

#define CMD_SET_COLOR                1
#define CMD_FADE_TO_COLOR            2
#define CMD_SET_AND_FADE_TO_COLOR    3
#define CMD_SET_RAW_DMX_CHANNEL_DATA 4
#define CMD_SET_MDNS_HOSTNAME        0x7000
#define CMD_GET_DEVICE_CONFIG        0x7001
#define CMD_SET_DEVICE_CONFIG        0x7002

#define DEVICE_CONFIG_REPLY_VERSION     1
#define DEVICE_CMD_FLAG_SET_COLOR       (1U << 0)
#define DEVICE_CMD_FLAG_FADE_TO_COLOR   (1U << 1)
#define DEVICE_CMD_FLAG_SET_AND_FADE    (1U << 2)
#define DEVICE_CMD_FLAG_GET_CONFIG      (1U << 4)
#define DEVICE_CMD_FLAG_SET_DMX_CHANNEL (1U << 5)
#define DEVICE_CMD_FLAG_SET_CONFIG      (1U << 6)

// -----------------------------------------------------------------------------

static RundownProtection_t rpReset = RUNDOWN_PROTECTION_INIT_STATIC;
static RunOnce_t resetOnce = RUN_ONCE_INIT_STATIC;
static Mutex appStateMtx;

static uint32_t onboardLedTimerCounter = 0;

static NVSStorage storage;
static bool mdnsHttpServiceRegistered = false;
static char provisioningSSID[32];
static bool provisioningRootUserConfigured = false;

#ifdef CONFIG_COMPILER_OPTIMIZATION_DEBUG
static bool powerIsOff = false;
#else
static bool powerIsOff = true;
#endif
static bool fixedColorActive = false;
static HSVColor_t currentColor = {0, 0, 0xFFFF};

// -----------------------------------------------------------------------------

static void setupTask(Task_t task, void* arg);

static void wifiMgrEventHandler(WifiMgrEvent_t event, void* ctx);
static void startProvisioning();

static esp_err_t captivePortalCredentialsHandler(CaptivePortalProvisioningConfig_t* creds, void* ctx);
static esp_err_t captivePortalRecoveryCredentialsHandler(CaptivePortalProvisioningConfig_t* creds, void* ctx);

static void iotCommEventHandler(IotCommEvent_t* event);

static esp_err_t loadFromStorage(IotCommStorageItemType_t itemType, void* dest, size_t destLen, void* ctx);
static esp_err_t saveToStorage(IotCommStorageItemType_t itemType, const void* data, size_t dataLen, void* ctx);

static void commStart();
static void commStop();
static bool commIsActive();

static void onCustomCommand(IotCommEvent_t* event);
static void onSetColorCmd(IotCommEvent_t* event);
static void onFadeToColorCmd(IotCommEvent_t* event);
static void onSetAndFadeToColorCmd(IotCommEvent_t* event);
static void onSetDmxChannelDataCmd(IotCommEvent_t* event);
static void onSetMDnsHostName(IotCommEvent_t* event);
static void onGetDeviceConfigCmd(IotCommEvent_t* event);
static void onSetDeviceConfigCmd(IotCommEvent_t* event);

static esp_err_t applyNewConfiguration(const Config_t* config);

static esp_err_t getDefaultHostname(char hostname[MAX_HOSTNAME_LEN + 1]);

static void registerMdnsHttpService();
static void unregisterMdnsHttpService();

static void onboardLedTimer(TimerHandle_t xTimer);

static void resetButtonPressedHandler(void* ctx);

static void onIrCodeReceived(uint32_t code, void* ctx);
static bool getRemoteControlButtonColor(uint32_t button, RGBColor_t* color);
static uint32_t getRemoteControlButtonFromIrCode(uint32_t irCode);

static void normalizeRgbToMaxValue(RGBColor_t* color, uint16_t maxValue);
static void adjustRgbChannelBias(uint16_t* channel, bool increase);
static void handleRemoteControlButton(uint32_t button);

// -----------------------------------------------------------------------------

extern "C" void app_main()
{
    Task_t setupTaskHandle;
    bool mustReboot;

    ESP_ERROR_CHECK(otaVerifyAndConfirmNewFirmware(
        [](void*) -> bool
        {
            return true;
        },
        nullptr, &mustReboot));
    if (mustReboot)
    {
        ESP_LOGI(TAG, "Rebooting due to firmware upgrade...");

        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    }

    setupTaskHandle = taskCreate(setupTask, "setupTask", 5120, nullptr, 1, tskNO_AFFINITY);
    ESP_ERROR_CHECK(setupTaskHandle ? ESP_OK : ESP_ERR_NO_MEM);
}

static void setupTask(Task_t task, void*)
{
    Config_t config;
    IotCommConfig_t iotCommConfig;
    WifiMgrConfig_t wifiConfig;
    TimerHandle_t onboardLedTimerHandle;
    uint8_t mac[6];

    if (ONBOARD_LED_GPIO_PIN != GPIO_NUM_NC)
    {
        ESP_ERROR_CHECK(gpioPinManagerClaim(ONBOARD_LED_GPIO_PIN, GpioPinUseOutput, "onboard LED"));
        onboardLedInit(ONBOARD_LED_GPIO_PIN);
    }
    if (RESET_BUTTON_GPIO_PIN != GPIO_NUM_NC)
    {
        ESP_ERROR_CHECK(gpioPinManagerClaim(RESET_BUTTON_GPIO_PIN, GpioPinUseInput, "reset button"));
        setupResetButton(RESET_BUTTON_GPIO_PIN, &resetButtonPressedHandler);
    }
    if (IR_RECEIVER_GPIO_PIN != GPIO_NUM_NC)
    {
        ESP_ERROR_CHECK(irReceiverInit(IR_RECEIVER_GPIO_PIN, &onIrCodeReceived, nullptr));
    }

    ESP_ERROR_CHECK(configLoad(&config));
    ESP_ERROR_CHECK(outputControllerInit(&config.output));

    iotCommConfig = iotCommDefaultConfig();
    iotCommConfig.handler = iotCommEventHandler;
    iotCommConfig.storage.load = loadFromStorage;
    iotCommConfig.storage.save = saveToStorage;
    ESP_ERROR_CHECK(iotCommInit(&iotCommConfig));

    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    snprintf(provisioningSSID, sizeof(provisioningSSID), "al-node-network-%02x%02x", mac[4], mac[5]);

    memset(&wifiConfig, 0, sizeof(wifiConfig));
    wifiConfig.handler = wifiMgrEventHandler;
    wifiConfig.maxWifiPower = 8.5f;
    wifiConfig.staConnectTimeoutMs = 60 * 1000; // 1 minute
    wifiConfig.recoveryPortalTimeoutMs = 5 * 60 * 1000; // 5 minutes
    ESP_ERROR_CHECK(wifiMgrInit(&wifiConfig));
    ESP_ERROR_CHECK(iotCommIsRootUserConfigured(&provisioningRootUserConfigured));
    if (!provisioningRootUserConfigured)
    {
        startProvisioning();
    }

    if (ONBOARD_LED_GPIO_PIN != GPIO_NUM_NC)
    {
        onboardLedTimerHandle = xTimerCreate("onboardLedTimer", pdMS_TO_TICKS(500), pdTRUE, nullptr, onboardLedTimer);
        xTimerStart(onboardLedTimerHandle, 0);
    }

    taskDetach(task);
    vTaskDelete(nullptr);
}

static void wifiMgrEventHandler(WifiMgrEvent_t event, void*)
{
    switch (event)
    {
        case WifiMgrEventConnected:
            {
                AutoRundownProtection rpResetLock(rpReset);

                if (rpResetLock.acquired())
                {
                    commStart();
                }
            }
            break;

        case WifiMgrEventDisconnected:
            {
                AutoRundownProtection rpResetLock(rpReset);

                if (rpResetLock.acquired())
                {
                    commStop();
                }
            }
            break;

        case WifiMgrEventProvisioningRequired:
            {
                AutoRundownProtection rpResetLock(rpReset);

                if (rpResetLock.acquired())
                {
                    startProvisioning();
                }
            }
            break;

        case WifiMgrEventAuthenticationFailed:
        case WifiMgrEventProvisioningStarted:
        case WifiMgrEventProvisioningStopped:
            break;
    }
}

static void startProvisioning()
{
    WifiMgrSoftApConfig_t provisioningApConfig = {};
    WifiMgrProvisioningHandlerConfig_t provisioningHandlerConfig = {};
    esp_err_t err;

    if (wifiMgrIsProvisioningActive())
    {
        ESP_LOGW(TAG, "Provisioning request ignored; provisioning is already active or transitioning.");
        return;
    }

    err = iotCommIsRootUserConfigured(&provisioningRootUserConfigured);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to determine root-user provisioning state. Error: %d.", err);
        return;
    }

    provisioningApConfig.ssid = provisioningSSID;
    provisioningHandlerConfig.start = [](void*) -> esp_err_t
    {
        CaptivePortalConfig_t captivePortalConfig = {};

        captivePortalConfig.handler =
            provisioningRootUserConfigured ? captivePortalRecoveryCredentialsHandler : captivePortalCredentialsHandler;
        captivePortalConfig.requestWifiCredentials = true;
        captivePortalConfig.setupRootUser = !provisioningRootUserConfigured;
        captivePortalConfig.setupDeviceHostname = !provisioningRootUserConfigured;
        if (provisioningRootUserConfigured)
        {
            captivePortalConfig.rootAuthorization = [](const uint8_t hash[P256_HASH_SIZE], const uint8_t signature[P256_SIGNATURE_SIZE],
                                                       void*) -> esp_err_t
            {
                return iotCommVerifyRootUserSignature(hash, signature);
            };
            captivePortalConfig.requireRootAuthorization = true;
        }
        return capPortalInit(&captivePortalConfig);
    };
    provisioningHandlerConfig.stop = [](void*) -> void
    {
        capPortalDeinit();
    };
    provisioningHandlerConfig.httpReq = [](httpd_req_t* req, void*) -> esp_err_t
    {
        return capPortalHandleRequest(req);
    };

    err = provisioningRootUserConfigured ? wifiMgrStartRecoveryProvisioning(&provisioningApConfig, &provisioningHandlerConfig)
                                         : wifiMgrStartProvisioning(&provisioningApConfig, &provisioningHandlerConfig);
    if (err == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "Provisioning request ignored; provisioning is already active or transitioning.");
        return;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to start provisioning. Error: %d.", err);
    }
}

static esp_err_t captivePortalCredentialsHandler(CaptivePortalProvisioningConfig_t* creds, void*)
{
    esp_err_t err;

    err = iotCommInitRootUserPublicKey(creds->rootUserPublicKey);
    if (err == ESP_OK)
    {
        char hostname[MAX_HOSTNAME_LEN + 1];
        const char* effectiveHostname;

        if (creds->hostname[0] != 0)
        {
            effectiveHostname = creds->hostname;
        }
        else
        {
            err = getDefaultHostname(hostname);
            effectiveHostname = hostname;
        }
        if (err == ESP_OK)
        {
            err = wifiMgrSetHostname(effectiveHostname);
        }
    }
    if (err == ESP_OK)
    {
        err = wifiMgrStoreSTA(creds->wifiSSID, creds->wifiPassword);
    }
    if (err == ESP_OK)
    {
        err = wifiMgrStartSTA();
    }
    return err;
}

static esp_err_t captivePortalRecoveryCredentialsHandler(CaptivePortalProvisioningConfig_t* creds, void*)
{
    esp_err_t err;

    err = wifiMgrStoreSTA(creds->wifiSSID, creds->wifiPassword);
    if (err == ESP_OK)
    {
        err = wifiMgrStartSTA();
    }
    return err;
}

static void iotCommEventHandler(IotCommEvent_t* event)
{
    AutoRundownProtection rpResetLock(rpReset);

    if (!rpResetLock.acquired())
    {
        return;
    }

    switch (event->eventType)
    {
        case IotCommEventTypeSessionStart:
            {
                bool restoreOutput = false;

                {
                    AutoMutex lock(appStateMtx);

                    if (powerIsOff)
                    {
                        currentColor.h = 0;
                        currentColor.s = 0;
                        currentColor.v = 0;
                        powerIsOff = false;
                        restoreOutput = true;
                    }
                }

                if (restoreOutput)
                {
                    // Session start intentionally re-enables output in a known black state.
                    (void)outputControllerSet(0, 0, 0);
                }
            }
            break;

        case IotCommEventTypeSessionEnd:
            break;

        case IotCommEventTypeCustomCommand:
            onCustomCommand(event);
            break;
    }
}

static esp_err_t loadFromStorage(IotCommStorageItemType_t itemType, void* dest, size_t destLen, void*)
{
    lightstd::vector<uint8_t> data;
    esp_err_t err;

    switch (itemType)
    {
        case IotCommStorageItemTypeUsers:
            err = storage.readBlob("users", data);
            break;

        case IotCommStorageItemTypeDeviceIdentityKeyPair:
            err = storage.readBlob("deviceID", data);
            break;

        default:
            return ESP_FAIL;
    }
    if (err != ESP_OK)
    {
        return err;
    }
    if (data.size() != destLen)
    {
        return ESP_FAIL;
    }

    memcpy(dest, data.data(), destLen);
    return ESP_OK;
}

static esp_err_t saveToStorage(IotCommStorageItemType_t itemType, const void* data, size_t dataLen, void*)
{
    esp_err_t err;

    switch (itemType)
    {
        case IotCommStorageItemTypeUsers:
            err = storage.writeBlob("users", data, dataLen);
            if (err != ESP_OK)
            {
                return err;
            }
            return storage.commit();

        case IotCommStorageItemTypeDeviceIdentityKeyPair:
            err = storage.writeBlob("deviceID", data, dataLen);
            if (err != ESP_OK)
            {
                return err;
            }
            return storage.commit();

        default:
            return ESP_FAIL;
    }
}

static void commStart()
{
    IotCommServerConfig_t iotCommServerConfig;
    esp_err_t err;

    iotCommServerConfig = iotCommDefaultServerConfig();
    iotCommServerConfig.udpListenPort = 32888;
    iotCommServerConfig.maxPacketSize = 4096;
    err = iotCommStartServer(&iotCommServerConfig);
    if (err != ESP_OK)
    {
        unregisterMdnsHttpService();
        return;
    }

    registerMdnsHttpService();
}

static void commStop()
{
    unregisterMdnsHttpService();
    iotCommStopServer();
}

static bool commIsActive()
{
    return iotCommIsServerRunning();
}

static void onCustomCommand(IotCommEvent_t* event)
{
    switch (event->command->transportType)
    {
        case IotCommTransportTypeWebSocket:
            switch (event->command->cmd)
            {
                case CMD_SET_COLOR:
                    onSetColorCmd(event);
                    return;

                case CMD_FADE_TO_COLOR:
                    onFadeToColorCmd(event);
                    return;

                case CMD_SET_AND_FADE_TO_COLOR:
                    onSetAndFadeToColorCmd(event);
                    return;

                case CMD_SET_RAW_DMX_CHANNEL_DATA:
                    onSetDmxChannelDataCmd(event);
                    return;

                case CMD_SET_MDNS_HOSTNAME:
                    onSetMDnsHostName(event);
                    return;

                case CMD_GET_DEVICE_CONFIG:
                    onGetDeviceConfigCmd(event);
                    return;

                case CMD_SET_DEVICE_CONFIG:
                    onSetDeviceConfigCmd(event);
                    return;

                default:
                    break;
            }
            break;

        case IotCommTransportTypeUDP:
            switch (event->command->cmd)
            {
                case CMD_SET_COLOR:
                    onSetColorCmd(event);
                    return;

                case CMD_FADE_TO_COLOR:
                    onFadeToColorCmd(event);
                    return;

                case CMD_SET_AND_FADE_TO_COLOR:
                    onSetAndFadeToColorCmd(event);
                    return;

                case CMD_SET_RAW_DMX_CHANNEL_DATA:
                    onSetDmxChannelDataCmd(event);
                    return;

                default:
                    break;
            }
            break;

        case IotCommTransportTypeUnknown:
            break;
    }

    iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "Invalid command");
}

static void onSetColorCmd(IotCommEvent_t* event)
{
    binary_reader_t br;
    uint16_t r, g, b;

    br = br_init(event->command->data, event->command->dataLen);
    if (!(br_read_be16(&br, &r) && br_read_be16(&br, &g) && br_read_be16(&br, &b)))
    {
        ESP_LOGD(TAG, "SET COLOR command: Invalid packet.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET COLOR command: Invalid packet");
        return;
    }

    ESP_LOGD(TAG, "SET COLOR command: R:%d, G:%d, B:%d.", static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));

    {
        AutoMutex lock(appStateMtx);

        if (fixedColorActive)
        {
            return;
        }

        currentColor = rgbToHsv(r, g, b);
        if (powerIsOff)
        {
            return;
        }
    }

    // Transport-driven color changes are best-effort; queue/runtime errors are intentionally ignored here.
    (void)outputControllerSet(r, g, b);
}

static void onFadeToColorCmd(IotCommEvent_t* event)
{
    binary_reader_t br;
    uint16_t r, g, b;
    uint16_t ms;

    br = br_init(event->command->data, event->command->dataLen);
    if (!(br_read_be16(&br, &r) && br_read_be16(&br, &g) && br_read_be16(&br, &b) && br_read_be16(&br, &ms)))
    {
        ESP_LOGD(TAG, "FADE TO COLOR command: Invalid packet.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET COLOR command: Invalid packet");
        return;
    }

    ESP_LOGD(TAG, "FADE TO COLOR command: R:%d, G:%d, B:%d (%dms).", static_cast<int>(r), static_cast<int>(g), static_cast<int>(b),
             static_cast<int>(ms));

    {
        AutoMutex lock(appStateMtx);

        if (fixedColorActive)
        {
            return;
        }

        currentColor = rgbToHsv(r, g, b);
        if (powerIsOff)
        {
            return;
        }
    }

    // Transport-driven color changes are best-effort; queue/runtime errors are intentionally ignored here.
    (void)outputControllerFadeTo(r, g, b, static_cast<uint32_t>(ms));
}

static void onSetAndFadeToColorCmd(IotCommEvent_t* event)
{
    binary_reader_t br;
    uint16_t r, g, b, rEnd, gEnd, bEnd;
    uint16_t ms;

    br = br_init(event->command->data, event->command->dataLen);
    if (!(br_read_be16(&br, &r) && br_read_be16(&br, &g) && br_read_be16(&br, &b) && br_read_be16(&br, &rEnd) && br_read_be16(&br, &gEnd) &&
          br_read_be16(&br, &bEnd) && br_read_be16(&br, &ms)))
    {
        ESP_LOGD(TAG, "SET AND FADE TO COLOR command: Invalid packet.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET COLOR command: Invalid packet");
        return;
    }

    ESP_LOGD(TAG, "SET AND FADE TO COLOR command: R:%u, G:%u, B:%u to R:%u, G:%u, B:%u (%ums).", r, g, b, rEnd, gEnd, bEnd, ms);

    {
        AutoMutex lock(appStateMtx);

        if (fixedColorActive)
        {
            return;
        }

        currentColor = rgbToHsv(rEnd, gEnd, bEnd);
        if (powerIsOff)
        {
            return;
        }
    }

    // Transport-driven color changes are best-effort; queue/runtime errors are intentionally ignored here.
    (void)outputControllerSetAndFadeTo(r, g, b, rEnd, gEnd, bEnd, static_cast<uint32_t>(ms));
}

static void onSetDmxChannelDataCmd(IotCommEvent_t* event)
{
    binary_reader_t br;
    uint16_t startChannel;
    uint16_t dataLen;
    const uint8_t* data;
    esp_err_t err;

    if (!outputControllerSupportsDmxChannelData())
    {
        return;
    }

    br = br_init(event->command->data, event->command->dataLen);
    if (!(br_read_be16(&br, &startChannel) && br_read_be16(&br, &dataLen) && br_read_blob(&br, dataLen, &data) && br.len == 0))
    {
        ESP_LOGD(TAG, "SET RAW DMX CHANNEL DATA command: Invalid packet.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET RAW DMX CHANNEL DATA command: Invalid packet");
        return;
    }
    if (dataLen == 0 || dataLen > DMX_MAX_CHANNELS || startChannel == 0 || startChannel > DMX_MAX_CHANNELS - dataLen + 1)
    {
        ESP_LOGD(TAG, "SET RAW DMX CHANNEL DATA command: Invalid channel range.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET RAW DMX CHANNEL DATA command: Invalid channel range");
        return;
    }

    {
        AutoMutex lock(appStateMtx);

        if (fixedColorActive || powerIsOff)
        {
            return;
        }
    }

    err = outputControllerSetDmxChannels(startChannel, data, dataLen);
    if (err != ESP_OK)
    {
        return;
    }
}

static void onSetMDnsHostName(IotCommEvent_t* event)
{
    binary_reader_t br;
    const char* name;
    size_t nameLen;
    esp_err_t err;

    if (!iotCommIsSessionUserAdmin(event->sessionHandle))
    {
        ESP_LOGD(TAG, "SET MDNS HOSTNAME command: Insufficient privileges.");
        iotCommEventReplyWithError(event->sessionHandle, ESP_ERR_NOT_ALLOWED, "Insufficient privileges");
        return;
    }

    br = br_init(event->command->data, event->command->dataLen);
    if ((!br_read_str(&br, &name, &nameLen)) || br.len != 0)
    {
        ESP_LOGD(TAG, "SET MDNS HOSTNAME command: Invalid packet.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET MDNS HOSTNAME command: Invalid packet");
        return;
    }

    err = wifiMgrSetHostname(name);
    if (err != ESP_OK)
    {
        iotCommEventReplyWithError(event->sessionHandle, err, "Unable to set new hostname");
        return;
    }

    iotCommEventReplyWithError(event->sessionHandle, ESP_OK, nullptr);
}

static void onGetDeviceConfigCmd(IotCommEvent_t* event)
{
    Config_t config;
    uint8_t* reply;
    uint32_t deviceCommandFlags;
    size_t replySize;
    binary_writer_t bw;
    esp_err_t err;

    if (event->command->dataLen != 0)
    {
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "GET DEVICE CONFIG command: Invalid packet");
        return;
    }

    err = configLoad(&config);
    if (err != ESP_OK)
    {
        iotCommEventReplyWithError(event->sessionHandle, err, "Unable to load device configuration");
        return;
    }

    deviceCommandFlags = DEVICE_CMD_FLAG_SET_COLOR | DEVICE_CMD_FLAG_FADE_TO_COLOR | DEVICE_CMD_FLAG_SET_AND_FADE |
                         DEVICE_CMD_FLAG_GET_CONFIG | DEVICE_CMD_FLAG_SET_CONFIG;

    if (config.output.type == OutputControllerTypeDmx)
    {
        deviceCommandFlags |= DEVICE_CMD_FLAG_SET_DMX_CHANNEL;
    }

    replySize = sizeof(uint8_t) + sizeof(uint32_t) + configCalcSize(&config);
    reply = static_cast<uint8_t*>(malloc(replySize));
    if (!reply)
    {
        iotCommEventReplyWithError(event->sessionHandle, ESP_ERR_NO_MEM, "Unable to allocate device config buffer");
        return;
    }

    bw = bw_init(reply, replySize);

    if ((!bw_write_byte(&bw, DEVICE_CONFIG_REPLY_VERSION)) || (!bw_write_be32(&bw, deviceCommandFlags)) || (!configWrite(&bw, &config)))
    {
        free(reply);
        iotCommEventReplyWithError(event->sessionHandle, ESP_ERR_NO_MEM, "Unable to encode device config");
        return;
    }

    iotCommEventReply(event->sessionHandle, reply, bw.written);
    free(reply);
}

static void onSetDeviceConfigCmd(IotCommEvent_t* event)
{
    Config_t config;
    binary_reader_t br;
    esp_err_t err;

    if (!iotCommIsSessionUserAdmin(event->sessionHandle))
    {
        ESP_LOGD(TAG, "SET DEVICE CONFIG command: Insufficient privileges.");
        iotCommEventReplyWithError(event->sessionHandle, ESP_ERR_NOT_ALLOWED, "Insufficient privileges");
        return;
    }

    br = br_init(event->command->data, event->command->dataLen);
    err = configRead(&br, &config);
    if (err == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGD(TAG, "SET DEVICE CONFIG command: Invalid configuration.");
        iotCommEventReplyWithError(event->sessionHandle, err, "Invalid configuration");
        return;
    }
    else if (err != ESP_OK || br.len != 0)
    {
        ESP_LOGD(TAG, "SET DEVICE CONFIG command: Invalid packet.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET DEVICE CONFIG command: Invalid packet");
        return;
    }

    err = configSave(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SET DEVICE CONFIG command: Unable to save configuration. Error: %d.", err);
        iotCommEventReplyWithError(event->sessionHandle, err, "Unable to save device configuration");
        return;
    }

    err = applyNewConfiguration(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SET DEVICE CONFIG command: Unable to apply configuration. Error: %d.", err);
        iotCommEventReplyWithError(event->sessionHandle, err, "Unable to apply device configuration");
        return;
    }

    {
        AutoMutex lock(appStateMtx);

        fixedColorActive = false;
        powerIsOff = false;
        currentColor = {0, 0, 0xFFFF};
    }
    iotCommEventReplyWithError(event->sessionHandle, ESP_OK, nullptr);
}

static esp_err_t applyNewConfiguration(const Config_t* config)
{
    esp_err_t err;

    assert(config);

    outputControllerDeinit();
    err = outputControllerInit(&config->output);

    return err;
}

static esp_err_t getDefaultHostname(char hostname[MAX_HOSTNAME_LEN + 1])
{
    Sha256Context_t sha256Ctx;
    uint8_t mac[6];
    uint8_t hash[32];
    esp_err_t err;

    hostname[0] = 0;

    err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK)
    {
        return err;
    }

    sha256Init(&sha256Ctx);
    err = sha256Start(&sha256Ctx);
    if (err == ESP_OK)
    {
        err = sha256Update(&sha256Ctx, mac, sizeof(mac));
        if (err == ESP_OK)
        {
            err = sha256Finish(&sha256Ctx, hash);
        }
    }
    sha256Done(&sha256Ctx);
    if (err != ESP_OK)
    {
        return err;
    }

    snprintf(hostname, MAX_HOSTNAME_LEN + 1, "node-%02x%02x%02x", hash[0], hash[1], hash[2]);
    return ESP_OK;
}

static void registerMdnsHttpService()
{
    mdns_txt_item_t txt[2] = {{"class", "lighting"}, {"subclass", outputControllerGetTypeName(outputControllerGetType())}};

    if ((!commIsActive()) || mdnsHttpServiceRegistered)
    {
        return;
    }

    if (wifiMgrMdnsServiceAdd(nullptr, "_http", "_tcp", 80, txt, 2) == ESP_OK)
    {
        mdnsHttpServiceRegistered = true;
    }
    else
    {
        ESP_LOGW(TAG, "Unable to register mDNS HTTP service.");
    }
}

static void unregisterMdnsHttpService()
{
    if (mdnsHttpServiceRegistered)
    {
        wifiMgrMdnsServiceRemove("_http", "_tcp");
        mdnsHttpServiceRegistered = false;
    }
}

static void onboardLedTimer(TimerHandle_t)
{
    AutoRundownProtection rpResetLock(rpReset);

    if (rpResetLock.acquired())
    {
        onboardLedTimerCounter = (onboardLedTimerCounter + 1) & 1;
        if (commIsActive())
        {
            onboardLedSet(true);
        }
        else
        {
            onboardLedSet(!!(onboardLedTimerCounter == 1));
        }
    }
}

static void resetButtonPressedHandler(void*)
{
    runOnce(
        &resetOnce,
        [](void*) -> void
        {
            rundownProtWait(&rpReset);

            commStop();
            iotCommDeinit();
            wifiMgrDeinit();
            irReceiverDeinit();
            outputControllerDeinit();

            storage.eraseAll();

            ESP_LOGI(TAG, "Rebooting...");

            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
        },
        nullptr);
}

static bool getRemoteControlButtonColor(uint32_t button, RGBColor_t* color)
{
    assert(color);

    switch (button)
    {
        case REMOTE_CONTROL_BTN_RED:
            color->r = 0xFFFF;
            color->g = 0x0000;
            color->b = 0x0000;
            return true;

        case REMOTE_CONTROL_BTN_GREEN:
            color->r = 0x0000;
            color->g = 0xFFFF;
            color->b = 0x0000;
            return true;

        case REMOTE_CONTROL_BTN_BLUE:
            color->r = 0x0000;
            color->g = 0x0000;
            color->b = 0xFFFF;
            return true;

        case REMOTE_CONTROL_BTN_WHITE:
            color->r = 0xFFFF;
            color->g = 0xFFFF;
            color->b = 0xFFFF;
            return true;

        case REMOTE_CONTROL_BTN_RED_ORANGE:
            color->r = 0xFFFF;
            color->g = 0x4040;
            color->b = 0x0000;
            return true;

        case REMOTE_CONTROL_BTN_LIGHT_GREEN:
            color->r = 0x4040;
            color->g = 0xFFFF;
            color->b = 0x4040;
            return true;

        case REMOTE_CONTROL_BTN_LIGHT_BLUE:
            color->r = 0x4040;
            color->g = 0x4040;
            color->b = 0xFFFF;
            return true;

        case REMOTE_CONTROL_BTN_LIGHT_PINK:
            color->r = 0xFFFF;
            color->g = 0x4040;
            color->b = 0x4040;
            return true;

        case REMOTE_CONTROL_BTN_ORANGE:
            color->r = 0xFFFF;
            color->g = 0x8080;
            color->b = 0x0000;
            return true;

        case REMOTE_CONTROL_BTN_SKY_BLUE:
            color->r = 0x8787;
            color->g = 0xCECE;
            color->b = 0xEBEB;
            return true;

        case REMOTE_CONTROL_BTN_VIOLET:
            color->r = 0x8080;
            color->g = 0x0000;
            color->b = 0x8080;
            return true;

        case REMOTE_CONTROL_BTN_LIGHT_PINK_2:
            color->r = 0xFFFF;
            color->g = 0x4040;
            color->b = 0x4040;
            return true;

        case REMOTE_CONTROL_BTN_YELLOW_ORANGE:
            color->r = 0xFFFF;
            color->g = 0xBFBF;
            color->b = 0x0000;
            return true;

        case REMOTE_CONTROL_BTN_TURQOISE:
            color->r = 0x4040;
            color->g = 0xE0E0;
            color->b = 0xD0D0;
            return true;

        case REMOTE_CONTROL_BTN_DARK_ORCHID:
            color->r = 0xC0C0;
            color->g = 0x3535;
            color->b = 0x9A9A;
            return true;

        case REMOTE_CONTROL_BTN_CYAN:
            color->r = 0x0000;
            color->g = 0xFFFF;
            color->b = 0xFFFF;
            return true;

        case REMOTE_CONTROL_BTN_YELLOW:
            color->r = 0xFFFF;
            color->g = 0xFFFF;
            color->b = 0x0000;
            return true;

        case REMOTE_CONTROL_BTN_DARK_CYAN:
            color->r = 0x0000;
            color->g = 0x8B8B;
            color->b = 0x8B8B;
            return true;

        case REMOTE_CONTROL_BTN_HOT_PINK:
            color->r = 0xFFFF;
            color->g = 0x6969;
            color->b = 0xB4B4;
            return true;

        case REMOTE_CONTROL_BTN_CYAN_2:
            color->r = 0x0000;
            color->g = 0xFFFF;
            color->b = 0xFFFF;
            return true;

        default:
            return false;
    }
}

static void normalizeRgbToMaxValue(RGBColor_t* color, uint16_t maxValue)
{
    uint16_t currentMax;

    if (!color)
    {
        return;
    }

    currentMax = color->r;
    if (color->g > currentMax)
    {
        currentMax = color->g;
    }
    if (color->b > currentMax)
    {
        currentMax = color->b;
    }

    if (currentMax == 0 || currentMax <= maxValue)
    {
        return;
    }

    color->r = static_cast<uint16_t>((static_cast<uint32_t>(color->r) * maxValue) / currentMax);
    color->g = static_cast<uint16_t>((static_cast<uint32_t>(color->g) * maxValue) / currentMax);
    color->b = static_cast<uint16_t>((static_cast<uint32_t>(color->b) * maxValue) / currentMax);
}

static void adjustRgbChannelBias(uint16_t* channel, bool increase)
{
    if (!channel)
    {
        return;
    }

    if (increase)
    {
        *channel = (*channel < 0xF000U) ? static_cast<uint16_t>(*channel + 0x1000U) : 0xFFFFU;
    }
    else
    {
        *channel = (*channel > 0x1000U) ? static_cast<uint16_t>(*channel - 0x1000U) : 0U;
    }
}

static void handleRemoteControlButton(uint32_t button)
{
    RGBColor_t color;
    bool applyOutput = false;

    if (getRemoteControlButtonColor(button, &color))
    {
        {
            AutoMutex lock(appStateMtx);

            fixedColorActive = true;
            currentColor = rgbToHsv(color.r, color.g, color.b);
            applyOutput = !powerIsOff;
        }

        if (applyOutput)
        {
            // IR color override is best-effort; queue/runtime errors are intentionally ignored here.
            (void)outputControllerSet(color.r, color.g, color.b);
        }
        ESP_LOGD(TAG, "Applied fixed IR color for button %" PRIu32 ".", button);
        return;
    }

    switch (button)
    {
        case REMOTE_CONTROL_AUTO_MODE:
            {
                AutoMutex lock(appStateMtx);

                fixedColorActive = false;
            }
            ESP_LOGD(TAG, "IR AUTO mode enabled; communication color commands accepted again.");
            return;

        case REMOTE_CONTROL_BTN_POWER_ON:
            {
                {
                    AutoMutex lock(appStateMtx);

                    if (!powerIsOff)
                    {
                        powerIsOff = true;
                        color = {0, 0, 0};
                    }
                    else
                    {
                        powerIsOff = false;
                        if (currentColor.v < 0x1000)
                        {
                            currentColor.h = currentColor.s = 0;
                            currentColor.v = 0xFFFF;
                            ESP_LOGD(TAG, "IR power button restored output with full white because saved color was too dark.");
                        }
                        else
                        {
                            ESP_LOGD(TAG, "IR power button restored saved current color.");
                        }

                        hsvToRgb(&currentColor, &color.r, &color.g, &color.b);
                    }
                    applyOutput = true;
                }

                // Power changes are best-effort; queue/runtime errors are intentionally ignored here.
                (void)outputControllerSet(color.r, color.g, color.b);
                ESP_LOGD(TAG, "IR power button toggled output state.");
            }
            return;

        case REMOTE_CONTROL_BTN_BRIGHT_UP:
            {
                {
                    AutoMutex lock(appStateMtx);

                    if (fixedColorActive && (!powerIsOff))
                    {
                        hsvLighter(&currentColor, 1000);
                        hsvToRgb(&currentColor, &color.r, &color.g, &color.b);
                        applyOutput = true;
                    }
                }

                if (applyOutput)
                {
                    (void)outputControllerSet(color.r, color.g, color.b);
                    ESP_LOGD(TAG, "IR brightness increased current fixed color.");
                }
            }
            return;

        case REMOTE_CONTROL_BTN_BRIGHT_DOWN:
            {
                {
                    AutoMutex lock(appStateMtx);

                    if (fixedColorActive && (!powerIsOff))
                    {
                        hsvDarker(&currentColor, 1000);
                        hsvToRgb(&currentColor, &color.r, &color.g, &color.b);
                        applyOutput = true;
                    }
                }

                if (applyOutput)
                {
                    (void)outputControllerSet(color.r, color.g, color.b);
                    ESP_LOGD(TAG, "IR brightness decreased current fixed color.");
                }
            }
            return;

        case REMOTE_CONTROL_BTN_RED_BRIGHT_UP:
            {
                {
                    AutoMutex lock(appStateMtx);

                    if (fixedColorActive && (!powerIsOff))
                    {
                        uint16_t originalMax;

                        hsvToRgb(&currentColor, &color.r, &color.g, &color.b);
                        originalMax = currentColor.v;
                        adjustRgbChannelBias(&color.r, true);
                        normalizeRgbToMaxValue(&color, originalMax);
                        currentColor = rgbToHsv(color.r, color.g, color.b);
                        applyOutput = true;
                    }
                }

                if (applyOutput)
                {
                    (void)outputControllerSet(color.r, color.g, color.b);
                    ESP_LOGD(TAG, "IR red channel bias increased.");
                }
            }
            return;

        case REMOTE_CONTROL_BTN_RED_BRIGHT_DOWN:
            {
                {
                    AutoMutex lock(appStateMtx);

                    if (fixedColorActive && (!powerIsOff))
                    {
                        hsvToRgb(&currentColor, &color.r, &color.g, &color.b);
                        adjustRgbChannelBias(&color.r, false);
                        currentColor = rgbToHsv(color.r, color.g, color.b);
                        applyOutput = true;
                    }
                }

                if (applyOutput)
                {
                    (void)outputControllerSet(color.r, color.g, color.b);
                    ESP_LOGD(TAG, "IR red channel bias decreased.");
                }
            }
            return;

        case REMOTE_CONTROL_BTN_GREEN_BRIGHT_UP:
            {
                {
                    AutoMutex lock(appStateMtx);

                    if (fixedColorActive && (!powerIsOff))
                    {
                        uint16_t originalMax;

                        hsvToRgb(&currentColor, &color.r, &color.g, &color.b);
                        originalMax = currentColor.v;
                        adjustRgbChannelBias(&color.g, true);
                        normalizeRgbToMaxValue(&color, originalMax);
                        currentColor = rgbToHsv(color.r, color.g, color.b);
                        applyOutput = true;
                    }
                }

                if (applyOutput)
                {
                    (void)outputControllerSet(color.r, color.g, color.b);
                    ESP_LOGD(TAG, "IR green channel bias increased.");
                }
            }
            return;

        case REMOTE_CONTROL_BTN_GREEN_BRIGHT_DOWN:
            {
                {
                    AutoMutex lock(appStateMtx);

                    if (fixedColorActive && (!powerIsOff))
                    {
                        hsvToRgb(&currentColor, &color.r, &color.g, &color.b);
                        adjustRgbChannelBias(&color.g, false);
                        currentColor = rgbToHsv(color.r, color.g, color.b);
                        applyOutput = true;
                    }
                }

                if (applyOutput)
                {
                    (void)outputControllerSet(color.r, color.g, color.b);
                    ESP_LOGD(TAG, "IR green channel bias decreased.");
                }
            }
            return;

        case REMOTE_CONTROL_BTN_BLUE_BRIGHT_UP:
            {
                {
                    AutoMutex lock(appStateMtx);

                    if (fixedColorActive && (!powerIsOff))
                    {
                        uint16_t originalMax;

                        hsvToRgb(&currentColor, &color.r, &color.g, &color.b);
                        originalMax = currentColor.v;
                        adjustRgbChannelBias(&color.b, true);
                        normalizeRgbToMaxValue(&color, originalMax);
                        currentColor = rgbToHsv(color.r, color.g, color.b);
                        applyOutput = true;
                    }
                }

                if (applyOutput)
                {
                    (void)outputControllerSet(color.r, color.g, color.b);
                    ESP_LOGD(TAG, "IR blue channel bias increased.");
                }
            }
            return;

        case REMOTE_CONTROL_BTN_BLUE_BRIGHT_DOWN:
            {
                {
                    AutoMutex lock(appStateMtx);

                    if (fixedColorActive && (!powerIsOff))
                    {
                        hsvToRgb(&currentColor, &color.r, &color.g, &color.b);
                        adjustRgbChannelBias(&color.b, false);
                        currentColor = rgbToHsv(color.r, color.g, color.b);
                        applyOutput = true;
                    }
                }

                if (applyOutput)
                {
                    (void)outputControllerSet(color.r, color.g, color.b);
                    ESP_LOGD(TAG, "IR blue channel bias decreased.");
                }
            }
            return;

        default:
            ESP_LOGD(TAG, "IR button %" PRIu32 " recognized but has no action yet.", button);
            return;
    }
}

static uint32_t getRemoteControlButtonFromIrCode(uint32_t irCode)
{
    switch (irCode)
    {
        case IR_REMOTE_CODE_BRIGHT_UP:
            return REMOTE_CONTROL_BTN_BRIGHT_UP;
        case IR_REMOTE_CODE_BRIGHT_DOWN:
            return REMOTE_CONTROL_BTN_BRIGHT_DOWN;
        case IR_REMOTE_CODE_POWER_ON:
            return REMOTE_CONTROL_BTN_POWER_ON;
        case IR_REMOTE_CODE_RED:
            return REMOTE_CONTROL_BTN_RED;
        case IR_REMOTE_CODE_GREEN:
            return REMOTE_CONTROL_BTN_GREEN;
        case IR_REMOTE_CODE_BLUE:
            return REMOTE_CONTROL_BTN_BLUE;
        case IR_REMOTE_CODE_WHITE:
            return REMOTE_CONTROL_BTN_WHITE;
        case IR_REMOTE_CODE_RED_ORANGE:
            return REMOTE_CONTROL_BTN_RED_ORANGE;
        case IR_REMOTE_CODE_LIGHT_GREEN:
            return REMOTE_CONTROL_BTN_LIGHT_GREEN;
        case IR_REMOTE_CODE_LIGHT_BLUE:
            return REMOTE_CONTROL_BTN_LIGHT_BLUE;
        case IR_REMOTE_CODE_LIGHT_PINK:
            return REMOTE_CONTROL_BTN_LIGHT_PINK;
        case IR_REMOTE_CODE_ORANGE:
            return REMOTE_CONTROL_BTN_ORANGE;
        case IR_REMOTE_CODE_SKY_BLUE:
            return REMOTE_CONTROL_BTN_SKY_BLUE;
        case IR_REMOTE_CODE_VIOLET:
            return REMOTE_CONTROL_BTN_VIOLET;
        case IR_REMOTE_CODE_LIGHT_PINK_2:
            return REMOTE_CONTROL_BTN_LIGHT_PINK_2;
        case IR_REMOTE_CODE_YELLOW_ORANGE:
            return REMOTE_CONTROL_BTN_YELLOW_ORANGE;
        case IR_REMOTE_CODE_TURQOISE:
            return REMOTE_CONTROL_BTN_TURQOISE;
        case IR_REMOTE_CODE_DARK_ORCHID:
            return REMOTE_CONTROL_BTN_DARK_ORCHID;
        case IR_REMOTE_CODE_CYAN:
            return REMOTE_CONTROL_BTN_CYAN;
        case IR_REMOTE_CODE_YELLOW:
            return REMOTE_CONTROL_BTN_YELLOW;
        case IR_REMOTE_CODE_DARK_CYAN:
            return REMOTE_CONTROL_BTN_DARK_CYAN;
        case IR_REMOTE_CODE_CYAN_2:
            return REMOTE_CONTROL_BTN_CYAN_2;
        case IR_REMOTE_CODE_RED_BRIGHT_UP:
            return REMOTE_CONTROL_BTN_RED_BRIGHT_UP;
        case IR_REMOTE_CODE_RED_BRIGHT_DOWN:
            return REMOTE_CONTROL_BTN_RED_BRIGHT_DOWN;
        case IR_REMOTE_CODE_GREEN_BRIGHT_UP:
            return REMOTE_CONTROL_BTN_GREEN_BRIGHT_UP;
        case IR_REMOTE_CODE_GREEN_BRIGHT_DOWN:
            return REMOTE_CONTROL_BTN_GREEN_BRIGHT_DOWN;
        case IR_REMOTE_CODE_BLUE_BRIGHT_UP:
            return REMOTE_CONTROL_BTN_BLUE_BRIGHT_UP;
        case IR_REMOTE_CODE_BLUE_BRIGHT_DOWN:
            return REMOTE_CONTROL_BTN_BLUE_BRIGHT_DOWN;
        case IR_REMOTE_CODE_AUTO_MODE:
            return REMOTE_CONTROL_AUTO_MODE;
        default:
            return 0;
    }
}

static void onIrCodeReceived(uint32_t code, void* ctx)
{
    AutoRundownProtection rpResetLock(rpReset);
    uint32_t button;

    static_cast<void>(ctx);

    if (!rpResetLock.acquired())
    {
        return;
    }

    button = getRemoteControlButtonFromIrCode(code);
    if (button == 0)
    {
        ESP_LOGD(TAG, "IR code received: 0x%08" PRIX32 " (unmapped).", code);
        return;
    }

    ESP_LOGD(TAG, "IR code received: 0x%08" PRIX32 " mapped to button %" PRIu32 ".", code, button);
    handleRemoteControlButton(button);
}
