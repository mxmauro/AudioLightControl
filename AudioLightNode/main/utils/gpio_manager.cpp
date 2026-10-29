#include "gpio_manager.h"
#include <cassert>
#include <mutex.h>
#include <string.h>

// -----------------------------------------------------------------------------

typedef struct GpioPin_s {
    const char* owner;
    GpioPinUse_t use;
} GpioPin_t;

typedef struct GpioPinValidatorImpl_s {
    GpioPin_t pins[GPIO_NUM_MAX];
} GpioPinValidatorImpl_t;

// -----------------------------------------------------------------------------

static RWMutex rwMtx;
static GpioPin_t pins[GPIO_NUM_MAX] = {};

// -----------------------------------------------------------------------------

bool gpioPinManagerIsValid(gpio_num_t pin, GpioPinUse_t use)
{
    if (pin < 0 || pin >= GPIO_NUM_MAX)
    {
        return false;
    }

    switch (use)
    {
        case GpioPinUseInput:
            return GPIO_IS_VALID_GPIO(pin);

        case GpioPinUseOutput:
            return GPIO_IS_VALID_OUTPUT_GPIO(pin);

        default:
            return false;
    }
}

esp_err_t gpioPinManagerClaim(gpio_num_t pin, GpioPinUse_t use, const char* owner)
{
    // Validate pin
    if (!(owner && *owner != 0 && gpioPinManagerIsValid(pin, use)))
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Check and add to managed pins
    {
        AutoRWMutex lock(rwMtx, false);

        if (pins[pin].owner)
        {
            return ESP_ERR_INVALID_STATE;
        }

        pins[pin].owner = owner;
        pins[pin].use = use;
    }

    // Done
    return ESP_OK;
}

void gpioPinManagerRelease(gpio_num_t pin)
{
    AutoRWMutex lock(rwMtx, false);

    if (pin >= 0 && pin < GPIO_NUM_MAX)
    {
        memset(&pins[pin], 0, sizeof(GpioPin_t));
    }
}

const char* gpioPinManagerGetOwnerAndUse(gpio_num_t pin, GpioPinUse_t* use)
{
    AutoRWMutex lock(rwMtx, true);

    if (pin < 0 || pin >= GPIO_NUM_MAX)
    {
        if (use)
        {
            *use = GpioPinUseNone;
        }
        return nullptr;
    }
    if (use)
    {
        *use = pins[pin].use;
    }
    return pins[pin].owner;
}

GpioPinValidatorHandle_t gpioPinValidatorCreate()
{
    GpioPinValidatorImpl_t* validator;

    validator = static_cast<GpioPinValidatorImpl_t*>(malloc(sizeof(GpioPinValidatorImpl_t)));
    if (validator)
    {
        memset(validator, 0, sizeof(GpioPinValidatorImpl_t));
    }
    return validator;
}

void gpioPinValidatorDestroy(GpioPinValidatorHandle_t validator)
{
    if (validator)
    {
        memset(validator, 0, sizeof(GpioPinValidatorImpl_t));
        free(validator);
    }
}

esp_err_t gpioPinValidatorClaim(GpioPinValidatorHandle_t validator, gpio_num_t pin, GpioPinUse_t use)
{
    GpioPinValidatorImpl_t* impl = static_cast<GpioPinValidatorImpl_t*>(validator);

    assert(validator);

    if (pin < 0 || pin >= GPIO_NUM_MAX)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!gpioPinManagerIsValid(pin, use))
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (impl->pins[pin].use != GpioPinUseNone)
    {
        return ESP_ERR_INVALID_STATE;
    }

    impl->pins[pin].use = use;

    // Done
    return ESP_OK;
}
