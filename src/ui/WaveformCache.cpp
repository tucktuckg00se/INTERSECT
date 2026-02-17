#include "WaveformCache.h"
#include <algorithm>
#include <cmath>

void WaveformCache::rebuild (const juce::AudioBuffer<float>& buffer, int numFrames,
                             float zoom, float scroll, int widthPixels)
{
    if (numFrames <= 0 || widthPixels <= 0)
    {
        peaks.clear();
        return;
    }

    int visibleLen = (int) (numFrames / zoom);
    int visibleStart = (int) (scroll * (numFrames - visibleLen));
    visibleStart = std::max (0, std::min (visibleStart, numFrames - visibleLen));

    peaks.resize ((size_t) widthPixels);
    float samplesPerPixel = (float) visibleLen / (float) widthPixels;

    const float* dataL = buffer.getReadPointer (0);
    const float* dataR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : dataL;

    for (int px = 0; px < widthPixels; ++px)
    {
        int sStart = visibleStart + (int) (px * samplesPerPixel);
        int sEnd   = visibleStart + (int) ((px + 1) * samplesPerPixel);
        sStart = std::max (0, sStart);
        sEnd   = std::min (sEnd, numFrames);

        if (sStart >= sEnd)
        {
            // Sub-sample zoom: interpolate at the exact fractional position
            float exactPos = visibleStart + px * samplesPerPixel;
            int ipos = (int) exactPos;
            float frac = exactPos - ipos;
            ipos = std::max (0, std::min (ipos, numFrames - 2));
            float valL = dataL[ipos] + (dataL[ipos + 1] - dataL[ipos]) * frac;
            float valR = dataR[ipos] + (dataR[ipos + 1] - dataR[ipos]) * frac;
            float val = (valL + valR) * 0.5f;
            peaks[(size_t) px] = { val, val };
        }
        else
        {
            float peakMax = -1.0f;
            float peakMin = 1.0f;

            int step = std::max (1, (sEnd - sStart) / 256);
            for (int s = sStart; s < sEnd; s += step)
            {
                float val = (dataL[s] + dataR[s]) * 0.5f;
                if (val > peakMax) peakMax = val;
                if (val < peakMin) peakMin = val;
            }

            peaks[(size_t) px] = { peakMax, peakMin };
        }
    }
}
