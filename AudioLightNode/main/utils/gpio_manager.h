#pragma once

#include <driver/gpio.h>
#include <esp_err.h>

// -----------------------------------------------------------------------------

typedef enum GpioPinUse_e {
    GpioPinUseNone = 0,
    GpioPinUseInput,
    GpioPinUseOutput,
} GpioPinUse_t;

typedef void* GpioPinValidatorHandle_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

bool gpioPinManagerIsValid(gpio_num_t pin, GpioPinUse_t use);

esp_err_t gpioPinManagerClaim(gpio_num_t pin, GpioPinUse_t use, const char* owner);
void gpioPinManagerRelease(gpio_num_t pin);

const char* gpioPinManagerGetOwnerAndUse(gpio_num_t pin, GpioPinUse_t* use);

GpioPinValidatorHandle_t gpioPinValidatorCreate();
void gpioPinValidatorDestroy(GpioPinValidatorHandle_t validator);
esp_err_t gpioPinValidatorClaim(GpioPinValidatorHandle_t validator, gpio_num_t pin, GpioPinUse_t use);

#ifdef __cplusplus
}
#endif // __cplusplus
