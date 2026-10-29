#pragma once
#include "StemSeparation.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <memory>
#include <vector>

// Host-side signal processing for spectral stem models (StemRuntimeKind::spectral and
// spectralMask), kept free of ONNX Runtime so it can be unit tested.

/**
    torch.stft / torch.istft of one channel as spectral models were trained with: center = true
    (reflect-padded by nFft / 2), onesided, Hann (periodic) or rectangular window, optionally
    normalized by 1 / sqrt (nFft).

    Every input is zero-padded at the end to `signalSamples` first, so the frame count is fixed:
    1 + signalSamples / hop. Spectra are two planes (re, im) of [bin][frame].
*/
class StemStft
{
public:
    StemStft (int nFft, int hopSize, bool hannWindow, bool normalized, int signalSamples);
    ~StemStft();

    int getNumBins() const noexcept;
    int getNumFrames() const noexcept;
    int getSignalSamples() const noexcept;
    size_t getPlaneSize() const noexcept { return (size_t) getNumBins() * (size_t) getNumFrames(); }

    void forward (const float* x, int numSamples, float* re, float* im);
    void inverse (const float* re, const float* im, float* out, int numSamples);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/** Samples a spectral model's STFT runs over: the segment, or more when the model's frame count
    needs it (SCNet pads its 485100-sample segments to 486400). */
int getStemSpectralSignalSamples (const StemSpectralParams& params);

/**
    The segmenting and blending of Music-Source-Separation-Training's demix() (generic mode): segments
    of segmentSamples every strideSamples until one reaches the end, linear fades of a tenth of a
    segment (none into the first, none out of the last), the input reflected out by
    segmentSamples - strideSamples at both ends when it is long enough, and a final short segment
    reflected out when more than half of it is input, else zero-padded.

    processSegment gets a stereo segment of exactly segmentSamples and must fill `outputs` (already
    sized to numOutputs stereo buffers of segmentSamples). Returning false aborts. On success
    `results` holds numOutputs stereo buffers as long as `mix`.
*/
using StemSegmentProcessor = std::function<bool (const juce::AudioBuffer<float>& segment,
                                                 std::vector<juce::AudioBuffer<float>>& outputs,
                                                 int segmentIndex, int numSegments)>;

bool runStemSegments (const juce::AudioBuffer<float>& mix,
                      int segmentSamples,
                      int strideSamples,
                      int numOutputs,
                      const StemSegmentProcessor& processSegment,
                      std::vector<juce::AudioBuffer<float>>& results);
