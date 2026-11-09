#pragma once

#include "gpio_manager.h"
#include "lighting/lighting_controller.h"
#include <driver/gpio.h>
#include <esp_err.h>
#include <iot_comm/utils/binary.h>

// -----------------------------------------------------------------------------

#define ONBOARD_LED_GPIO_PIN    GPIO_NUM_8
#define INPUT_CONFIG_VERSION    1
#define LIGHTING_CONFIG_VERSION 1

// -----------------------------------------------------------------------------

typedef struct InputConfig_s {
    gpio_num_t resetGpioPin;
    gpio_num_t irGpioPin;
} InputConfig_t;

typedef struct Config_s {
    InputConfig_t               input;
    LightingControllerConfigs_t lighting;
} Config_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

esp_err_t configLoad(Config_t* config);
esp_err_t configSaveInput(const InputConfig_t* input);
esp_err_t configSaveLighting(const LightingControllerConfigs_t* lighting);

esp_err_t configReadInput(binary_reader_t* br, InputConfig_t* input);
size_t configCalcInputSize(const InputConfig_t* input);
bool configWriteInput(binary_writer_t* bw, const InputConfig_t* input);

esp_err_t configReadLighting(binary_reader_t* br, LightingControllerConfigs_t* lighting);
size_t configCalcLightingSize(const LightingControllerConfigs_t* lighting);
bool configWriteLighting(binary_writer_t* bw, const LightingControllerConfigs_t* lighting);

esp_err_t configValidate(const Config_t* config);

uint8_t configEncodeGpioPin(gpio_num_t pin);
bool configDecodeGpioPin(uint8_t encodedPin, GpioPinUse_t use, bool optional, gpio_num_t* pin);

bool br_ext_read_gpio(binary_reader_t* br, gpio_num_t* pin);
bool bw_ext_write_gpio(binary_writer_t* bw, gpio_num_t pin);

#ifdef __cplusplus
}
#endif // __cplusplus
