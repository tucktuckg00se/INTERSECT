#include "SampleData.h"

SampleData::SampleData()
{
    formatManager.registerBasicFormats();
}

bool SampleData::loadFromFile (const juce::File& file, double projectSampleRate)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    auto numFrames = (int) reader->lengthInSamples;
    auto numChannels = (int) reader->numChannels;
    auto sourceSampleRate = reader->sampleRate;

    // Read source audio
    juce::AudioBuffer<float> sourceBuffer (numChannels, numFrames);
    reader->read (&sourceBuffer, 0, numFrames, 0, true, true);

    // Resample if needed
    if (std::abs (sourceSampleRate - projectSampleRate) > 0.01)
    {
        double ratio = sourceSampleRate / projectSampleRate;
        int resampledLen = (int) std::ceil (numFrames / ratio);
        juce::AudioBuffer<float> resampledBuffer (numChannels, resampledLen);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.process (ratio,
                                  sourceBuffer.getReadPointer (ch),
                                  resampledBuffer.getWritePointer (ch),
                                  resampledLen);
        }

        sourceBuffer = std::move (resampledBuffer);
        numFrames = resampledLen;
    }

    // Build new stereo buffer locally, then swap to minimise the window
    // where the UI thread might read a partially-constructed buffer
    juce::AudioBuffer<float> newBuffer (2, numFrames);

    if (numChannels >= 2)
    {
        newBuffer.copyFrom (0, 0, sourceBuffer, 0, 0, numFrames);
        newBuffer.copyFrom (1, 0, sourceBuffer, 1, 0, numFrames);
    }
    else
    {
        // Mono -> stereo duplication
        newBuffer.copyFrom (0, 0, sourceBuffer, 0, 0, numFrames);
        newBuffer.copyFrom (1, 0, sourceBuffer, 0, 0, numFrames);
    }

    // Atomic-ish swap: the old buffer is freed after swap, not during setSize
    std::swap (buffer, newBuffer);

    loadedFileName = file.getFileName();
    loadedFilePath = file.getFullPathName();
    loaded = true;
    buildMipmaps();
    return true;
}

float SampleData::getInterpolatedSample (double pos, int channel) const
{
    if (! loaded || channel < 0 || channel > 1)
        return 0.0f;

    int ipos = (int) pos;
    float frac = (float) (pos - ipos);

    if (ipos < 0 || ipos >= buffer.getNumSamples() - 1)
        return 0.0f;

    auto* data = buffer.getReadPointer (channel);
    if (data == nullptr)
        return 0.0f;
    return data[ipos] + (data[ipos + 1] - data[ipos]) * frac;
}

void SampleData::buildMipmaps()
{
    int numFrames = buffer.getNumSamples();
    if (numFrames <= 0)
    {
        for (auto& m : peakMipmaps)
        {
            m.samplesPerPeak = 0;
            m.maxPeaks.clear();
            m.minPeaks.clear();
        }
        return;
    }

    if (buffer.getNumChannels() < 1)
        return;

    const float* dataL = buffer.getReadPointer (0);
    const float* dataR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : dataL;

    if (dataL == nullptr)
        return;

    static constexpr int kBlockSizes[kNumMipmapLevels] = { 64, 512, 4096 };

    for (int level = 0; level < kNumMipmapLevels; ++level)
    {
        auto& m = peakMipmaps[(size_t) level];
        m.samplesPerPeak = kBlockSizes[level];
        int numPeaks = (numFrames + m.samplesPerPeak - 1) / m.samplesPerPeak;
        m.maxPeaks.resize ((size_t) numPeaks);
        m.minPeaks.resize ((size_t) numPeaks);

        for (int i = 0; i < numPeaks; ++i)
        {
            int start = i * m.samplesPerPeak;
            int end = std::min (start + m.samplesPerPeak, numFrames);
            float hi = -1.0f;
            float lo = 1.0f;
            for (int s = start; s < end; ++s)
            {
                float val = (dataL[s] + dataR[s]) * 0.5f;
                if (val > hi) hi = val;
                if (val < lo) lo = val;
            }
            m.maxPeaks[(size_t) i] = hi;
            m.minPeaks[(size_t) i] = lo;
        }
    }
}
