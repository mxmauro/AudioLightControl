#include "MathHelpers.h"

// -----------------------------------------------------------------------------

namespace MathHelpers
{

CBiQuad CBiQuad::CreateBandPass(_In_ float fs, _In_ float fLo, _In_ float fHi)
{
    float fCenter = (fLo + fHi) / 2.0f;
    return CreateBandPassWithQ(fs, fCenter, fCenter / (fHi - fLo));
}

CBiQuad CBiQuad::CreateBandPassWithWidth(_In_ float fs, _In_ float fCenter, _In_ float fWidth)
{
    return CreateBandPassWithQ(fs, fCenter, fCenter / (fWidth * 2.0f));
}

CBiQuad CBiQuad::CreateBandPassWithQ(_In_ float fs, _In_ float f0, _In_ float Q)
{
    CBiQuad cBiQuad;

    // RBJ Audio EQ Cookbook (bandpass, constant skirt gain, peak gain = Q)
    const float w0 = 2.0f * 3.14159265358979323846f * (f0 / fs);
    const float cw = cosf(w0);
    const float sw = sinf(w0);
    const float alpha = sw / (2.0f * Q);

    const float m_b0 = alpha;
    const float m_b1 = 0.0f;
    const float m_b2 = -alpha;
    const float a0 = 1.0f + alpha;
    const float m_a1 = -2.0f * cw;
    const float m_a2 = 1.0f - alpha;

    cBiQuad.m_b0 = m_b0 / a0;
    cBiQuad.m_b1 = m_b1 / a0;
    cBiQuad.m_b2 = m_b2 / a0;
    cBiQuad.m_a1 = m_a1 / a0;
    cBiQuad.m_a2 = m_a2 / a0;
    return cBiQuad;
}

float CBiQuad::Process(_In_ float x)
{
    // Transposed DF2 is often nicer for stability, but this is fine at audio rates
    float y = m_b0 * x + m_z1;
    m_z1 = m_b1 * x - m_a1 * y + m_z2;
    m_z2 = m_b2 * x - m_a2 * y;
    return y;
}

float CEnvelope::Process(_In_ float signal, _In_ float sampleRate, _In_ float attackTimeSec,
                         _In_ float releaseTimeSec)
{
    if (attackTimeSec < 1e-5f)
    {
        attackTimeSec = 1e-5f;
    }
    if (releaseTimeSec < 1e-5f)
    {
        releaseTimeSec = 1e-5f;
    }

    const float attackSmoothing = expf(-1.0f / (sampleRate * attackTimeSec));
    const float releaseSmoothing = expf(-1.0f / (sampleRate * releaseTimeSec));

    if (signal > m_currentLevel)
    {
        m_currentLevel = attackSmoothing * m_currentLevel + (1.0f - attackSmoothing) * signal;
    }
    else
    {
        m_currentLevel = releaseSmoothing * m_currentLevel + (1.0f - releaseSmoothing) * signal;
    }
    return m_currentLevel;
}

} // namespace MathHelpers
