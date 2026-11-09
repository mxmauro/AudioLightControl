#include "config.h"
#include "esp_err.h"
#include "gpio_manager.h"
#include "iot_comm/utils/binary.h"
#include "soc/gpio_num.h"
#include <assert.h>
#include <cstring>
#include <esp_log.h>
#include <lightstd/vector.h>
#include <storage/nvs.h>
#include <stdlib.h>

static const char* TAG = "CONFIG";

#define CONFIG_NAMESPACE          "config"
#define INPUT_CONFIG_KEY          "input"
#define LIGHTING_CONFIG_KEY       "lighting"
#define LEGACY_CONFIG_NAMESPACE   "general"
#define LEGACY_CONFIG_KEY         "config"
#define LEGACY_CONFIG_VERSION_V1  1

// -----------------------------------------------------------------------------

static void setDefaultInputConfig(InputConfig_t* input);
static void setDefaultLightingConfig(LightingControllerConfigs_t* lighting);
static void setDefaultConfig(Config_t* config);
static esp_err_t configLoadInput(InputConfig_t* input);
static esp_err_t configLoadLighting(LightingControllerConfigs_t* lighting);
static esp_err_t configReadLegacyLighting(binary_reader_t* br, LightingControllerConfigs_t* lighting);
static esp_err_t createAndInitPinValidator(GpioPinValidatorHandle_t* pinValidator);
static esp_err_t validateLightingConfig(const LightingControllerConfig_t* lighting, GpioPinValidatorHandle_t pinValidator);
static esp_err_t writeConfigBlob(const char* key, const uint8_t* data, size_t dataLen);

// -----------------------------------------------------------------------------

esp_err_t configLoad(Config_t* config)
{
    esp_err_t err;

    assert(config);

    setDefaultConfig(config);
    err = configLoadInput(&config->input);
    if (err != ESP_OK)
    {
        return err;
    }
    err = configLoadLighting(&config->lighting);
    if (err != ESP_OK)
    {
        return err;
    }
    return configValidate(config);
}

esp_err_t configSaveInput(const InputConfig_t* input)
{
    uint8_t data[3];
    binary_writer_t bw;

    assert(input);

    bw = bw_init(data, sizeof(data));
    if ((!configWriteInput(&bw, input)) || bw.written != sizeof(data))
    {
        return ESP_FAIL;
    }
    return writeConfigBlob(INPUT_CONFIG_KEY, data, sizeof(data));
}

esp_err_t configSaveLighting(const LightingControllerConfigs_t* lighting)
{
    NVSStorage legacyStorage(LEGACY_CONFIG_NAMESPACE);
    uint8_t* data;
    binary_writer_t bw;
    size_t dataLen;
    esp_err_t err;

    assert(lighting);

    dataLen = configCalcLightingSize(lighting);
    data = static_cast<uint8_t*>(malloc(dataLen));
    if (!data)
    {
        return ESP_ERR_NO_MEM;
    }

    bw = bw_init(data, dataLen);
    if ((!configWriteLighting(&bw, lighting)) || bw.written != dataLen)
    {
        free(data);
        return ESP_FAIL;
    }

    err = writeConfigBlob(LIGHTING_CONFIG_KEY, data, dataLen);
    free(data);
    if (err != ESP_OK)
    {
        return err;
    }

    // A completed new-format write makes legacy lighting storage obsolete.
    err = legacyStorage.erase(LEGACY_CONFIG_KEY);
    if (err == ESP_OK)
    {
        err = legacyStorage.commit();
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Unable to remove legacy lighting configuration. Error: %d.", err);
    }
    return ESP_OK;
}

esp_err_t configReadInput(binary_reader_t* br, InputConfig_t* input)
{
    uint8_t version;

    assert(br);
    assert(input);

    setDefaultInputConfig(input);
    if ((!br_read_byte(br, &version)) || version != INPUT_CONFIG_VERSION || (!br_ext_read_gpio(br, &input->resetGpioPin)) ||
        (!br_ext_read_gpio(br, &input->irGpioPin)))
    {
        setDefaultInputConfig(input);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

size_t configCalcInputSize(const InputConfig_t*)
{
    return sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t);
}

bool configWriteInput(binary_writer_t* bw, const InputConfig_t* input)
{
    assert(bw);
    assert(input);

    return bw_write_byte(bw, INPUT_CONFIG_VERSION) && bw_ext_write_gpio(bw, input->resetGpioPin) &&
           bw_ext_write_gpio(bw, input->irGpioPin);
}

esp_err_t configReadLighting(binary_reader_t* br, LightingControllerConfigs_t* lighting)
{
    GpioPinValidatorHandle_t pinValidator;
    uint8_t version;
    uint8_t lightingCount;
    esp_err_t err;

    assert(br);
    assert(lighting);

    setDefaultLightingConfig(lighting);
    if ((!br_read_byte(br, &version)) || version != LIGHTING_CONFIG_VERSION || (!br_read_byte(br, &lightingCount)) ||
        lightingCount > LIGHTING_CONTROLLER_MAX_LIGHTING)
    {
        return ESP_ERR_INVALID_ARG;
    }

    err = createAndInitPinValidator(&pinValidator);
    if (err != ESP_OK)
    {
        return err;
    }

    for (uint8_t i = 0; i < lightingCount && err == ESP_OK; ++i)
    {
        err = lightingControllerReadConfig(br, &lighting->lighting[i], pinValidator);
        if (err == ESP_OK && lighting->lighting[i].type == LightingControllerTypeNone)
        {
            err = ESP_ERR_INVALID_ARG;
        }
        if (err == ESP_OK && lighting->lighting[i].type == LightingControllerTypeDmx)
        {
            for (uint8_t previous = 0; previous < i; ++previous)
            {
                if (lighting->lighting[previous].type == LightingControllerTypeDmx)
                {
                    err = ESP_ERR_INVALID_ARG;
                    break;
                }
            }
        }
    }
    if (err == ESP_OK)
    {
        lighting->lightingCount = lightingCount;
    }
    else
    {
        setDefaultLightingConfig(lighting);
    }

    gpioPinValidatorDestroy(pinValidator);
    return err;
}

size_t configCalcLightingSize(const LightingControllerConfigs_t* lighting)
{
    size_t size = sizeof(uint8_t) + sizeof(uint8_t);

    assert(lighting);
    for (size_t i = 0; i < lighting->lightingCount; ++i)
    {
        size += lightingControllerCalcConfigSize(&lighting->lighting[i]);
    }
    return size;
}

bool configWriteLighting(binary_writer_t* bw, const LightingControllerConfigs_t* lighting)
{
    assert(bw);
    assert(lighting);

    if (lighting->lightingCount > LIGHTING_CONTROLLER_MAX_LIGHTING ||
        (!bw_write_byte(bw, LIGHTING_CONFIG_VERSION)) || (!bw_write_byte(bw, static_cast<uint8_t>(lighting->lightingCount))))
    {
        return false;
    }
    for (size_t i = 0; i < lighting->lightingCount; ++i)
    {
        if (!lightingControllerWriteConfig(bw, &lighting->lighting[i]))
        {
            return false;
        }
    }
    return true;
}

esp_err_t configValidate(const Config_t* config)
{
    GpioPinValidatorHandle_t pinValidator;
    esp_err_t err;

    assert(config);

    err = createAndInitPinValidator(&pinValidator);
    if (err != ESP_OK)
    {
        return err;
    }

    if (config->input.resetGpioPin != GPIO_NUM_NC)
    {
        err = gpioPinValidatorClaim(pinValidator, config->input.resetGpioPin, GpioPinUseInput);
    }
    if (err == ESP_OK && config->input.irGpioPin != GPIO_NUM_NC)
    {
        err = gpioPinValidatorClaim(pinValidator, config->input.irGpioPin, GpioPinUseInput);
    }
    if (err == ESP_OK && config->lighting.lightingCount > LIGHTING_CONTROLLER_MAX_LIGHTING)
    {
        err = ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < config->lighting.lightingCount && err == ESP_OK; ++i)
    {
        const LightingControllerConfig_t* lighting = &config->lighting.lighting[i];

        if (lighting->type == LightingControllerTypeNone)
        {
            err = ESP_ERR_INVALID_ARG;
        }
        else
        {
            err = validateLightingConfig(lighting, pinValidator);
        }
        if (err == ESP_OK && lighting->type == LightingControllerTypeDmx)
        {
            for (size_t previous = 0; previous < i; ++previous)
            {
                if (config->lighting.lighting[previous].type == LightingControllerTypeDmx)
                {
                    err = ESP_ERR_INVALID_ARG;
                    break;
                }
            }
        }
    }

    gpioPinValidatorDestroy(pinValidator);
    return err;
}

uint8_t configEncodeGpioPin(gpio_num_t pin)
{
    return (pin == GPIO_NUM_NC) ? 0xFFU : static_cast<uint8_t>(pin);
}

bool configDecodeGpioPin(uint8_t encodedPin, GpioPinUse_t use, bool optional, gpio_num_t* pin)
{
    assert(pin);
    assert(use == GpioPinUseInput || use == GpioPinUseOutput);

    *pin = GPIO_NUM_NC;
    if (encodedPin == 0xFFU)
    {
        return optional;
    }
    if (encodedPin > 0x7FU || !gpioPinManagerIsValid(static_cast<gpio_num_t>(encodedPin), use))
    {
        return false;
    }
    *pin = static_cast<gpio_num_t>(encodedPin);
    return true;
}

bool br_ext_read_gpio(binary_reader_t* br, gpio_num_t* pin)
{
    uint8_t encodedPin;

    assert(br);
    assert(pin);

    *pin = GPIO_NUM_NC;
    if (!br_read_byte(br, &encodedPin))
    {
        return false;
    }
    if (encodedPin == 0xFFU)
    {
        return true;
    }
    if (encodedPin > 0x7FU)
    {
        return false;
    }
    *pin = static_cast<gpio_num_t>(encodedPin);
    return true;
}

bool bw_ext_write_gpio(binary_writer_t* bw, gpio_num_t pin)
{
    assert(bw);

    return bw_write_byte(bw, configEncodeGpioPin(pin));
}

// -----------------------------------------------------------------------------

static void setDefaultInputConfig(InputConfig_t* input)
{
    memset(input, 0, sizeof(InputConfig_t));
    input->resetGpioPin = GPIO_NUM_NC;
    input->irGpioPin = GPIO_NUM_NC;
}

static void setDefaultLightingConfig(LightingControllerConfigs_t* lighting)
{
    memset(lighting, 0, sizeof(LightingControllerConfigs_t));
}

static void setDefaultConfig(Config_t* config)
{
    setDefaultInputConfig(&config->input);
    setDefaultLightingConfig(&config->lighting);
}

static esp_err_t configLoadInput(InputConfig_t* input)
{
    NVSStorage storage(CONFIG_NAMESPACE);
    lightstd::vector<uint8_t> data;
    binary_reader_t br;
    esp_err_t err;

    setDefaultInputConfig(input);
    err = storage.readBlob(INPUT_CONFIG_KEY, data);
    if (err == ESP_ERR_NOT_FOUND)
    {
        return ESP_OK;
    }
    if (err != ESP_OK)
    {
        return err;
    }

    br = br_init(data.data(), data.size());
    err = configReadInput(&br, input);
    if (err != ESP_OK || br.len != 0)
    {
        ESP_LOGE(TAG, "Stored input configuration is invalid.");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t configLoadLighting(LightingControllerConfigs_t* lighting)
{
    NVSStorage storage(CONFIG_NAMESPACE);
    NVSStorage legacyStorage(LEGACY_CONFIG_NAMESPACE);
    lightstd::vector<uint8_t> data;
    binary_reader_t br;
    esp_err_t err;

    setDefaultLightingConfig(lighting);
    err = storage.readBlob(LIGHTING_CONFIG_KEY, data);
    if (err == ESP_ERR_NOT_FOUND)
    {
        err = legacyStorage.readBlob(LEGACY_CONFIG_KEY, data);
        if (err == ESP_ERR_NOT_FOUND)
        {
            return ESP_OK;
        }
        if (err != ESP_OK)
        {
            return err;
        }

        br = br_init(data.data(), data.size());
        err = configReadLegacyLighting(&br, lighting);
        if (err != ESP_OK || br.len != 0)
        {
            ESP_LOGE(TAG, "Stored legacy lighting configuration is invalid.");
            return ESP_ERR_INVALID_ARG;
        }
        return ESP_OK;
    }
    if (err != ESP_OK)
    {
        return err;
    }

    br = br_init(data.data(), data.size());
    err = configReadLighting(&br, lighting);
    if (err != ESP_OK || br.len != 0)
    {
        ESP_LOGE(TAG, "Stored lighting configuration is invalid.");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t configReadLegacyLighting(binary_reader_t* br, LightingControllerConfigs_t* lighting)
{
    GpioPinValidatorHandle_t pinValidator;
    uint8_t version;
    esp_err_t err;

    assert(br);
    assert(lighting);

    setDefaultLightingConfig(lighting);
    if ((!br_read_byte(br, &version)) || version != LEGACY_CONFIG_VERSION_V1)
    {
        return ESP_ERR_INVALID_ARG;
    }
    err = createAndInitPinValidator(&pinValidator);
    if (err != ESP_OK)
    {
        return err;
    }

    err = lightingControllerReadConfig(br, &lighting->lighting[0], pinValidator);
    if (err == ESP_OK && lighting->lighting[0].type == LightingControllerTypeNone)
    {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK)
    {
        lighting->lightingCount = 1;
    }
    else
    {
        setDefaultLightingConfig(lighting);
    }

    gpioPinValidatorDestroy(pinValidator);
    return err;
}

static esp_err_t createAndInitPinValidator(GpioPinValidatorHandle_t* pinValidator)
{
    esp_err_t err;

    assert(pinValidator);

    *pinValidator = gpioPinValidatorCreate();
    if (!(*pinValidator))
    {
        return ESP_ERR_NO_MEM;
    }
    if (ONBOARD_LED_GPIO_PIN != GPIO_NUM_NC)
    {
        err = gpioPinValidatorClaim(*pinValidator, ONBOARD_LED_GPIO_PIN, GpioPinUseOutput);
        if (err != ESP_OK)
        {
            gpioPinValidatorDestroy(*pinValidator);
            *pinValidator = nullptr;
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t validateLightingConfig(const LightingControllerConfig_t* lighting, GpioPinValidatorHandle_t pinValidator)
{
    assert(lighting);
    assert(pinValidator);

    switch (lighting->type)
    {
        case LightingControllerTypeDmx:
            return gpioPinValidatorClaim(pinValidator, lighting->controller.dmx.txGpioPin, GpioPinUseOutput);

        case LightingControllerTypeRgb5050:
            {
                esp_err_t err = gpioPinValidatorClaim(pinValidator, lighting->controller.rgb5050.redGpioPin, GpioPinUseOutput);

                if (err == ESP_OK)
                {
                    err = gpioPinValidatorClaim(pinValidator, lighting->controller.rgb5050.greenGpioPin, GpioPinUseOutput);
                }
                if (err == ESP_OK)
                {
                    err = gpioPinValidatorClaim(pinValidator, lighting->controller.rgb5050.blueGpioPin, GpioPinUseOutput);
                }
                return err;
            }

        case LightingControllerTypeWs281x:
            {
                esp_err_t err = gpioPinValidatorClaim(pinValidator, lighting->controller.ws281x.dataGpioPin, GpioPinUseOutput);

                if (err == ESP_OK && lighting->controller.ws281x.mirrorGpioPin != GPIO_NUM_NC)
                {
                    err = gpioPinValidatorClaim(pinValidator, lighting->controller.ws281x.mirrorGpioPin, GpioPinUseOutput);
                }
                if (err == ESP_OK && (lighting->controller.ws281x.ledCount == 0 ||
                                      lighting->controller.ws281x.ledCount > WS281X_MAX_LEDS))
                {
                    err = ESP_ERR_INVALID_ARG;
                }
                return err;
            }

        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t writeConfigBlob(const char* key, const uint8_t* data, size_t dataLen)
{
    NVSStorage storage(CONFIG_NAMESPACE);
    esp_err_t err;

    err = storage.writeBlob(key, data, dataLen);
    if (err != ESP_OK)
    {
        return err;
    }
    return storage.commit();
}
