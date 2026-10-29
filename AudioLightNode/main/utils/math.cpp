#include "math.h"

// -----------------------------------------------------------------------------

uint16_t lerp_u16(uint16_t a, uint16_t b, uint32_t pos, uint32_t total)
{
    uint32_t value;

    if (pos >= total)
    {
        return b;
    }
    if (b >= a)
    {
        value = static_cast<uint32_t>(a) + (static_cast<uint32_t>(b - a) * pos) / total;
    }
    else
    {
        value = static_cast<uint32_t>(a) - (static_cast<uint32_t>(a - b) * pos) / total;
    }
    if (value > 65535)
    {
        return 65535;
    }
    return static_cast<uint16_t>(value);
}
