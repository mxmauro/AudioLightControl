#pragma once

#include <cstdint>
#include <stdlib.h>
#include <driver/gpio.h>

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct LedStrip5050Config_s {
    gpio_num_t redGpioPin{ GPIO_NUM_NC };
    gpio_num_t greenGpioPin{ GPIO_NUM_NC };
    gpio_num_t blueGpioPin{ GPIO_NUM_NC };
    bool       isActiveLow{false};
} LedStrip5050Config_t;

esp_err_t ledStrip5050Init(LedStrip5050Config_t *config);
void ledStrip5050Deinit();

esp_err_t ledStrip5050Set(uint16_t r, uint16_t g, uint16_t b);
esp_err_t ledStrip5050FadeTo(uint16_t r, uint16_t g, uint16_t b, uint32_t ms);
esp_err_t ledStrip5050SetAndFadeTo(uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd, uint16_t gEnd, uint16_t bEnd,
                                   uint32_t ms);

#ifdef __cplusplus
}
#endif // __cplusplus
