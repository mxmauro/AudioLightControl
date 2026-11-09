#include "lighting_controller.h"
#include <assert.h>
#include <mutex.h>
#include <rundown_protection.h>

// -----------------------------------------------------------------------------

typedef struct LightingControllerLease_s {
    size_t lightingCount;
    LightingControllerType_t types[LIGHTING_CONTROLLER_MAX_LIGHTING];
} LightingControllerLease_t;

// -----------------------------------------------------------------------------

static RWMutex sLifecycleRwMtx;
static RundownProtection_t sUsageRundown = RUNDOWN_PROTECTION_INIT_STATIC;
static LightingControllerConfigs_t sActiveConfigs = {};
static bool sInitialized = false;
static bool sTransitioning = false;

// -----------------------------------------------------------------------------

static bool acquireLightingControllerLease(uint8_t target, LightingControllerLease_t* lease);
static void releaseLightingController();
static esp_err_t initLighting(size_t slot, const LightingControllerConfig_t* config);
static void deinitLighting(size_t slot, LightingControllerType_t type);

// -----------------------------------------------------------------------------

esp_err_t lightingControllerReadConfig(binary_reader_t* br, LightingControllerConfig_t* config, GpioPinValidatorHandle_t pinValidator)
{
    uint8_t lightingType;

    assert(br);
    assert(config);
    assert(pinValidator);
    if (!br_read_byte(br, &lightingType))
    {
        return ESP_ERR_INVALID_ARG;
    }
    config->type = static_cast<LightingControllerType_t>(lightingType);
    switch (config->type)
    {
        case LightingControllerTypeNone:
            return ESP_OK;
        case LightingControllerTypeDmx:
            return dmxControllerReadConfig(br, &config->controller.dmx, pinValidator);
        case LightingControllerTypeRgb5050:
            return ledStrip5050ReadConfig(br, &config->controller.rgb5050, pinValidator);
        case LightingControllerTypeWs281x:
            return ledStripWs281xReadConfig(br, &config->controller.ws281x, pinValidator);
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

size_t lightingControllerCalcConfigSize(const LightingControllerConfig_t* config)
{
    assert(config);
    switch (config->type)
    {
        case LightingControllerTypeNone:
            return sizeof(uint8_t);
        case LightingControllerTypeDmx:
            return sizeof(uint8_t) + dmxControllerCalcConfigSize(&config->controller.dmx);
        case LightingControllerTypeRgb5050:
            return sizeof(uint8_t) + ledStrip5050CalcConfigSize(&config->controller.rgb5050);
        case LightingControllerTypeWs281x:
            return sizeof(uint8_t) + ledStripWs281xCalcConfigSize(&config->controller.ws281x);
        default:
            return 0;
    }
}

bool lightingControllerWriteConfig(binary_writer_t* bw, const LightingControllerConfig_t* config)
{
    assert(bw);
    assert(config);
    if (!bw_write_byte(bw, static_cast<uint8_t>(config->type)))
    {
        return false;
    }
    switch (config->type)
    {
        case LightingControllerTypeNone:
            return true;
        case LightingControllerTypeDmx:
            return dmxControllerWriteConfig(bw, &config->controller.dmx);
        case LightingControllerTypeRgb5050:
            return ledStrip5050WriteConfig(bw, &config->controller.rgb5050);
        case LightingControllerTypeWs281x:
            return ledStripWs281xWriteConfig(bw, &config->controller.ws281x);
        default:
            return false;
    }
}

// -----------------------------------------------------------------------------

esp_err_t lightingControllerInit(const LightingControllerConfigs_t* configs)
{
    esp_err_t err = ESP_OK;
    size_t initializedCount = 0;

    assert(configs);
    if (configs->lightingCount > LIGHTING_CONTROLLER_MAX_LIGHTING)
    {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t slot = 0; slot < configs->lightingCount; ++slot)
    {
        if (configs->lighting[slot].type == LightingControllerTypeDmx)
        {
            for (size_t previous = 0; previous < slot; ++previous)
            {
                if (configs->lighting[previous].type == LightingControllerTypeDmx)
                {
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
    }

    // Starting initialization transition.
    {
        AutoRWMutex lock(sLifecycleRwMtx, false);

        if (sInitialized || sTransitioning)
        {
            return ESP_ERR_INVALID_STATE;
        }
        sTransitioning = true;
    }

    // Initialize each slot outside the metadata lock.
    for (; initializedCount < configs->lightingCount; ++initializedCount)
    {
        err = initLighting(initializedCount, &configs->lighting[initializedCount]);
        if (err != ESP_OK)
        {
            break;
        }
    }
    if (err != ESP_OK)
    {
        // Roll back slots initialized before the failure.
        while (initializedCount > 0)
        {
            --initializedCount;
            deinitLighting(initializedCount, configs->lighting[initializedCount].type);
        }
    }

    // Publish the fully initialized configuration.
    {
        AutoRWMutex lock(sLifecycleRwMtx, false);

        sTransitioning = false;
        if (err == ESP_OK)
        {
            sActiveConfigs = *configs;
            sInitialized = true;
        }
    }
    return err;
}

void lightingControllerDeinit()
{
    LightingControllerConfigs_t configs;

    // Stop new leases before waiting for active calls to finish.
    {
        AutoRWMutex lock(sLifecycleRwMtx, false);

        if ((!sInitialized) || sTransitioning)
        {
            return;
        }
        sTransitioning = true;
        configs = sActiveConfigs;
        sActiveConfigs = {};
    }

    // Drain callers before tearing down controller-owned resources.
    rundownProtWait(&sUsageRundown);

    for (size_t slot = configs.lightingCount; slot > 0; --slot)
    {
        deinitLighting(slot - 1, configs.lighting[slot - 1].type);
    }
    rundownProtDestroy(&sUsageRundown);
    rundownProtInit(&sUsageRundown);

    // Mark the runtime inactive after every slot is gone.
    {
        AutoRWMutex lock(sLifecycleRwMtx, false);

        sInitialized = false;
        sTransitioning = false;
    }
}

size_t lightingControllerGetCount()
{
    AutoRWMutex lock(sLifecycleRwMtx, true);

    return sInitialized ? sActiveConfigs.lightingCount : 0;
}

LightingControllerType_t lightingControllerGetType(size_t slot)
{
    AutoRWMutex lock(sLifecycleRwMtx, true);

    return (sInitialized && slot < sActiveConfigs.lightingCount) ? sActiveConfigs.lighting[slot].type : LightingControllerTypeNone;
}

bool lightingControllerIsValidTarget(uint8_t target)
{
    AutoRWMutex lock(sLifecycleRwMtx, true);

    return sInitialized && (!sTransitioning) &&
           (target == LIGHTING_CONTROLLER_TARGET_ALL || target < sActiveConfigs.lightingCount);
}

const char* lightingControllerGetTypeName(LightingControllerType_t type)
{
    switch (type)
    {
        case LightingControllerTypeDmx:
            return "dmx";
        case LightingControllerTypeRgb5050:
            return "rgb5050";
        case LightingControllerTypeWs281x:
            return "ws281x";
        default:
            return "none";
    }
}

esp_err_t lightingControllerSet(uint8_t target, uint16_t r, uint16_t g, uint16_t b)
{
    LightingControllerLease_t lease;
    esp_err_t result = ESP_OK;

    if (!acquireLightingControllerLease(target, &lease))
    {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t slot = 0; slot < lease.lightingCount; ++slot)
    {
        if (target == LIGHTING_CONTROLLER_TARGET_ALL || target == slot)
        {
            esp_err_t err;

            switch (lease.types[slot])
            {
                case LightingControllerTypeDmx:
                    err = dmxControllerSet(r, g, b);
                    break;
                case LightingControllerTypeRgb5050:
                    err = ledStrip5050Set(slot, r, g, b);
                    break;
                case LightingControllerTypeWs281x:
                    err = ledStripWs281xSet(slot, r, g, b);
                    break;
                default:
                    err = ESP_ERR_INVALID_STATE;
                    break;
            }

            if (result == ESP_OK)
            {
                result = err;
            }
        }
    }
    releaseLightingController();
    return result;
}

esp_err_t lightingControllerFadeTo(uint8_t target, uint16_t r, uint16_t g, uint16_t b, uint32_t ms)
{
    LightingControllerLease_t lease;
    esp_err_t result = ESP_OK;

    if (!acquireLightingControllerLease(target, &lease))
    {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t slot = 0; slot < lease.lightingCount; ++slot)
    {
        if (target == LIGHTING_CONTROLLER_TARGET_ALL || target == slot)
        {
            esp_err_t err;

            switch (lease.types[slot])
            {
                case LightingControllerTypeDmx:
                    err = dmxControllerFadeTo(r, g, b, ms);
                    break;
                case LightingControllerTypeRgb5050:
                    err = ledStrip5050FadeTo(slot, r, g, b, ms);
                    break;
                case LightingControllerTypeWs281x:
                    err = ledStripWs281xFadeTo(slot, r, g, b, ms);
                    break;
                default:
                    err = ESP_ERR_INVALID_STATE;
                    break;
            }

            if (result == ESP_OK)
            {
                result = err;
            }
        }
    }
    releaseLightingController();
    return result;
}

esp_err_t lightingControllerSetAndFadeTo(uint8_t target, uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd,
                                         uint16_t gEnd, uint16_t bEnd, uint32_t ms)
{
    LightingControllerLease_t lease;
    esp_err_t result = ESP_OK;

    if (!acquireLightingControllerLease(target, &lease))
    {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t slot = 0; slot < lease.lightingCount; ++slot)
    {
        if (target == LIGHTING_CONTROLLER_TARGET_ALL || target == slot)
        {
            esp_err_t err;

            switch (lease.types[slot])
            {
                case LightingControllerTypeDmx:
                    err = dmxControllerSetAndFadeTo(rStart, gStart, bStart, rEnd, gEnd, bEnd, ms);
                    break;
                case LightingControllerTypeRgb5050:
                    err = ledStrip5050SetAndFadeTo(slot, rStart, gStart, bStart, rEnd, gEnd, bEnd, ms);
                    break;
                case LightingControllerTypeWs281x:
                    err = ledStripWs281xSetAndFadeTo(slot, rStart, gStart, bStart, rEnd, gEnd, bEnd, ms);
                    break;
                default:
                    err = ESP_ERR_INVALID_STATE;
                    break;
            }

            if (result == ESP_OK)
            {
                result = err;
            }
        }
    }
    releaseLightingController();
    return result;
}

esp_err_t lightingControllerSetDmxChannels(uint8_t target, uint16_t startChannel, const uint8_t* data, size_t dataLen)
{
    LightingControllerLease_t lease;
    esp_err_t result = ESP_OK;

    if (!acquireLightingControllerLease(target, &lease))
    {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t slot = 0; slot < lease.lightingCount; ++slot)
    {
        if ((target == LIGHTING_CONTROLLER_TARGET_ALL || target == slot) && lease.types[slot] == LightingControllerTypeDmx)
        {
            esp_err_t err = dmxControllerSetChannels(startChannel, data, dataLen);

            if (result == ESP_OK)
            {
                result = err;
            }
        }
    }
    releaseLightingController();
    return result;
}

esp_err_t lightingControllerSetWs281xPixels(uint8_t target, uint16_t firstLed, const uint8_t* data, uint16_t ledCount)
{
    LightingControllerLease_t lease;
    esp_err_t result = ESP_OK;

    if (!data)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!acquireLightingControllerLease(target, &lease))
    {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t slot = 0; slot < lease.lightingCount; ++slot)
    {
        if ((target == LIGHTING_CONTROLLER_TARGET_ALL || target == slot) && lease.types[slot] == LightingControllerTypeWs281x)
        {
            esp_err_t err = ledStripWs281xSetPixels(slot, firstLed, data, ledCount);

            if (result == ESP_OK)
            {
                result = err;
            }
        }
    }
    releaseLightingController();
    return result;
}

// -----------------------------------------------------------------------------

static bool acquireLightingControllerLease(uint8_t target, LightingControllerLease_t* lease)
{
    // Snapshot targetable slots while granting one lifetime lease for the complete dispatch.
    AutoRWMutex lock(sLifecycleRwMtx, true);

    assert(lease);
    if ((!sInitialized) || sTransitioning ||
        (target != LIGHTING_CONTROLLER_TARGET_ALL && target >= sActiveConfigs.lightingCount) || (!rundownProtAcquire(&sUsageRundown)))
    {
        return false;
    }
    lease->lightingCount = sActiveConfigs.lightingCount;
    for (size_t slot = 0; slot < lease->lightingCount; ++slot)
    {
        lease->types[slot] = sActiveConfigs.lighting[slot].type;
    }
    return true;
}

static void releaseLightingController()
{
    rundownProtRelease(&sUsageRundown);
}

static esp_err_t initLighting(size_t slot, const LightingControllerConfig_t* config)
{
    switch (config->type)
    {
        case LightingControllerTypeDmx:
            return dmxControllerInit(&config->controller.dmx);
        case LightingControllerTypeRgb5050:
            return ledStrip5050Init(slot, &config->controller.rgb5050);
        case LightingControllerTypeWs281x:
            return ledStripWs281xInit(slot, &config->controller.ws281x);
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static void deinitLighting(size_t slot, LightingControllerType_t type)
{
    if (type == LightingControllerTypeDmx)
    {
        dmxControllerDeinit();
    }
    else if (type == LightingControllerTypeRgb5050)
    {
        ledStrip5050Deinit(slot);
    }
    else if (type == LightingControllerTypeWs281x)
    {
        ledStripWs281xDeinit(slot);
    }
}
