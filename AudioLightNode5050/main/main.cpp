#include <esp_mac.h>
#include "iot_comm/utils/network.h"
#include "ledstrip/5050_controller.h"
#include "lightstd/string.h"
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <iot_comm/captive_portal/captive_portal.h>
#include <iot_comm/iot_comm.h>
#include <iot_comm/crypto/sha.h>
#include <iot_comm/ota/ota.h>
#include <iot_comm/provisioning/wifi.h>
#include <iot_comm/utils/binary.h>
#include <mdns.h>
#include <onboard_led.h>
#include <reset_button.h>
#include <rundown_protection.h>
#include <run_once.h>
#include <storage/nvs.h>

static const char* TAG = "MAIN";

#define ONBOARD_LED_GPIO_PIN GPIO_NUM_10

#define RESET_BUTTON_GPIO_PIN GPIO_NUM_2

#define RED_CHANNEL_GPIO_PIN   GPIO_NUM_3
#define GREEN_CHANNEL_GPIO_PIN GPIO_NUM_4
#define BLUE_CHANNEL_GPIO_PIN  GPIO_NUM_1

#define CMD_SET_COLOR             1
#define CMD_FADE_TO_COLOR         2
#define CMD_SET_AND_FADE_TO_COLOR 3
#define CMD_SET_MDNS_HOSTNAME     0x7000

// -----------------------------------------------------------------------------

static RundownProtection_t rpReset = RUNDOWN_PROTECTION_INIT_STATIC;
static RunOnce_t resetOnce = RUN_ONCE_INIT_STATIC;
static uint32_t volatile onboardLedTimerCounter = 0;
static NVSStorage storage;
static bool mdnsInitialized = false;
static bool mdnsHttpServiceRegistered = false;

// -----------------------------------------------------------------------------

static void setupTask(void *arg);

static void wifiMgrEventHandler(WifiMgrEvent_t event, void *ctx);

static esp_err_t captivePortalInitHandler(void *);
static void captivePortalDeinitHandler(void *);
static esp_err_t captivePortalHttpRequestHandler(httpd_req_t *req, void *);
static esp_err_t captivePortalProvisioningHandler(CaptivePortalProvisioningConfig_t *creds, void *ctx);

static void iotCommEventHandler(IotCommEvent_t *event);

static esp_err_t loadFromStorage(IotCommStorageItemType_t itemType, void *dest, size_t destLen, void *ctx);
static esp_err_t saveToStorage(IotCommStorageItemType_t itemType, const void *data, size_t dataLen, void *ctx);

static void commStart();
static void commStop();
static bool commIsActive();

static void onCustomCommand(IotCommEvent_t *event);
static void onSetColorCmd(IotCommEvent_t *event);
static void onFadeToColorCmd(IotCommEvent_t *event);
static void onSetAndFadeToColorCmd(IotCommEvent_t *event);
static void onSetMDnsHostName(IotCommEvent_t *event);

static esp_err_t getDefaultHostname(char hostname[MAX_HOSTNAME_LEN + 1]);

static bool ensureMdnsInitialized();
static void registerMdnsHttpService();
static void unregisterMdnsHttpService();
static esp_err_t setMdnsHostname(const char *hostname);

static void onboardLedTimer(TimerHandle_t xTimer);

static void resetButtonPressedHandler(void *ctx);

// -----------------------------------------------------------------------------

extern "C" void app_main()
{
    TaskHandle_t setupTaskHandle;
    bool mustReboot;

    ESP_ERROR_CHECK(otaVerifyAndConfirmNewFirmware([](void *ctx) -> bool
    {
        return true;
    }, nullptr, &mustReboot));
    if (mustReboot) {
        ESP_LOGI(TAG, "Rebooting due to firmware upgrade...");

        // Wait a bit and restart
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    }

    ESP_ERROR_CHECK((xTaskCreatePinnedToCore(setupTask, "setupTask", 4096, NULL, 1, &setupTaskHandle, 0) == pdPASS
                     ? ESP_OK : ESP_ERR_NO_MEM));
}

static void setupTask(void *arg)
{
    LedStrip5050Config_t ledStripConfig;
    IotCommConfig_t iotCommConfig;
    WifiMgrConfig_t wifiConfig;
    TimerHandle_t onboardLedTimerHandle;
    lightstd::string strHostname;

    onboardLedInit(ONBOARD_LED_GPIO_PIN);

    setupResetButton(RESET_BUTTON_GPIO_PIN, &resetButtonPressedHandler);

    ledStripConfig.redGpioPin = RED_CHANNEL_GPIO_PIN;
    ledStripConfig.greenGpioPin = GREEN_CHANNEL_GPIO_PIN;
    ledStripConfig.blueGpioPin = BLUE_CHANNEL_GPIO_PIN;
    ESP_ERROR_CHECK(ledStrip5050Init(&ledStripConfig));

    iotCommConfig = iotCommDefaultConfig();
    iotCommConfig.handler = iotCommEventHandler;
    iotCommConfig.storage.load = loadFromStorage;
    iotCommConfig.storage.save = saveToStorage;
    ESP_ERROR_CHECK(iotCommInit(&iotCommConfig));

    // Initialize Wi-Fi manager
    memset(&wifiConfig, 0, sizeof(wifiConfig));
    wifiConfig.handler = wifiMgrEventHandler;
    wifiConfig.softAP.ssid = "iotcomm-network";
    wifiConfig.softAP.captivePortal.init = captivePortalInitHandler;
    wifiConfig.softAP.captivePortal.deinit = captivePortalDeinitHandler;
    wifiConfig.softAP.captivePortal.httpReq = captivePortalHttpRequestHandler;
    ESP_ERROR_CHECK(wifiMgrInit(&wifiConfig));

    // Create onboard led controller
    onboardLedTimerHandle = xTimerCreate("onboardLedTimer", pdMS_TO_TICKS(500), pdTRUE, nullptr, onboardLedTimer);
    xTimerStart(onboardLedTimerHandle, 0);

    vTaskDelete(nullptr);
}

static void wifiMgrEventHandler(WifiMgrEvent_t event, void *ctx)
{
    switch (event) {
        case WifiMgrEventConnected:
            {
                AutoRundownProtection rpResetLock(rpReset);

                if (rpResetLock.acquired()) {
                    commStart();
                }
            }
            break;

        case WifiMgrEventDisconnected:
            {
                AutoRundownProtection rpResetLock(rpReset);

                if (rpResetLock.acquired()) {
                    commStop();
                }
            }
            break;

        case WifiMgrEventAuthenticationFailed:
            break;
    }
}

static esp_err_t captivePortalInitHandler(void *)
{
    CaptivePortalConfig_t capPortalConfig = {};

    capPortalConfig.handler = captivePortalProvisioningHandler;
    capPortalConfig.setupRootUser = true;
    capPortalConfig.setupDeviceHostname = true;
    return capPortalInit(&capPortalConfig);
}

static void captivePortalDeinitHandler(void *)
{
    capPortalDeinit();
}

static esp_err_t captivePortalHttpRequestHandler(httpd_req_t *req, void *)
{
    return capPortalHandleRequest(req);
}

static esp_err_t captivePortalProvisioningHandler(CaptivePortalProvisioningConfig_t *creds, void *ctx)
{
    esp_err_t err;

    err = iotCommInitRootUserPublicKey(creds->rootUserPublicKey);
    if (err == ESP_OK) {
        char hostname[MAX_HOSTNAME_LEN + 1];
        const char *effectiveHostname;

        if (creds->hostname[0] != 0) {
            effectiveHostname = creds->hostname;
        }
        else {
            err = getDefaultHostname(hostname);
            effectiveHostname = hostname;
        }
        if (err == ESP_OK) {
            err = wifiMgrSetHostname(effectiveHostname);
            if (err == ESP_OK) {
                err = setMdnsHostname(effectiveHostname);
            }
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Unable to configure hostname during provisioning; continuing.");
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = wifiMgrStoreSTA(creds->wifiSSID, creds->wifiPassword);
    }
    if (err == ESP_OK) {
        err = wifiMgrStartSTA();
    }
    return err;
}

static void iotCommEventHandler(IotCommEvent_t *event)
{
    switch (event->eventType) {
        case IotCommEventTypeSessionStart:
            break;
        case IotCommEventTypeSessionEnd:
            break;
        case IotCommEventTypeCustomCommand:
            onCustomCommand(event);
            break;
    }
}

static esp_err_t loadFromStorage(IotCommStorageItemType_t itemType, void *dest, size_t destLen, void *ctx)
{
    lightstd::vector<uint8_t> data;
    esp_err_t err;

    switch (itemType) {
        case IotCommStorageItemTypeUsers:
            err = storage.readBlob("users", data);
            break;
        case IotCommStorageItemTypeDeviceIdentityKeyPair:
            err = storage.readBlob("deviceID", data);
            break;
        default:
            return ESP_FAIL;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (data.size() != destLen) {
        return ESP_FAIL;
    }

    memcpy(dest, data.data(), destLen);
    return ESP_OK;
}

static esp_err_t saveToStorage(IotCommStorageItemType_t itemType, const void *data, size_t dataLen, void *ctx)
{
    switch (itemType) {
        case IotCommStorageItemTypeUsers:
            return storage.writeBlob("users", data, dataLen);

        case IotCommStorageItemTypeDeviceIdentityKeyPair:
            return storage.writeBlob("deviceID", data, dataLen);

        default:
            return ESP_FAIL;
    }
}

static void commStart()
{
    IotCommServerConfig_t iotCommServerConfig;
    char hostname[MAX_HOSTNAME_LEN + 1];
    esp_err_t err;

    err = wifiMgrGetHostname(hostname);
    if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) {
        err = setMdnsHostname(hostname);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to configure mDNS hostname; continuing without mDNS discovery.");
    }

    iotCommServerConfig = iotCommDefaultServerConfig();
    iotCommServerConfig.udpListenPort = 32888;
    err = iotCommStartServer(&iotCommServerConfig);
    if (err != ESP_OK) {
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

static void onCustomCommand(IotCommEvent_t *event)
{
    switch (event->command->transportType) {
        case IotCommTransportTypeWebSocket:
            switch (event->command->cmd) {
                case CMD_SET_COLOR:
                    onSetColorCmd(event);
                    return;

                case CMD_FADE_TO_COLOR:
                    onFadeToColorCmd(event);
                    return;

                case CMD_SET_AND_FADE_TO_COLOR:
                    onSetAndFadeToColorCmd(event);
                    return;

                case CMD_SET_MDNS_HOSTNAME:
                    onSetMDnsHostName(event);
                    return;

                default:
                    break;
            }
            break;

        case IotCommTransportTypeUDP:
            switch (event->command->cmd) {
                case CMD_SET_COLOR:
                    onSetColorCmd(event);
                    return;

                case CMD_FADE_TO_COLOR:
                    onFadeToColorCmd(event);
                    return;

                case CMD_SET_AND_FADE_TO_COLOR:
                    onSetAndFadeToColorCmd(event);
                    return;

                default:
                    break;
            }
            break;

        case IotCommTransportTypeUnknown:
            break;
    }

    // Invalid command
    iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "Invalid command");
}

static void onSetColorCmd(IotCommEvent_t *event)
{
    binary_reader_t br;
    uint16_t r, g, b;

    br = br_init(event->command->data, event->command->dataLen);
    if (!(br_read_be16(&br, &r) && br_read_be16(&br, &g) && br_read_be16(&br, &b))) {
        ESP_LOGD(TAG, "SET COLOR command: Invalid packet.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET COLOR command: Invalid packet");
        return;
    }

    ESP_LOGD(TAG, "SET COLOR command: R:%d, G:%d, B:%d.", (int)r, (int)g, (int)b);
    ledStrip5050Set(r, g, b);
}

static void onFadeToColorCmd(IotCommEvent_t *event)
{
    binary_reader_t br;
    uint16_t r, g, b;
    uint16_t ms;

    br = br_init(event->command->data, event->command->dataLen);
    if (!(br_read_be16(&br, &r) && br_read_be16(&br, &g) && br_read_be16(&br, &b) && br_read_be16(&br, &ms))) {
        ESP_LOGD(TAG, "FADE TO COLOR command: Invalid packet.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET COLOR command: Invalid packet");
        return;
    }

    ESP_LOGD(TAG, "FADE TO COLOR command: R:%d, G:%d, B:%d (%dms).", (int)r, (int)g, (int)b, (int)ms);
    ledStrip5050FadeTo(r, g, b, (uint32_t)ms);
}

static void onSetAndFadeToColorCmd(IotCommEvent_t *event)
{
    binary_reader_t br;
    uint16_t r, g, b, rEnd, gEnd, bEnd;
    uint16_t ms;

    br = br_init(event->command->data, event->command->dataLen);
    if (!(
        br_read_be16(&br, &r) && br_read_be16(&br, &g) && br_read_be16(&br, &b) &&
        br_read_be16(&br, &rEnd) && br_read_be16(&br, &gEnd) && br_read_be16(&br, &bEnd) &&
        br_read_be16(&br, &ms)
    )) {
        ESP_LOGD(TAG, "SET AND FADE TO COLOR command: Invalid packet.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET COLOR command: Invalid packet");
        return;
    }

    ESP_LOGD(TAG, "SET AND FADE TO COLOR command: R:%u, G:%u, B:%u to R:%u, G:%u, B:%u (%ums).", r, g, b, rEnd, gEnd, bEnd, ms);
    ledStrip5050SetAndFadeTo(r, g, b, rEnd, gEnd, bEnd, (uint32_t)ms);
}

static void onSetMDnsHostName(IotCommEvent_t *event)
{
    binary_reader_t br;
    lightstd::string hostname;
    const char *name;
    size_t nameLen;
    esp_err_t err;

    if (!iotCommIsSessionUserAdmin(event->sessionHandle)) {
        ESP_LOGD(TAG, "SET MDNS HOSTNAME command: Insufficient privileges.");
        iotCommEventReplyWithError(event->sessionHandle, ESP_ERR_NOT_ALLOWED, "Insufficient privileges");
        return;
    }

    br = br_init(event->command->data, event->command->dataLen);
    if (!br_read_str(&br, &name,  &nameLen)) {
        ESP_LOGD(TAG, "SET MDNS HOSTNAME command: Invalid packet.");
        iotCommSessionClose(event->sessionHandle, WS_CLOSE_INVALID_PAYLOAD, "SET MDNS HOSTNAME command: Invalid packet");
        return;
    }

    if (!hostname.append(name, nameLen)) {
        iotCommEventReplyWithError(event->sessionHandle, ESP_ERR_NO_MEM, "Unable to set new hostname");
        return;
    }

    err = wifiMgrSetHostname(hostname.c_str());
    if (err == ESP_OK) {
        err = setMdnsHostname(hostname.c_str());
    }
    if (err != ESP_OK) {
        iotCommEventReplyWithError(event->sessionHandle, ESP_FAIL, "Unable to set new hostname");
        return;
    }

    // Done
    iotCommEventReplyWithError(event->sessionHandle, ESP_OK, nullptr);
}

static esp_err_t getDefaultHostname(char hostname[MAX_HOSTNAME_LEN + 1])
{
    Sha256Context_t sha256Ctx;
    uint8_t mac[6];
    uint8_t hash[32];
    esp_err_t err;

    hostname[0] = 0;

    err =  esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        return err;
    }

    sha256Init(&sha256Ctx);
    err = sha256Start(&sha256Ctx);
    if (err == ESP_OK) {
        err = sha256Update(&sha256Ctx, mac, sizeof(mac));
        if (err == ESP_OK) {
            err = sha256Finish(&sha256Ctx, hash);
        }
    }
    sha256Done(&sha256Ctx);
    if (err != ESP_OK) {
        return err;
    }

    snprintf(hostname, MAX_HOSTNAME_LEN + 1, "node-%02x%02x%02x", hash[0], hash[1], hash[2]);
    return ESP_OK;
}

static bool ensureMdnsInitialized()
{
    if (mdnsInitialized) {
        return true;
    }

    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mDNS initialization retry failed.");
        return false;
    }

    mdnsInitialized = true;
    ESP_LOGI(TAG, "mDNS initialization retry succeeded.");
    return true;
}

static void registerMdnsHttpService()
{
    mdns_txt_item_t txt[2] = {
        { "class", "ledstrip" },
        { "subclass", "5050" }
    };

    if ((!commIsActive()) || mdnsHttpServiceRegistered || (!ensureMdnsInitialized())) {
        return;
    }

    if (mdns_service_add(nullptr, "_http", "_tcp", 80, txt, 2) == ESP_OK) {
        mdnsHttpServiceRegistered = true;
    }
    else {
        ESP_LOGW(TAG, "Unable to register mDNS HTTP service.");
    }
}

static void unregisterMdnsHttpService()
{
    if (mdnsHttpServiceRegistered) {
        mdns_service_remove("_http", "_tcp");
        mdnsHttpServiceRegistered = false;
    }
}

static esp_err_t setMdnsHostname(const char *hostname)
{
    if (!ensureMdnsInitialized()) {
        return ESP_FAIL;
    }
    return mdns_hostname_set(hostname);
}

static void onboardLedTimer(TimerHandle_t xTimer)
{
    AutoRundownProtection rpResetLock(rpReset);

    if (rpResetLock.acquired()) {
        onboardLedTimerCounter = (onboardLedTimerCounter + 1) & 1;
        if (commIsActive()) {
            onboardLedSet(true);
        }
        else {
            onboardLedSet(!!(onboardLedTimerCounter == 1));
        }
    }
}

static void resetButtonPressedHandler(void *ctx)
{
    runOnce(&resetOnce, [](void *ctx) -> void {
        rundownProtWait(&rpReset);

        commStop();
        iotCommDeinit();
        wifiMgrDeinit();
        if (mdnsInitialized) {
            mdns_free();
            mdnsInitialized = false;
            mdnsHttpServiceRegistered = false;
        }
        ledStrip5050Deinit();

        // Full NVS erase (factory reset for provisioning + any stored app data)
        storage.eraseAll();

        ESP_LOGI(TAG, "Rebooting...");

        // Wait a bit and restart
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    }, nullptr);
}
