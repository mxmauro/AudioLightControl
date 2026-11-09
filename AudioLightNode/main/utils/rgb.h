#pragma once

#include <driver/gpio.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// RGB color type

typedef struct RGBColor_s {
    uint16_t r;
    uint16_t g;
    uint16_t b;
} RGBColor_t;

typedef struct RGBColor8_s {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} RGBColor8_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

RGBColor_t rgbInterpolate(const RGBColor_t* start, const RGBColor_t* target, uint32_t elapsedMs, uint32_t totalMs);
RGBColor8_t rgbColorTo8(uint16_t r, uint16_t g, uint16_t b);
RGBColor8_t rgbInterpolate8(const RGBColor8_t* start, const RGBColor8_t* target, uint32_t elapsedMs, uint32_t totalMs);

#ifdef __cplusplus
}
#endif // __cplusplus
