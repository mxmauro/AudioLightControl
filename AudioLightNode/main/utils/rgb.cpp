#include "rgb.h"
#include "math.h"

// -----------------------------------------------------------------------------
// RGB interpolation

RGBColor_t rgbInterpolate(const RGBColor_t* start, const RGBColor_t* target, uint32_t elapsedMs, uint32_t totalMs)
{
    RGBColor_t out;

    out.r = lerp_u16(start->r, target->r, elapsedMs, totalMs);
    out.g = lerp_u16(start->g, target->g, elapsedMs, totalMs);
    out.b = lerp_u16(start->b, target->b, elapsedMs, totalMs);
    return out;
}

RGBColor8_t rgbColorTo8(uint16_t r, uint16_t g, uint16_t b)
{
    RGBColor8_t out;

    out.r = static_cast<uint8_t>((static_cast<uint32_t>(r) + 128U) / 257U);
    out.g = static_cast<uint8_t>((static_cast<uint32_t>(g) + 128U) / 257U);
    out.b = static_cast<uint8_t>((static_cast<uint32_t>(b) + 128U) / 257U);
    return out;
}

RGBColor8_t rgbInterpolate8(const RGBColor8_t* start, const RGBColor8_t* target, uint32_t elapsedMs, uint32_t totalMs)
{
    RGBColor8_t out;

    out.r = static_cast<uint8_t>(lerp_u16(start->r, target->r, elapsedMs, totalMs));
    out.g = static_cast<uint8_t>(lerp_u16(start->g, target->g, elapsedMs, totalMs));
    out.b = static_cast<uint8_t>(lerp_u16(start->b, target->b, elapsedMs, totalMs));
    return out;
}
