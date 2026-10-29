#include "rgb.h"
#include "math.h"

// -----------------------------------------------------------------------------

RGBColor_t rgbInterpolate(const RGBColor_t* start, const RGBColor_t* target, uint32_t elapsedMs, uint32_t totalMs)
{
    RGBColor_t out;

    out.r = lerp_u16(start->r, target->r, elapsedMs, totalMs);
    out.g = lerp_u16(start->g, target->g, elapsedMs, totalMs);
    out.b = lerp_u16(start->b, target->b, elapsedMs, totalMs);
    return out;
}
