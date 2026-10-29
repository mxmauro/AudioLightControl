#pragma once

#include "gpio_manager.h"
#include "output/output_controller.h"
#include <driver/gpio.h>
#include <esp_err.h>
#include <iot_comm/utils/binary.h>

#define ONBOARD_LED_GPIO_PIN  GPIO_NUM_8
#define RESET_BUTTON_GPIO_PIN GPIO_NUM_NC
#define IR_RECEIVER_GPIO_PIN  GPIO_NUM_NC

// -----------------------------------------------------------------------------

typedef struct Config_s {
    OutputControllerConfig_t output;
} Config_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

esp_err_t configLoad(Config_t* config);
esp_err_t configSave(const Config_t* config);

esp_err_t configRead(binary_reader_t* br, Config_t* config);
size_t configCalcSize(const Config_t* config);
bool configWrite(binary_writer_t* bw, const Config_t* config);

uint8_t configEncodeGpioPin(gpio_num_t pin);
bool configDecodeGpioPin(uint8_t encodedPin, GpioPinUse_t use, bool optional, gpio_num_t* pin);

bool br_ext_read_gpio(binary_reader_t* br, gpio_num_t* pin);
bool bw_ext_write_gpio(binary_writer_t* bw, gpio_num_t pin);

#ifdef __cplusplus
}
#endif // __cplusplus
