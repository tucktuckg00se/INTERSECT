#pragma once
#include <algorithm>
#include <cmath>

struct SvfFilter
{
    struct SvfCoeffs
    {
        float a1 = 0.0f;
        float a2 = 0.0f;
        float a3 = 0.0f;
        float k  = 0.0f;
    };

    void reset()
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    static SvfCoeffs computeCoeffs (float cutoffHz, float q, float sampleRate)
    {
        constexpr float kPi = 3.14159265358979323846f;
        const float safeSampleRate = std::max (1.0f, sampleRate);
        const float maxCutoff = std::max (20.0f, safeSampleRate * 0.49f);
        const float cutoff = std::clamp (cutoffHz, 20.0f, maxCutoff);
        const float safeQ = std::max (0.5f, q);
        const float g = std::tan (kPi * cutoff / safeSampleRate);

        SvfCoeffs c;
        c.k = 1.0f / safeQ;
        c.a1 = 1.0f / (1.0f + g * (g + c.k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }

    float process (float input, const SvfCoeffs& c, int type)
    {
        const float v3 = input - ic2eq;
        const float v1 = c.a1 * ic1eq + c.a2 * v3;
        const float v2 = ic2eq + c.a2 * ic1eq + c.a3 * v3;

        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        const float low = v2;
        const float band = v1;
        const float high = input - c.k * v1 - v2;
        const float notch = high + low;

        switch (type)
        {
            case 1:  return high;
            case 2:  return band;
            case 3:  return notch;
            default: return low;
        }
    }

private:
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
};
