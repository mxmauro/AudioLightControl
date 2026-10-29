#pragma once

#include "utils/gpio_manager.h"
#include <driver/gpio.h>
#include <esp_err.h>
#include <stdint.h>

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef void (*IrReceiverCodeHandler_t)(uint32_t code, void* ctx);

esp_err_t irReceiverInit(gpio_num_t gpioPin, IrReceiverCodeHandler_t handler, void* ctx);
void irReceiverDeinit();

#ifdef __cplusplus
}
#endif // __cplusplus
