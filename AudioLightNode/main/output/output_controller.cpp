#include "output_controller.h"
#include <assert.h>
#include <mutex.h>
#include <rundown_protection.h>

static Mutex sLifecycleMtx;
static RundownProtection_t sUsageRundown = RUNDOWN_PROTECTION_INIT_STATIC;
static OutputControllerType_t sActiveType = OutputControllerTypeNone;
static bool sInitialized = false;
static bool sTransitioning = false;

// -----------------------------------------------------------------------------

static bool acquireOutputController(OutputControllerType_t* type);
static void releaseOutputController();

// -----------------------------------------------------------------------------

esp_err_t outputControllerReadConfig(binary_reader_t* br, OutputControllerConfig_t* config, GpioPinValidatorHandle_t pinValidator)

{
    uint8_t outputType;

    assert(br);
    assert(config);
    assert(pinValidator);

    if (!br_read_byte(br, &outputType))
    {
        return ESP_ERR_INVALID_ARG;
    }
    config->type = static_cast<OutputControllerType_t>(outputType);
    switch (config->type)
    {
        case OutputControllerTypeNone:
            return ESP_OK;

        case OutputControllerTypeDmx:
            return dmxControllerReadConfig(br, &config->controller.dmx, pinValidator);

        case OutputControllerTypeRgb5050:
            return ledStrip5050ReadConfig(br, &config->controller.rgb5050, pinValidator);
    }

    return ESP_ERR_INVALID_ARG;
}

size_t outputControllerCalcConfigSize(const OutputControllerConfig_t* config)
{
    assert(config);

    switch (config->type)
    {
        case OutputControllerTypeNone:
            return sizeof(uint8_t);

        case OutputControllerTypeDmx:
            return sizeof(uint8_t) + dmxControllerCalcConfigSize(&config->controller.dmx);

        case OutputControllerTypeRgb5050:
            return sizeof(uint8_t) + ledStrip5050CalcConfigSize(&config->controller.rgb5050);
    }

    return 0;
}

bool outputControllerWriteConfig(binary_writer_t* bw, const OutputControllerConfig_t* config)
{
    assert(bw);
    assert(config);

    if (!bw_write_byte(bw, static_cast<uint8_t>(config->type)))
    {
        return false;
    }

    switch (config->type)
    {
        case OutputControllerTypeNone:
            return true;

        case OutputControllerTypeDmx:
            return dmxControllerWriteConfig(bw, &config->controller.dmx);

        case OutputControllerTypeRgb5050:
            return ledStrip5050WriteConfig(bw, &config->controller.rgb5050);
    }

    return false;
}

esp_err_t outputControllerInit(const OutputControllerConfig_t* config)
{
    esp_err_t err = ESP_OK;

    assert(config);

    {
        AutoMutex lock(sLifecycleMtx);

        if (sInitialized || sTransitioning)
        {
            return ESP_ERR_INVALID_STATE;
        }
        sTransitioning = true;
    }

    switch (config->type)
    {
        case OutputControllerTypeNone:
            break;

        case OutputControllerTypeDmx:
            err = dmxControllerInit(&config->controller.dmx);
            break;

        case OutputControllerTypeRgb5050:
            err = ledStrip5050Init(&config->controller.rgb5050);
            break;

        default:
            err = ESP_ERR_INVALID_ARG;
            break;
    }

    {
        AutoMutex lock(sLifecycleMtx);

        sTransitioning = false;
        if (err == ESP_OK)
        {
            sActiveType = config->type;
            sInitialized = true;
        }
    }

    return err;
}

void outputControllerDeinit()
{
    OutputControllerType_t activeType;

    {
        AutoMutex lock(sLifecycleMtx);

        if ((!sInitialized) || sTransitioning)
        {
            return;
        }

        sTransitioning = true;
        activeType = sActiveType;
        sActiveType = OutputControllerTypeNone;
    }

    rundownProtWait(&sUsageRundown);

    switch (activeType)
    {
        case OutputControllerTypeNone:
            break;

        case OutputControllerTypeDmx:
            dmxControllerDeinit();
            break;

        case OutputControllerTypeRgb5050:
            ledStrip5050Deinit();
            break;
    }

    rundownProtDestroy(&sUsageRundown);
    rundownProtInit(&sUsageRundown);

    {
        AutoMutex lock(sLifecycleMtx);

        sInitialized = false;
        sTransitioning = false;
    }
}

OutputControllerType_t outputControllerGetType()
{
    AutoMutex lock(sLifecycleMtx);

    return sInitialized ? sActiveType : OutputControllerTypeNone;
}

bool outputControllerSupportsDmxChannelData()
{
    AutoMutex lock(sLifecycleMtx);

    return sInitialized && sActiveType == OutputControllerTypeDmx;
}

const char* outputControllerGetTypeName(OutputControllerType_t type)
{
    switch (type)
    {
        case OutputControllerTypeDmx:
            return "dmx";

        case OutputControllerTypeRgb5050:
            return "rgb5050";

        default:
            return "none";
    }
}

esp_err_t outputControllerSet(uint16_t r, uint16_t g, uint16_t b)
{
    OutputControllerType_t activeType;
    esp_err_t err;

    if (!acquireOutputController(&activeType))
    {
        return ESP_ERR_INVALID_STATE;
    }

    switch (activeType)
    {
        case OutputControllerTypeDmx:
            err = dmxControllerSet(r, g, b);
            break;

        case OutputControllerTypeRgb5050:
            err = ledStrip5050Set(r, g, b);
            break;

        default:
            err = ESP_ERR_INVALID_STATE;
            break;
    }

    releaseOutputController();
    return err;
}

esp_err_t outputControllerFadeTo(uint16_t r, uint16_t g, uint16_t b, uint32_t ms)
{
    OutputControllerType_t activeType;
    esp_err_t err;

    if (!acquireOutputController(&activeType))
    {
        return ESP_ERR_INVALID_STATE;
    }

    switch (activeType)
    {
        case OutputControllerTypeDmx:
            err = dmxControllerFadeTo(r, g, b, ms);
            break;

        case OutputControllerTypeRgb5050:
            err = ledStrip5050FadeTo(r, g, b, ms);
            break;

        default:
            err = ESP_ERR_INVALID_STATE;
            break;
    }

    releaseOutputController();
    return err;
}

esp_err_t outputControllerSetAndFadeTo(uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd, uint16_t gEnd, uint16_t bEnd,
                                       uint32_t ms)
{
    OutputControllerType_t activeType;
    esp_err_t err;

    if (!acquireOutputController(&activeType))
    {
        return ESP_ERR_INVALID_STATE;
    }

    switch (activeType)
    {
        case OutputControllerTypeDmx:
            err = dmxControllerSetAndFadeTo(rStart, gStart, bStart, rEnd, gEnd, bEnd, ms);
            break;

        case OutputControllerTypeRgb5050:
            err = ledStrip5050SetAndFadeTo(rStart, gStart, bStart, rEnd, gEnd, bEnd, ms);
            break;

        default:
            err = ESP_ERR_INVALID_STATE;
            break;
    }

    releaseOutputController();
    return err;
}

esp_err_t outputControllerSetDmxChannels(uint16_t startChannel, const uint8_t* data, size_t dataLen)
{
    OutputControllerType_t activeType;
    esp_err_t err;

    if (!acquireOutputController(&activeType))
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (activeType != OutputControllerTypeDmx)
    {
        releaseOutputController();
        return ESP_ERR_NOT_SUPPORTED;
    }

    err = dmxControllerSetChannels(startChannel, data, dataLen);
    releaseOutputController();
    return err;
}

// -----------------------------------------------------------------------------

static bool acquireOutputController(OutputControllerType_t* type)
{
    AutoMutex lock(sLifecycleMtx);

    assert(type);

    if ((!sInitialized) || sTransitioning || (!rundownProtAcquire(&sUsageRundown)))
    {
        return false;
    }

    *type = sActiveType;
    return true;
}

static void releaseOutputController()
{
    rundownProtRelease(&sUsageRundown);
}
