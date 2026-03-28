#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <numeric>

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
                                           float sensitivity = 1.0f,
                                           double sampleRate = 44100.0,
                                           float minSliceLenMs = 100.0f)
{
    std::vector<int> onsets;

    int numFrames = buffer.getNumSamples();
    if (numFrames == 0 || start < 0 || end <= start || end > numFrames)
        return onsets;

    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : L;

    // --- STFT parameters ---
    constexpr int fftOrder = 10;            // 2^10 = 1024
    constexpr int fftSize = 1 << fftOrder;  // 1024
    constexpr int hopSize = 256;            // ~5.8ms at 44.1kHz
    constexpr int numBins = fftSize / 2 + 1; // 513 magnitude bins
    constexpr int peakRadius = 3;
    constexpr float delta = 1e-4f;          // noise floor for silence suppression

    // Precompute Hann window
    std::vector<float> hannWindow (fftSize);
    for (int i = 0; i < fftSize; ++i)
        hannWindow[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize));

    juce::dsp::FFT fft (fftOrder);

    // --- Step 1: STFT and spectral flux ODF ---
    // FFT work buffer: needs 2*fftSize floats for performRealOnlyForwardTransform
    std::vector<float> fftData (2 * (size_t) fftSize, 0.0f);
    std::vector<float> prevMag (numBins, 0.0f);
    std::vector<float> currMag (numBins);
    std::vector<float> odf;

    for (int pos = start; pos + fftSize <= end; pos += hopSize)
    {
        // Fill FFT buffer with windowed mono signal
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
        {
            int idx = pos + i;
            float mono = (L[idx] + R[idx]) * 0.5f;
            fftData[(size_t) i] = mono * hannWindow[(size_t) i];
        }

        fft.performRealOnlyForwardTransform (fftData.data());

        // Compute magnitudes from interleaved real/imag pairs
        for (int k = 0; k < numBins; ++k)
        {
            float re = fftData[(size_t) (2 * k)];
            float im = fftData[(size_t) (2 * k + 1)];
            currMag[(size_t) k] = std::sqrt (re * re + im * im);
        }

        // Half-wave rectified spectral flux: sum of positive magnitude increases
        float flux = 0.0f;
        for (int k = 0; k < numBins; ++k)
        {
            float diff = currMag[(size_t) k] - prevMag[(size_t) k];
            if (diff > 0.0f)
                flux += diff;
        }

        odf.push_back (flux);
        std::swap (prevMag, currMag);
    }

    if (odf.size() < 5)
        return onsets;

    // --- Step 2: Adaptive threshold — moving median × multiplier ---
    float lambda = std::max (0.1f, sensitivity);
    int medianW = 10; // ~58ms lookaround at 256-sample hop / 44.1kHz

    std::vector<float> threshold (odf.size());
    std::vector<float> window;
    window.reserve ((size_t) (2 * medianW + 1));

    for (size_t i = 0; i < odf.size(); ++i)
    {
        size_t lo = i > (size_t) medianW ? i - (size_t) medianW : 0;
        size_t hi = std::min (i + (size_t) medianW, odf.size() - 1);

        window.clear();
        for (size_t j = lo; j <= hi; ++j)
            window.push_back (odf[j]);

        size_t mid = window.size() / 2;
        std::nth_element (window.begin(), window.begin() + (ptrdiff_t) mid, window.end());
        float median = window[mid];

        threshold[i] = delta + lambda * median;
    }

    // --- Step 3: Peak-pick — local maxima above adaptive threshold ---
    struct Onset
    {
        int samplePos;
        float strength;
    };
    std::vector<Onset> candidates;

    for (size_t i = 1; i < odf.size() - 1; ++i)
    {
        if (odf[i] <= threshold[i])
            continue;

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

        int samplePos = start + (int) i * hopSize + fftSize / 2;
        samplePos = std::min (samplePos, end);
        if (samplePos > start)
            candidates.push_back ({ samplePos, odf[i] });
    }

    // --- Step 4: MIN post-filter — greedy strongest-first, remove close neighbors ---
    int minOnsetDist = (int) std::round (sampleRate * (double) minSliceLenMs / 1000.0);
    minOnsetDist = std::max (1, minOnsetDist);

    // Sort by strength descending so strongest onsets survive
    std::sort (candidates.begin(), candidates.end(),
               [] (const Onset& a, const Onset& b) { return a.strength > b.strength; });

    std::vector<int> kept;
    for (const auto& c : candidates)
    {
        bool tooClose = false;
        for (int k : kept)
        {
            if (std::abs (c.samplePos - k) < minOnsetDist)
            {
                tooClose = true;
                break;
            }
        }
        if (! tooClose)
            kept.push_back (c.samplePos);
    }

    // Sort by position for output
    std::sort (kept.begin(), kept.end());
    onsets = std::move (kept);

    return onsets;
}

} // namespace AudioAnalysis
