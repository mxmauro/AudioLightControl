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

#define CONFIG_NAMESPACE "general"
#define CONFIG_KEY       "config"
#define CONFIG_VERSION   1

// -----------------------------------------------------------------------------

static void setDefaultConfig(Config_t* config);
static esp_err_t createAndInitPinValidator(GpioPinValidatorHandle_t* pinValidator);

// -----------------------------------------------------------------------------

esp_err_t configLoad(Config_t* config)
{
    NVSStorage storage(CONFIG_NAMESPACE);
    lightstd::vector<uint8_t> data;
    binary_reader_t br;
    esp_err_t err;

    assert(config);

    setDefaultConfig(config);
    err = storage.readBlob(CONFIG_KEY, data);
    if (err == ESP_ERR_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No configuration stored; output is inactive.");
        return ESP_OK;
    }
    if (err != ESP_OK)
    {
        return err;
    }

    br = br_init(data.data(), data.size());
    err = configRead(&br, config);
    if (err != ESP_OK || br.len != 0)
    {
        ESP_LOGE(TAG, "Stored configuration is invalid.");
        setDefaultConfig(config);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t configSave(const Config_t* config)
{
    NVSStorage storage(CONFIG_NAMESPACE);
    uint8_t* data;
    binary_writer_t bw;
    size_t dataLen;
    esp_err_t err;

    assert(config);

    dataLen = configCalcSize(config);
    data = static_cast<uint8_t*>(malloc(dataLen));
    if (!data)
    {
        return ESP_ERR_NO_MEM;
    }

    bw = bw_init(data, dataLen);
    if (!configWrite(&bw, config))
    {
        free(data);
        return ESP_FAIL;
    }

    if (bw.written != dataLen)
    {
        free(data);
        return ESP_FAIL;
    }

    err = storage.writeBlob(CONFIG_KEY, data, dataLen);
    if (err == ESP_OK)
    {
        err = storage.commit();
    }

    // Done
    free(data);
    return err;
}

esp_err_t configRead(binary_reader_t* br, Config_t* config)
{
    GpioPinValidatorHandle_t pinValidator;
    uint8_t version;
    esp_err_t err;

    assert(br);
    assert(config);

    setDefaultConfig(config);
    if ((!br_read_byte(br, &version)) || version != CONFIG_VERSION)
    {
        return ESP_ERR_INVALID_ARG;
    }

    err = createAndInitPinValidator(&pinValidator);
    if (err != ESP_OK)
    {
        return err;
    }

    err = outputControllerReadConfig(br, &config->output, pinValidator);
    if (err != ESP_OK)
    {
        gpioPinValidatorDestroy(pinValidator);
        return err;
    }

    gpioPinValidatorDestroy(pinValidator);
    return ESP_OK;
}

size_t configCalcSize(const Config_t* config)
{
    return sizeof(uint8_t) + outputControllerCalcConfigSize(&config->output);
}

bool configWrite(binary_writer_t* bw, const Config_t* config)
{
    return bw_write_byte(bw, CONFIG_VERSION) && outputControllerWriteConfig(bw, &config->output);
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
        return (optional) ? true : false;
    }
    if (encodedPin > 0x7FU)
    {
        return false;
    }
    if (!gpioPinManagerIsValid(static_cast<gpio_num_t>(encodedPin), use))
    {
        return false;
    }
    *pin = static_cast<gpio_num_t>(encodedPin);
    return true;
}

bool br_ext_read_gpio(binary_reader_t* br, gpio_num_t* pin)
{
    uint8_t encodedPin;

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
    return bw_write_byte(bw, configEncodeGpioPin(pin));
}

// -----------------------------------------------------------------------------

static void setDefaultConfig(Config_t* config)
{
    memset(config, 0, sizeof(Config_t));
    config->output.type = OutputControllerTypeNone;
}

static esp_err_t createAndInitPinValidator(GpioPinValidatorHandle_t* pinValidator)
{
    esp_err_t err;

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
            goto done;
        }
    }
    if (RESET_BUTTON_GPIO_PIN != GPIO_NUM_NC)
    {
        err = gpioPinValidatorClaim(*pinValidator, RESET_BUTTON_GPIO_PIN, GpioPinUseInput);
        if (err != ESP_OK)
        {
            goto done;
        }
    }
    if (IR_RECEIVER_GPIO_PIN != GPIO_NUM_NC)
    {
        err = gpioPinValidatorClaim(*pinValidator, IR_RECEIVER_GPIO_PIN, GpioPinUseInput);
        if (err != ESP_OK)
        {
            goto done;
        }
    }

    err = ESP_OK;

done:
    if (err != ESP_OK)
    {
        gpioPinValidatorDestroy(*pinValidator);
        *pinValidator = nullptr;
    }
    return err;
}
