#include "WaveformCache.h"
#include <algorithm>
#include <cmath>

void WaveformCache::rebuild (const juce::AudioBuffer<float>& buffer,
                             const std::array<SampleData::PeakMipmap, SampleData::kNumMipmapLevels>& mipmaps,
                             int numFrames, float zoom, float scroll, int widthPixels)
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

    if (buffer.getNumChannels() < 1)
    {
        peaks.clear();
        return;
    }

    const float* dataL = buffer.getReadPointer (0);
    const float* dataR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : dataL;

    if (dataL == nullptr)
    {
        peaks.clear();
        return;
    }

    if (samplesPerPixel < 1.0f)
    {
        // Sub-sample zoom: interpolate at exact fractional positions
        for (int px = 0; px < widthPixels; ++px)
        {
            float exactPos = visibleStart + px * samplesPerPixel;
            int ipos = (int) exactPos;
            float frac = exactPos - ipos;
            ipos = std::max (0, std::min (ipos, numFrames - 2));
            float valL = dataL[ipos] + (dataL[ipos + 1] - dataL[ipos]) * frac;
            float valR = dataR[ipos] + (dataR[ipos + 1] - dataR[ipos]) * frac;
            float val = (valL + valR) * 0.5f;
            peaks[(size_t) px] = { val, val };
        }
        return;
    }

    // Pick the best mipmap level: finest level that is coarser than 1 sample
    // but still finer than samplesPerPixel (so we get at least 1 mipmap entry per pixel)
    const SampleData::PeakMipmap* mip = nullptr;
    for (int i = SampleData::kNumMipmapLevels - 1; i >= 0; --i)
    {
        if (mipmaps[(size_t) i].samplesPerPeak > 0
            && mipmaps[(size_t) i].samplesPerPeak <= (int) samplesPerPixel)
        {
            mip = &mipmaps[(size_t) i];
            break;
        }
    }

    if (mip != nullptr)
    {
        int spp = mip->samplesPerPeak;
        int numMipPeaks = (int) mip->maxPeaks.size();

        for (int px = 0; px < widthPixels; ++px)
        {
            int sStart = visibleStart + (int) (px * samplesPerPixel);
            int sEnd   = visibleStart + (int) ((px + 1) * samplesPerPixel);
            sStart = std::max (0, sStart);
            sEnd   = std::min (sEnd, numFrames);

            int mipStart = sStart / spp;
            int mipEnd   = (sEnd + spp - 1) / spp;
            mipStart = std::max (0, std::min (mipStart, numMipPeaks - 1));
            mipEnd   = std::max (mipStart + 1, std::min (mipEnd, numMipPeaks));

            float hi = -1.0f;
            float lo = 1.0f;
            for (int m = mipStart; m < mipEnd; ++m)
            {
                if (mip->maxPeaks[(size_t) m] > hi) hi = mip->maxPeaks[(size_t) m];
                if (mip->minPeaks[(size_t) m] < lo) lo = mip->minPeaks[(size_t) m];
            }

            peaks[(size_t) px] = { hi, lo };
        }
    }
    else
    {
        // No suitable mipmap (samplesPerPixel < smallest mipmap block size) — scan raw audio
        for (int px = 0; px < widthPixels; ++px)
        {
            int sStart = visibleStart + (int) (px * samplesPerPixel);
            int sEnd   = visibleStart + (int) ((px + 1) * samplesPerPixel);
            sStart = std::max (0, sStart);
            sEnd   = std::min (sEnd, numFrames);

            float hi = -1.0f;
            float lo = 1.0f;
            for (int s = sStart; s < sEnd; ++s)
            {
                float val = (dataL[s] + dataR[s]) * 0.5f;
                if (val > hi) hi = val;
                if (val < lo) lo = val;
            }

            peaks[(size_t) px] = { hi, lo };
        }
    }
}
