#pragma once

#include <driver/gpio.h>
#include <stdint.h>

// -----------------------------------------------------------------------------

typedef struct RGBColor_s {
    uint16_t r;
    uint16_t g;
    uint16_t b;
} RGBColor_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

RGBColor_t rgbInterpolate(const RGBColor_t* start, const RGBColor_t* target, uint32_t elapsedMs, uint32_t totalMs);

#ifdef __cplusplus
}
#endif // __cplusplus
