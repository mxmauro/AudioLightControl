#pragma once

#include <stdint.h>

// -----------------------------------------------------------------------------

typedef struct HSVColor_s {
    uint16_t h;
    uint16_t s;
    uint16_t v;
} HSVColor_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

HSVColor_t rgbToHsv(uint16_t r, uint16_t g, uint16_t b);
void hsvToRgb(const HSVColor_t* hsv, uint16_t* r, uint16_t* g, uint16_t* b);

// NOTE: amount ranges [0..10000] (10000 means 100%)
void hsvLighter(HSVColor_t* hsv, uint32_t amount);
void hsvDarker(HSVColor_t* hsv, uint32_t amount);
#ifdef __cplusplus
}
#endif // __cplusplus
