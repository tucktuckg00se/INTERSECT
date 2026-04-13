#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <deque>
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

// Result of the expensive STFT spectral flux computation.
struct ODFResult
{
    std::vector<float> odf;
    int hopSize  = 0;
    int fftSize  = 0;
    int start    = 0;
    int end      = 0;
};

// Phase 1 (expensive): Compute spectral flux onset detection function via STFT.
// This is the heavy part — ~5000 FFTs for a 30-second sample at 44.1kHz.
inline ODFResult computeSpectralFluxODF (const juce::AudioBuffer<float>& buffer,
                                          int start, int end,
                                          double /*sampleRate*/ = 44100.0)
{
    ODFResult result;
    result.start = start;
    result.end   = end;

    constexpr int fftOrder = 10;
    constexpr int fftSize  = 1 << fftOrder;  // 1024
    constexpr int hopSize  = 256;
    constexpr int numBins  = fftSize / 2 + 1;

    result.hopSize = hopSize;
    result.fftSize = fftSize;

    int numFrames = buffer.getNumSamples();
    if (numFrames == 0 || start < 0 || end <= start || end > numFrames)
        return result;

    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : L;

    // Precompute Hann window
    std::vector<float> hannWindow (fftSize);
    for (int i = 0; i < fftSize; ++i)
        hannWindow[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize));

    juce::dsp::FFT fft (fftOrder);

    std::vector<float> fftData (2 * (size_t) fftSize, 0.0f);
    std::vector<float> prevMag (numBins, 0.0f);
    std::vector<float> currMag (numBins);

    for (int pos = start; pos + fftSize <= end; pos += hopSize)
    {
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
        {
            int idx = pos + i;
            float mono = (L[idx] + R[idx]) * 0.5f;
            fftData[(size_t) i] = mono * hannWindow[(size_t) i];
        }

        fft.performRealOnlyForwardTransform (fftData.data());

        for (int k = 0; k < numBins; ++k)
        {
            float re = fftData[(size_t) (2 * k)];
            float im = fftData[(size_t) (2 * k + 1)];
            currMag[(size_t) k] = std::sqrt (re * re + im * im);
        }

        float flux = 0.0f;
        for (int k = 0; k < numBins; ++k)
        {
            float diff = currMag[(size_t) k] - prevMag[(size_t) k];
            if (diff > 0.0f)
                flux += diff;
        }

        result.odf.push_back (flux);
        std::swap (prevMag, currMag);
    }

    return result;
}

// Phase 2 (cheap): Pick transients from a pre-computed ODF.
// Runs adaptive threshold, peak-pick, backtrack refinement, and min-distance filter.
inline std::vector<int> pickTransientsFromODF (const ODFResult& odfResult,
                                                const juce::AudioBuffer<float>& buffer,
                                                float sensitivity = 1.0f,
                                                double sampleRate = 44100.0,
                                                float minSliceLenMs = 100.0f)
{
    std::vector<int> onsets;

    const auto& odf = odfResult.odf;
    const int hopSize = odfResult.hopSize;
    const int fftSize = odfResult.fftSize;
    const int start   = odfResult.start;
    const int end     = odfResult.end;

    if (odf.size() < 5)
        return onsets;

    constexpr int peakRadius = 3;
    constexpr float delta = 1e-4f;

    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : L;

    // --- Adaptive threshold — moving median x multiplier ---
    float lambda = std::max (0.1f, sensitivity);
    int medianW = 10;

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

    // --- Peak-pick — local maxima above adaptive threshold ---
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

    // --- Backtrack refinement — walk backward to true transient onset ---
    {
        constexpr float envWindowMs    = 0.7f;
        constexpr float noiseWindowMs  = 6.0f;
        constexpr float noiseGapMs     = 1.0f;
        constexpr float prerollMs      = 0.2f;
        constexpr float backtrackFraction = 0.1f;

        const int envHalf     = std::max (1, (int) std::round (sampleRate * envWindowMs   / 1000.0));
        const int noiseWinLen = std::max (1, (int) std::round (sampleRate * noiseWindowMs / 1000.0));
        const int noiseGap    = std::max (1, (int) std::round (sampleRate * noiseGapMs    / 1000.0));
        const int preroll     = std::max (0, (int) std::round (sampleRate * prerollMs     / 1000.0));

        // Pre-compute sliding window max envelope over [start, end) — O(n) total
        // Uses a monotonic deque for sliding window maximum.
        const int regionLen = end - start;
        std::vector<float> envArray ((size_t) regionLen);
        {
            // Pre-compute absolute mono values to avoid redundant per-sample calculation
            std::vector<float> absMono ((size_t) regionLen);
            for (int i = 0; i < regionLen; ++i)
                absMono[(size_t) i] = std::abs ((L[start + i] + R[start + i]) * 0.5f);

            std::deque<int> deq; // indices into absMono of descending local maxima

            // Process indices [0, regionLen + envHalf) so that center = i - envHalf
            // covers [0, regionLen) fully.
            const int loopEnd = regionLen + envHalf;
            for (int i = 0; i < loopEnd; ++i)
            {
                // Only push real samples; past regionLen the window just shrinks
                if (i < regionLen)
                {
                    while (! deq.empty() && absMono[(size_t) deq.back()] <= absMono[(size_t) i])
                        deq.pop_back();
                    deq.push_back (i);
                }

                // Evict elements that have fallen out of the window [center - envHalf, center + envHalf]
                int center = i - envHalf;
                int windowStart = center - envHalf;
                while (! deq.empty() && deq.front() < windowStart)
                    deq.pop_front();

                if (center >= 0 && center < regionLen && ! deq.empty())
                    envArray[(size_t) center] = absMono[(size_t) deq.front()];
            }
        }

        // O(1) envelope lookup (idx is in absolute sample coordinates)
        auto envelope = [&] (int idx) -> float
        {
            int rel = idx - start;
            if (rel < 0 || rel >= regionLen)
                return 0.0f;
            return envArray[(size_t) rel];
        };

        std::sort (candidates.begin(), candidates.end(),
                   [] (const Onset& a, const Onset& b) { return a.samplePos < b.samplePos; });

        for (size_t ci = 0; ci < candidates.size(); ++ci)
        {
            int pos = candidates[ci].samplePos;
            int earliest = start;
            if (ci > 0)
                earliest = std::max (earliest, candidates[ci - 1].samplePos + 1);

            int peakLo = std::max (earliest, pos - hopSize);
            int peakHi = std::min (end, pos + hopSize / 2);
            if (peakHi <= peakLo)
                continue;

            int peakPos = peakLo;
            float peakAmp = 0.0f;
            for (int s = peakLo; s < peakHi; ++s)
            {
                float env = envelope (s);
                if (env > peakAmp)
                {
                    peakAmp = env;
                    peakPos = s;
                }
            }

            int noiseEnd = std::max (start, peakLo - noiseGap);
            int noiseStart = std::max (start, noiseEnd - noiseWinLen);
            float noiseFloor = 0.0f;
            if (noiseEnd > noiseStart)
            {
                float sumSq = 0.0f;
                for (int s = noiseStart; s < noiseEnd; ++s)
                {
                    float v = std::abs ((L[s] + R[s]) * 0.5f);
                    sumSq += v * v;
                }
                noiseFloor = std::sqrt (sumSq / (float) (noiseEnd - noiseStart));
            }

            float thresh = noiseFloor + backtrackFraction * (peakAmp - noiseFloor);

            int refined = peakPos;
            for (int s = peakPos; s >= earliest; --s)
            {
                if (envelope (s) < thresh)
                {
                    refined = s + 1;
                    break;
                }
                refined = s;
            }

            refined = std::max (earliest, refined - preroll);

            candidates[ci].samplePos = refined;
        }
    }

    // --- MIN post-filter — greedy strongest-first, remove close neighbors ---
    int minOnsetDist = (int) std::round (sampleRate * (double) minSliceLenMs / 1000.0);
    minOnsetDist = std::max (1, minOnsetDist);

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

    std::sort (kept.begin(), kept.end());
    onsets = std::move (kept);

    return onsets;
}

// Convenience wrapper — calls both phases sequentially. Preserves the original API
// so existing call sites (if any outside AutoChopPanel) continue to work.
inline std::vector<int> detectTransients (const juce::AudioBuffer<float>& buffer,
                                           int start, int end,
                                           float sensitivity = 1.0f,
                                           double sampleRate = 44100.0,
                                           float minSliceLenMs = 100.0f)
{
    auto odf = computeSpectralFluxODF (buffer, start, end, sampleRate);
    return pickTransientsFromODF (odf, buffer, sensitivity, sampleRate, minSliceLenMs);
}

} // namespace AudioAnalysis
