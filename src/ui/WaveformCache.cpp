#include "WaveformCache.h"
#include <algorithm>
#include <cmath>

void WaveformCache::rebuild (const juce::AudioBuffer<float>& buffer,
                             const std::array<SampleData::PeakMipmap, SampleData::kNumMipmapLevels>& mipmaps,
                             const TimelineMap::View& view, const FrameMap& toKit)
{
    const int numFrames = buffer.getNumSamples();
    const int widthPixels = view.width;
    if (numFrames <= 1 || widthPixels <= 0 || buffer.getNumChannels() < 1 || toKit == nullptr)
    {
        peaks.clear();
        present.clear();
        return;
    }

    const float* dataL = buffer.getReadPointer (0);
    const float* dataR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : dataL;
    if (dataL == nullptr)
    {
        peaks.clear();
        present.clear();
        return;
    }

    peaks.assign ((size_t) widthPixels, Peak {});
    present.assign ((size_t) widthPixels, false);
    const double samplesPerPixel = view.framesPerPixel();

    if (samplesPerPixel < 1.0)
    {
        // Sub-sample zoom: interpolate at exact fractional positions.
        for (int px = 0; px < widthPixels; ++px)
        {
            const double exactPos = view.start + px * samplesPerPixel;
            const int timelineFrame = (int) exactPos;
            const int kitFrame = toKit (timelineFrame);
            if (kitFrame < 0)
                continue;
            const float frac = (float) (exactPos - timelineFrame);
            const int ipos = std::max (0, std::min (kitFrame, numFrames - 2));
            const float valL = dataL[ipos] + (dataL[ipos + 1] - dataL[ipos]) * frac;
            const float valR = dataR[ipos] + (dataR[ipos + 1] - dataR[ipos]) * frac;
            const float val = (valL + valR) * 0.5f;
            peaks[(size_t) px] = { val, val };
            present[(size_t) px] = true;
        }
        return;
    }

    // The finest mipmap level that still gives at least one entry per pixel.
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

    for (int px = 0; px < widthPixels; ++px)
    {
        const int tStart = view.start + (int) (px * samplesPerPixel);
        const int tEnd = std::max (tStart + 1, view.start + (int) ((px + 1) * samplesPerPixel));
        const int kStart = toKit (tStart);
        if (kStart < 0)
            continue;

        // A pixel that straddles two slots takes the first slot's frames for its width.
        int kEnd = toKit (tEnd - 1);
        kEnd = (kEnd < kStart || kEnd - kStart > 2 * (tEnd - tStart)) ? kStart + (tEnd - tStart) : kEnd + 1;
        const int sStart = std::max (0, kStart);
        const int sEnd = std::min (kEnd, numFrames);
        if (sEnd <= sStart)
            continue;

        float hi = -1.0f;
        float lo = 1.0f;
        if (mip != nullptr)
        {
            const int spp = mip->samplesPerPeak;
            const int numMipPeaks = (int) mip->maxPeaks.size();
            int mipStart = sStart / spp;
            int mipEnd = (sEnd + spp - 1) / spp;
            mipStart = std::max (0, std::min (mipStart, numMipPeaks - 1));
            mipEnd = std::max (mipStart + 1, std::min (mipEnd, numMipPeaks));
            for (int m = mipStart; m < mipEnd; ++m)
            {
                hi = std::max (hi, mip->maxPeaks[(size_t) m]);
                lo = std::min (lo, mip->minPeaks[(size_t) m]);
            }
        }
        else
        {
            // No suitable mipmap (fewer samples per pixel than the smallest block): scan raw audio.
            for (int s = sStart; s < sEnd; ++s)
            {
                const float val = (dataL[s] + dataR[s]) * 0.5f;
                hi = std::max (hi, val);
                lo = std::min (lo, val);
            }
        }
        peaks[(size_t) px] = { hi, lo };
        present[(size_t) px] = true;
    }
}

void WaveformCache::paint (juce::Graphics& g, juce::Rectangle<int> lane, juce::Colour colour,
                           const TimelineMap::View& view, float verticalScale) const
{
    const int numPeaks = std::min ((int) peaks.size(), lane.getRight());
    if (numPeaks <= 0 || lane.isEmpty())
        return;

    const float cy = (float) lane.getCentreY();
    const float scale = (float) lane.getHeight() * verticalScale;
    const double samplesPerPixel = view.framesPerPixel();
    g.setColour (colour);

    int px = std::max (0, lane.getX());
    while (px < numPeaks)
    {
        while (px < numPeaks && ! present[(size_t) px])
            ++px;
        const int runStart = px;
        while (px < numPeaks && present[(size_t) px])
            ++px;
        const int runEnd = px;   // exclusive
        if (runEnd <= runStart)
            continue;

        if (samplesPerPixel < 1.0)
        {
            juce::Path path;
            path.startNewSubPath ((float) runStart, cy - peaks[(size_t) runStart].maxVal * scale);
            for (int x = runStart + 1; x < runEnd; ++x)
                path.lineTo ((float) x, cy - peaks[(size_t) x].maxVal * scale);
            g.strokePath (path, juce::PathStrokeType (1.5f));

            if (samplesPerPixel < 0.125)   // past 8 pixels per frame: a dot on each frame
            {
                constexpr float dotR = 2.5f;
                for (int x = runStart; x < runEnd; ++x)
                {
                    const double exactPos = view.start + x * samplesPerPixel;
                    if (exactPos - std::floor (exactPos) < samplesPerPixel)
                    {
                        const float y = cy - peaks[(size_t) x].maxVal * scale;
                        g.fillEllipse ((float) x - dotR, y - dotR, dotR * 2.0f, dotR * 2.0f);
                    }
                }
            }
            continue;
        }

        juce::Path fill;
        fill.startNewSubPath ((float) runStart, cy - peaks[(size_t) runStart].maxVal * scale);
        for (int x = runStart + 1; x < runEnd; ++x)
            fill.lineTo ((float) x, cy - peaks[(size_t) x].maxVal * scale);
        for (int x = runEnd - 1; x >= runStart; --x)
            fill.lineTo ((float) x, cy - peaks[(size_t) x].minVal * scale);
        fill.closeSubPath();
        g.fillPath (fill);

        // Few frames per pixel: the fill gets thinner than a pixel, so a centre line keeps it visible.
        if (samplesPerPixel < 8.0)
        {
            juce::Path mid;
            mid.startNewSubPath ((float) runStart, cy - (peaks[(size_t) runStart].maxVal + peaks[(size_t) runStart].minVal) * 0.5f * scale);
            for (int x = runStart + 1; x < runEnd; ++x)
                mid.lineTo ((float) x, cy - (peaks[(size_t) x].maxVal + peaks[(size_t) x].minVal) * 0.5f * scale);
            g.strokePath (mid, juce::PathStrokeType (1.5f));
        }
    }
}
