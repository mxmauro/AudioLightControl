#pragma once

#include "utils/gpio_manager.h"
#include <driver/gpio.h>
#include <esp_err.h>
#include <iot_comm/utils/binary.h>
#include <stdint.h>

// -----------------------------------------------------------------------------

typedef struct LedStrip5050Config_s {
    gpio_num_t redGpioPin;
    gpio_num_t greenGpioPin;
    gpio_num_t blueGpioPin;
    bool isActiveLow;
} LedStrip5050Config_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

esp_err_t ledStrip5050ReadConfig(binary_reader_t* br, LedStrip5050Config_t* config, GpioPinValidatorHandle_t pinValidator);
size_t ledStrip5050CalcConfigSize(const LedStrip5050Config_t* config);
bool ledStrip5050WriteConfig(binary_writer_t* bw, const LedStrip5050Config_t* config);

esp_err_t ledStrip5050Init(const LedStrip5050Config_t* config);
void ledStrip5050Deinit();

esp_err_t ledStrip5050Set(uint16_t r, uint16_t g, uint16_t b);
esp_err_t ledStrip5050FadeTo(uint16_t r, uint16_t g, uint16_t b, uint32_t ms);
esp_err_t ledStrip5050SetAndFadeTo(uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd, uint16_t gEnd, uint16_t bEnd,
                                   uint32_t ms);

#ifdef __cplusplus
}
#endif // __cplusplus
