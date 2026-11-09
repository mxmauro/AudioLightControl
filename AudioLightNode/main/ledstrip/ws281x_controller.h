#pragma once

#include "utils/gpio_manager.h"
#include <driver/gpio.h>
#include <esp_err.h>
#include <iot_comm/utils/binary.h>
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// WS281x configuration

#define WS281X_MAX_LEDS 300U

typedef struct LedStripWs281xConfig_s {
    gpio_num_t dataGpioPin;
    gpio_num_t mirrorGpioPin;
    uint16_t   ledCount;
} LedStripWs281xConfig_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

esp_err_t ledStripWs281xReadConfig(binary_reader_t* br, LedStripWs281xConfig_t* config, GpioPinValidatorHandle_t pinValidator);
size_t ledStripWs281xCalcConfigSize(const LedStripWs281xConfig_t* config);
bool ledStripWs281xWriteConfig(binary_writer_t* bw, const LedStripWs281xConfig_t* config);

esp_err_t ledStripWs281xInit(size_t slot, const LedStripWs281xConfig_t* config);
void ledStripWs281xDeinit(size_t slot);

esp_err_t ledStripWs281xSet(size_t slot, uint16_t r, uint16_t g, uint16_t b);
esp_err_t ledStripWs281xFadeTo(size_t slot, uint16_t r, uint16_t g, uint16_t b, uint32_t ms);
esp_err_t ledStripWs281xSetAndFadeTo(size_t slot, uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd, uint16_t gEnd,
                                     uint16_t bEnd, uint32_t ms);
esp_err_t ledStripWs281xSetPixels(size_t slot, uint16_t firstLed, const uint8_t* data, uint16_t ledCount);

#ifdef __cplusplus
}
#endif // __cplusplus
