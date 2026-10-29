#include "StemSpectral.h"
#include "signalsmith-linear/fft.h"
#include <cmath>
#include <complex>

struct StemStft::Impl
{
    Impl (int fftSize, int hopSize, bool hannWindow, bool normalizedScale, int signalSamples)
        : nFft (fftSize),
          hop (hopSize),
          bins (fftSize / 2 + 1),
          frames (1 + signalSamples / hopSize),
          paddedLen (signalSamples),
          normalized (normalizedScale),
          fft ((size_t) fftSize),
          window ((size_t) fftSize, 1.0f),
          padded ((size_t) (signalSamples + fftSize)),
          frameTime ((size_t) fftSize),
          frameFreq ((size_t) (fftSize / 2)),
          overlap ((size_t) (signalSamples + fftSize)),
          envelope ((size_t) (signalSamples + fftSize), 0.0f)
    {
        if (hannWindow)
            for (int i = 0; i < nFft; ++i)
                window[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * juce::MathConstants<double>::pi * i / nFft);

        // Overlap-added squared window, as torch.istft divides by.
        for (int t = 0; t < frames; ++t)
            for (int i = 0; i < nFft; ++i)
                envelope[(size_t) (t * hop + i)] += window[(size_t) i] * window[(size_t) i];
    }

    // x zero-padded to paddedLen, then reflect-padded by nFft / 2 on both sides (numpy "reflect").
    void fillPadded (const float* x, int numSamples)
    {
        const int half = nFft / 2;
        const int n = juce::jmin (numSamples, paddedLen);
        std::fill (padded.begin(), padded.end(), 0.0f);
        std::copy (x, x + n, padded.begin() + half);

        const float* body = padded.data() + half;
        for (int i = 0; i < half; ++i)
        {
            padded[(size_t) i] = body[half - i];
            padded[(size_t) (half + paddedLen + i)] = body[paddedLen - 2 - i];
        }
    }

    void forward (const float* x, int numSamples, float* re, float* im)
    {
        fillPadded (x, numSamples);
        const float scale = normalized ? 1.0f / std::sqrt ((float) nFft) : 1.0f;

        for (int t = 0; t < frames; ++t)
        {
            const float* src = padded.data() + t * hop;
            for (int i = 0; i < nFft; ++i)
                frameTime[(size_t) i] = src[i] * window[(size_t) i];

            fft.fft (frameTime.data(), frameFreq.data());

            // signalsmith packs DC and Nyquist (both real) into bin 0.
            re[(size_t) t] = frameFreq[0].real() * scale;
            im[(size_t) t] = 0.0f;
            re[(size_t) ((bins - 1) * frames + t)] = frameFreq[0].imag() * scale;
            im[(size_t) ((bins - 1) * frames + t)] = 0.0f;
            for (int k = 1; k < bins - 1; ++k)
            {
                re[(size_t) (k * frames + t)] = frameFreq[(size_t) k].real() * scale;
                im[(size_t) (k * frames + t)] = frameFreq[(size_t) k].imag() * scale;
            }
        }
    }

    void inverse (const float* re, const float* im, float* out, int numSamples)
    {
        // Undo the forward scaling and the unnormalized inverse FFT's factor of nFft.
        const float scale = (normalized ? std::sqrt ((float) nFft) : 1.0f) / (float) nFft;
        std::fill (overlap.begin(), overlap.end(), 0.0f);

        for (int t = 0; t < frames; ++t)
        {
            // Imaginary parts of DC and Nyquist are ignored, as irfft does.
            frameFreq[0] = { re[(size_t) t], re[(size_t) ((bins - 1) * frames + t)] };
            for (int k = 1; k < bins - 1; ++k)
                frameFreq[(size_t) k] = { re[(size_t) (k * frames + t)], im[(size_t) (k * frames + t)] };

            fft.ifft (frameFreq.data(), frameTime.data());

            float* dst = overlap.data() + t * hop;
            for (int i = 0; i < nFft; ++i)
                dst[i] += frameTime[(size_t) i] * scale * window[(size_t) i];
        }

        const int half = nFft / 2;
        const int n = juce::jmin (numSamples, paddedLen);
        for (int i = 0; i < n; ++i)
        {
            const float env = envelope[(size_t) (half + i)];
            out[i] = env > 1.0e-11f ? overlap[(size_t) (half + i)] / env : 0.0f;
        }
        for (int i = n; i < numSamples; ++i)
            out[i] = 0.0f;
    }

    int nFft, hop, bins, frames, paddedLen;
    bool normalized;
    signalsmith::linear::RealFFT<float> fft;
    std::vector<float> window;
    std::vector<float> padded;
    std::vector<float> frameTime;
    std::vector<std::complex<float>> frameFreq;
    std::vector<float> overlap;
    std::vector<float> envelope;
};

StemStft::StemStft (int nFft, int hopSize, bool hannWindow, bool normalized, int signalSamples)
    : impl (std::make_unique<Impl> (nFft, hopSize, hannWindow, normalized, signalSamples))
{
}

StemStft::~StemStft() = default;

int StemStft::getNumBins() const noexcept { return impl->bins; }
int StemStft::getNumFrames() const noexcept { return impl->frames; }
int StemStft::getSignalSamples() const noexcept { return impl->paddedLen; }

void StemStft::forward (const float* x, int numSamples, float* re, float* im)
{
    impl->forward (x, numSamples, re, im);
}

void StemStft::inverse (const float* re, const float* im, float* out, int numSamples)
{
    impl->inverse (re, im, out, numSamples);
}

int getStemSpectralSignalSamples (const StemSpectralParams& params)
{
    return juce::jmax (params.segmentSamples, (params.frames - 1) * params.hopSize);
}

// ── Segmenting ──────────────────────────────────────────────────────────

namespace
{
// numpy.pad (mode="reflect") of one channel by `before` and `after`; both must be < length.
void reflectPad (const float* x, int length, int before, int after, float* out)
{
    for (int i = 0; i < before; ++i)
        out[i] = x[before - i];
    std::copy (x, x + length, out + before);
    for (int i = 0; i < after; ++i)
        out[before + length + i] = x[length - 2 - i];
}
}

bool runStemSegments (const juce::AudioBuffer<float>& mix,
                      int segmentSamples,
                      int strideSamples,
                      int numOutputs,
                      const StemSegmentProcessor& processSegment,
                      std::vector<juce::AudioBuffer<float>>& results)
{
    const int length = mix.getNumSamples();
    const int segLen = segmentSamples;
    const int stride = juce::jmax (1, strideSamples);
    if (length <= 0 || segLen <= 1 || numOutputs <= 0)
        return false;

    const int border = segLen - stride;
    const bool wide = border > 0 && length > 2 * border;

    juce::AudioBuffer<float> source (2, wide ? length + 2 * border : length);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = mix.getReadPointer (juce::jmin (ch, mix.getNumChannels() - 1));
        if (wide)
            reflectPad (src, length, border, border, source.getWritePointer (ch));
        else
            source.copyFrom (ch, 0, src, length);
    }

    const int total = source.getNumSamples();
    const int fade = segLen / 10;
    const int numSegments = total > segLen ? (total - segLen + stride - 1) / stride + 1 : 1;

    std::vector<juce::AudioBuffer<float>> accum;
    for (int o = 0; o < numOutputs; ++o)
    {
        accum.emplace_back (2, total);
        accum.back().clear();
    }
    std::vector<float> weightSum ((size_t) total, 0.0f);
    std::vector<float> weights ((size_t) segLen);

    juce::AudioBuffer<float> segment (2, segLen);
    std::vector<juce::AudioBuffer<float>> outputs;
    for (int o = 0; o < numOutputs; ++o)
        outputs.emplace_back (2, segLen);

    for (int k = 0; k < numSegments; ++k)
    {
        const int start = k * stride;
        const int n = juce::jmin (segLen, total - start);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = source.getReadPointer (ch, start);
            float* dst = segment.getWritePointer (ch);
            if (n == segLen)
                std::copy (src, src + segLen, dst);
            else if (n > segLen / 2)
                reflectPad (src, n, 0, segLen - n, dst);
            else
            {
                std::copy (src, src + n, dst);
                std::fill (dst + n, dst + segLen, 0.0f);
            }
        }

        for (auto& out : outputs)
            out.clear();

        if (! processSegment (segment, outputs, k, numSegments))
            return false;

        std::fill (weights.begin(), weights.end(), 1.0f);
        if (fade > 1)
        {
            // numpy.linspace (0, 1, fade) in, linspace (1, 0, fade) out
            for (int i = 0; i < fade; ++i)
            {
                const float ramp = (float) i / (float) (fade - 1);
                if (k > 0)
                    weights[(size_t) i] = ramp;
                if (k < numSegments - 1)
                    weights[(size_t) (segLen - fade + i)] = 1.0f - ramp;
            }
        }

        for (int o = 0; o < numOutputs; ++o)
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = outputs[(size_t) o].getReadPointer (ch);
                float* dst = accum[(size_t) o].getWritePointer (ch, start);
                for (int i = 0; i < n; ++i)
                    dst[i] += src[i] * weights[(size_t) i];
            }

        for (int i = 0; i < n; ++i)
            weightSum[(size_t) (start + i)] += weights[(size_t) i];
    }

    const int offset = wide ? border : 0;
    results.clear();
    for (int o = 0; o < numOutputs; ++o)
    {
        juce::AudioBuffer<float> result (2, length);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = accum[(size_t) o].getReadPointer (ch, offset);
            float* dst = result.getWritePointer (ch);
            for (int i = 0; i < length; ++i)
            {
                const float w = weightSum[(size_t) (offset + i)];
                dst[i] = w > 0.0f ? src[i] / w : src[i];
            }
        }
        results.push_back (std::move (result));
    }

    return true;
}
