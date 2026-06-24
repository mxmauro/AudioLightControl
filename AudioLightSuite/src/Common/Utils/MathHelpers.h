#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>

#define MATH_EPSILON 1e-5f

// -----------------------------------------------------------------------------

namespace MathHelpers {

static inline float Min(_In_ float a, _In_ float b)
{
    return (a <= b) ? a : b;
}

static inline float Max(_In_ float a, _In_ float b)
{
    return (a >= b) ? a : b;
}

static inline float Clamp(_In_ float x)
{
    if (x < 0.0f)
    {
        return 0.0f;
    }
    if (x > 1.0f)
    {
        return 1.0f;
    }
    return x;
}

static inline float SubstractWithMin(_In_ float x, _In_ float y, _In_ float fMin)
{
    x -= y;
    return (x >= fMin) ? x : 0.f;
}

static float RemapClamp(_In_ float x, _In_ float xMin, _In_ float xMax)
{
    if (xMax <= xMin)
    {
        return 0.0f;
    }
    return Clamp((x - xMin) / (xMax - xMin));
}

static inline float Frac(_In_ float x)
{
    return x - truncf(x);
}

static inline float Lerp(_In_ float a, _In_ float b, _In_ float t)
{
    return a + (b - a) * t;
}

static inline float GammaCorrect(_In_ float x, _In_ float gamma = 2.2f)
{
    x = Clamp(x);
    return powf(x, 1.0f / gamma);
}

static inline ULONG GetLuminance(_In_ BYTE r, _In_ BYTE g, _In_ BYTE b)
{
    return 2126UL * (ULONG)r + 7152UL * (ULONG)g + 722UL * (ULONG)b;
}

static inline float RmsToDb(_In_ float fRms)
{
    // RMS should be >= 0
    if (fRms < 0.0f)
    {
        fRms = 0.0f;
    }
    // db = 20*log10(rms)
    return 20.0f * log10f(fRms + MATH_EPSILON);
}

static inline VOID HSV2RGB(_In_ float h, _In_ float s, _In_ float v, _Out_ LPBYTE lpR, _Out_ LPBYTE lpG, _Out_ LPBYTE lpB)
{
    // h: 0.0 to 1.0
    // s: 0.0 to 1.0
    // v: 0.0 to 1.0
    h = h - (int)h; // Wrap to 0-1
    if (h < 0.0f)
    {
        h += 1.0f;
    }

    float fH = h * 6.0f;
    int iSector = (int)fH;
    float fFrac = fH - (float)iSector;

    float fP = v * (1.0f - s);
    float fQ = v * (1.0f - s * fFrac);
    float fT = v * (1.0f - s * (1.0f - fFrac));

    float fR, fG, fB;

    switch (iSector)
    {
        case 0:
            fR = v; fG = fT; fB = fP;
            break;
        case 1:
            fR = fQ; fG = v; fB = fP;
            break;
        case 2:
            fR = fP; fG = v; fB = fT;
            break;
        case 3:
            fR = fP; fG = fQ; fB = v;
            break;
        case 4:
            fR = fT; fG = fP; fB = v;
            break;
        default:
            fR = v; fG = fP; fB = fQ;
            break;
    }

    *lpR = (BYTE)(Clamp(fR) * 255.0f);
    *lpG = (BYTE)(Clamp(fG) * 255.0f);
    *lpB = (BYTE)(Clamp(fB) * 255.0f);
}

static inline USHORT be16(_In_ USHORT u)
{
    return ((u >> 8) & 0x00FF) |
           ((u << 8) & 0xFF00);
}

static inline ULONG be32(_In_ ULONG u)
{
    return ((u >> 24) & 0x000000FFUL) |
           ((u >>  8) & 0x0000FF00UL) |
           ((u <<  8) & 0x00FF0000UL) |
           ((u << 24) & 0xFF000000UL);
}

class CBiQuad
{
public:
    CBiQuad() = default;
    CBiQuad(_In_ const CBiQuad &) = default;
    CBiQuad(_Inout_ CBiQuad &&) = default;

    ~CBiQuad() = default;

    static CBiQuad CreateBandPass(_In_ float fs, _In_ float fLo, _In_ float fHi);
    static CBiQuad CreateBandPassWithQ(_In_ float fs, _In_ float f0, _In_ float Q);
    static CBiQuad CreateBandPassWithWidth(_In_ float fs, _In_ float fCenter, _In_ float fWidth);

    CBiQuad &operator=(_In_ const CBiQuad &) = default;
    CBiQuad &operator=(_Inout_ CBiQuad &&) = default;

    float Process(_In_ float x);

private:
    // Direct Form I
    float m_b0{ 0.0f };
    float m_b1{ 0.0f };
    float m_b2{ 0.0f };
    float m_a1{ 0.0f };
    float m_a2{ 0.0f };
    float m_z1{ 0.0f };
    float m_z2{ 0.0f };
};

class CEnvelope
{
public:
    CEnvelope() = default;
    CEnvelope(_In_ const CEnvelope &) = default;
    CEnvelope(_Inout_ CEnvelope &&) = default;

    ~CEnvelope() = default;

    CEnvelope &operator=(_In_ const CEnvelope &) = default;
    CEnvelope &operator=(_Inout_ CEnvelope &&) = default;

    // attack/release in seconds
    float Process(_In_ float signal, _In_ float sampleRate, _In_ float attackTimeSec,
                  _In_ float releaseTimeSec);

private:
    float m_currentLevel{ 0.0f };
};

} // namespace MathHelpers
