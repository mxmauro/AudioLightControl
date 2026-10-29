#include "hsv.h"
#include <assert.h>
#include <stdint.h>

// -----------------------------------------------------------------------------

#define HSV_HUE_120_DEGREES 21845U
#define HSV_HUE_240_DEGREES 43690U
#define HSV_HUE_CIRCLE      65535U

#define HSV_SCALE 65535U

static uint16_t divideRoundU32(uint32_t numerator, uint32_t denominator);
static uint16_t max3u16(uint16_t a, uint16_t b, uint16_t c);
static uint16_t min3u16(uint16_t a, uint16_t b, uint16_t c);

// -----------------------------------------------------------------------------

HSVColor_t rgbToHsv(uint16_t r, uint16_t g, uint16_t b)
{
    HSVColor_t hsv;
    uint16_t maxValue = max3u16(r, g, b);
    uint16_t minValue = min3u16(r, g, b);
    uint16_t delta = maxValue - minValue;

    hsv.v = maxValue;
    if (maxValue == 0 || delta == 0)
    {
        hsv.h = 0;
        hsv.s = 0;
        return hsv;
    }

    hsv.s = divideRoundU32(static_cast<uint32_t>(delta) * HSV_SCALE, maxValue);
    if (hsv.s == 0)
    {
        hsv.h = 0;
        return hsv;
    }

    if (maxValue == r)
    {
        if (g >= b)
        {
            hsv.h = static_cast<uint16_t>((static_cast<uint32_t>(g - b) * HSV_HUE_CIRCLE) / (static_cast<uint32_t>(delta) * 6U));
        }
        else
        {
            hsv.h = HSV_HUE_CIRCLE -
                    static_cast<uint16_t>((static_cast<uint32_t>(b - g) * HSV_HUE_CIRCLE) / (static_cast<uint32_t>(delta) * 6U));
        }
    }
    else if (maxValue == g)
    {
        if (b >= r)
        {
            hsv.h = HSV_HUE_120_DEGREES +
                    static_cast<uint16_t>((static_cast<uint32_t>(b - r) * HSV_HUE_CIRCLE) / (static_cast<uint32_t>(delta) * 6U));
        }
        else
        {
            hsv.h = HSV_HUE_120_DEGREES -
                    static_cast<uint16_t>((static_cast<uint32_t>(r - b) * HSV_HUE_CIRCLE) / (static_cast<uint32_t>(delta) * 6U));
        }
    }
    else
    {
        if (r >= g)
        {
            hsv.h = HSV_HUE_240_DEGREES +
                    static_cast<uint16_t>((static_cast<uint32_t>(r - g) * HSV_HUE_CIRCLE) / (static_cast<uint32_t>(delta) * 6U));
        }
        else
        {
            hsv.h = HSV_HUE_240_DEGREES -
                    static_cast<uint16_t>((static_cast<uint32_t>(g - r) * HSV_HUE_CIRCLE) / (static_cast<uint32_t>(delta) * 6U));
        }
    }

    return hsv;
}

void hsvToRgb(const HSVColor_t* hsv, uint16_t* r, uint16_t* g, uint16_t* b)
{
    uint32_t hueScaled;
    uint32_t sector;
    uint32_t remainder;
    uint16_t chroma;
    uint16_t secondary;
    uint16_t match;
    uint16_t rd;
    uint16_t gd;
    uint16_t bd;

    assert(hsv);
    assert(r);
    assert(g);
    assert(b);

    if (hsv->s == 0)
    {
        *r = hsv->v;
        *g = hsv->v;
        *b = hsv->v;
        return;
    }

    chroma = divideRoundU32(static_cast<uint32_t>(hsv->v) * static_cast<uint32_t>(hsv->s), HSV_SCALE);
    hueScaled = static_cast<uint32_t>(hsv->h) * 6U;
    sector = hueScaled >> 16;
    remainder = hueScaled & 0xFFFFU;

    if ((sector & 1U) == 0)
    {
        secondary = divideRoundU32(static_cast<uint32_t>(chroma) * remainder, HSV_SCALE);
    }
    else
    {
        secondary = divideRoundU32(static_cast<uint32_t>(chroma) * (HSV_SCALE - remainder), HSV_SCALE);
    }

    match = hsv->v - chroma;

    switch (sector)
    {
        case 0:
            rd = chroma;
            gd = secondary;
            bd = 0;
            break;
        case 1:
            rd = secondary;
            gd = chroma;
            bd = 0;
            break;
        case 2:
            rd = 0;
            gd = chroma;
            bd = secondary;
            break;
        case 3:
            rd = 0;
            gd = secondary;
            bd = chroma;
            break;
        case 4:
            rd = secondary;
            gd = 0;
            bd = chroma;
            break;
        default:
            rd = chroma;
            gd = 0;
            bd = secondary;
            break;
    }

    *r = rd + match;
    *g = gd + match;
    *b = bd + match;
}

void hsvLighter(HSVColor_t* hsv, uint32_t amount)
{
    uint32_t delta;

    assert(hsv);

    if (amount > 10000U)
    {
        amount = 10000U;
    }

    if (hsv->v < 65535U)
    {
        delta = (static_cast<uint32_t>(hsv->v) * amount + 9999U) / 10000U;
        if (delta == 0 && hsv->v > 0)
        {
            delta = 1;
        }

        if (delta < 65535U - static_cast<uint32_t>(hsv->v))
        {
            hsv->v += static_cast<uint16_t>(delta);
        }
        else
        {
            hsv->v = 65535U;
        }
    }
    else if (hsv->s > 0)
    {
        delta = (static_cast<uint32_t>(hsv->s) * amount + 9999U) / 10000U;

        if (delta < hsv->s)
        {
            hsv->s -= static_cast<uint16_t>(delta);
        }
        else
        {
            hsv->s = 0;
        }
    }
}

void hsvDarker(HSVColor_t* hsv, uint32_t amount)
{
    uint32_t delta;

    assert(hsv);

    if (amount > 10000U)
    {
        amount = 10000U;
    }

    delta = (static_cast<uint32_t>(hsv->v) * amount + 9999U) / 10000U;

    if (delta < hsv->v)
    {
        hsv->v -= static_cast<uint16_t>(delta);
    }
    else
    {
        hsv->v = 0;
    }
}

// -----------------------------------------------------------------------------

static uint16_t divideRoundU32(uint32_t numerator, uint32_t denominator)
{
    uint32_t quotient;
    uint32_t remainder;
    uint32_t roundThreshold;

    quotient = numerator / denominator;
    remainder = numerator % denominator;
    roundThreshold = denominator - (denominator / 2U);

    if (remainder >= roundThreshold)
    {
        quotient++;
    }
    return static_cast<uint16_t>(quotient);
}

static uint16_t max3u16(uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t maxValue = a;

    if (b > maxValue)
    {
        maxValue = b;
    }
    if (c > maxValue)
    {
        maxValue = c;
    }
    return maxValue;
}

static uint16_t min3u16(uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t minValue = a;

    if (b < minValue)
    {
        minValue = b;
    }
    if (c < minValue)
    {
        minValue = c;
    }
    return minValue;
}
