#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace AudioAnalysis
{

inline int findNearestZeroCrossing (const juce::AudioBuffer<float>& buffer, int pos,
                                     int searchRange = 512)
{
    int numFrames = buffer.getNumSamples();
    if (numFrames == 0 || pos < 0 || pos >= numFrames)
        return pos;

    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : L;

    auto mono = [&] (int i) -> float { return (L[i] + R[i]) * 0.5f; };

    int bestPos = pos;
    int bestDist = searchRange + 1;

    int lo = std::max (1, pos - searchRange);
    int hi = std::min (numFrames - 1, pos + searchRange);

    for (int i = lo; i <= hi; ++i)
    {
        float a = mono (i - 1);
        float b = mono (i);
        if ((a >= 0.0f && b < 0.0f) || (a < 0.0f && b >= 0.0f))
        {
            int dist = std::abs (i - pos);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestPos = i;
            }
        }
    }

    return bestPos;
}

inline std::vector<int> detectTransients (const juce::AudioBuffer<float>& buffer,
                                           int start, int end,
                                           float sensitivity = 1.0f)
{
    std::vector<int> onsets;

    int numFrames = buffer.getNumSamples();
    if (numFrames == 0 || start < 0 || end <= start || end > numFrames)
        return onsets;

    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : L;

    constexpr int windowSize = 1024;
    constexpr int hopSize = 256;
    constexpr int minOnsetDist = 4410;  // ~100ms at 44.1kHz
    constexpr int peakRadius = 3;       // local max must beat 3 neighbours each side

    // Step 1: Compute RMS energy per window
    std::vector<float> energy;
    for (int pos = start; pos + windowSize <= end; pos += hopSize)
    {
        float sum = 0.0f;
        for (int i = 0; i < windowSize; ++i)
        {
            int idx = pos + i;
            float m = (L[idx] + R[idx]) * 0.5f;
            sum += m * m;
        }
        energy.push_back (std::sqrt (sum / windowSize));
    }

    if (energy.size() < 5)
        return onsets;

    // Step 2: Compute onset detection function — ratio of energy increase
    // Uses log-ratio: log(energy[i] / energy[i-1]) when energy rises
    // This normalizes by current level, so a hit in a quiet section
    // scores the same as a hit in a loud section.
    std::vector<float> odf;
    odf.push_back (0.0f);
    for (size_t i = 1; i < energy.size(); ++i)
    {
        float prev = energy[i - 1];
        float curr = energy[i];
        if (curr > prev && prev > 1e-8f)
            odf.push_back (std::log (curr / prev));
        else if (curr > 1e-8f && prev <= 1e-8f)
            odf.push_back (10.0f);  // silence-to-sound: strong onset
        else
            odf.push_back (0.0f);
    }

    // Step 3: Compute global threshold from sorted ODF values
    // Sensitivity controls which percentile we pick as the cutoff.
    // sensitivity 1.0 -> low percentile (many detections)
    // sensitivity 0.0 -> high percentile (only biggest hits)
    std::vector<float> sorted;
    std::copy_if (odf.begin(), odf.end(), std::back_inserter (sorted),
                  [] (float v) { return v > 0.0f; });

    if (sorted.empty())
        return onsets;

    std::sort (sorted.begin(), sorted.end());

    float s = juce::jlimit (0.0f, 1.0f, sensitivity);
    // Map sensitivity to percentile: 1.0 -> 50th percentile, 0.0 -> 99.5th
    float percentile = 0.995f - s * 0.495f;
    size_t threshIdx = std::min ((size_t) (percentile * (float) sorted.size()),
                                  sorted.size() - 1);
    float threshold = sorted[threshIdx];

    // Step 4: Peak-pick — only accept positions where ODF is a local maximum
    // AND exceeds the threshold AND respects minimum onset distance
    int lastOnsetSample = start - minOnsetDist;

    for (size_t i = 1; i < odf.size() - 1; ++i)
    {
        if (odf[i] <= threshold)
            continue;

        // Check local maximum within peakRadius
        bool isLocalMax = true;
        for (int k = 1; k <= peakRadius && isLocalMax; ++k)
        {
            if (i >= (size_t) k && odf[i - (size_t) k] >= odf[i])
                isLocalMax = false;
            if (i + (size_t) k < odf.size() && odf[i + (size_t) k] >= odf[i])
                isLocalMax = false;
        }

        if (! isLocalMax)
            continue;

        // Offset by half window to compensate for analysis latency:
        // the energy window starting at pos integrates windowSize samples,
        // so the actual transient sits roughly half a window into it.
        int samplePos = start + (int) i * hopSize + windowSize / 2;
        samplePos = std::min (samplePos, end);
        if (samplePos - lastOnsetSample >= minOnsetDist && samplePos > start)
        {
            onsets.push_back (samplePos);
            lastOnsetSample = samplePos;
        }
    }

    return onsets;
}

} // namespace AudioAnalysis
