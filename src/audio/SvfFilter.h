#pragma once
#include <algorithm>
#include <cmath>

struct SvfFilter
{
    void reset()
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    float process (float input, float cutoffHz, float q, float sampleRate, int type)
    {
        constexpr float kPi = 3.14159265358979323846f;
        const float maxCutoff = std::max (20.0f, sampleRate * 0.49f);
        const float cutoff = std::clamp (cutoffHz, 20.0f, maxCutoff);
        const float safeQ = std::max (0.5f, q);
        const float g = std::tan (kPi * cutoff / sampleRate);
        const float k = 1.0f / safeQ;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = input - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;

        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        const float low = v2;
        const float band = v1;
        const float high = input - k * v1 - v2;
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
