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

    // Convert to stereo
    buffer.setSize (2, numFrames);

    if (numChannels >= 2)
    {
        buffer.copyFrom (0, 0, sourceBuffer, 0, 0, numFrames);
        buffer.copyFrom (1, 0, sourceBuffer, 1, 0, numFrames);
    }
    else
    {
        // Mono -> stereo duplication
        buffer.copyFrom (0, 0, sourceBuffer, 0, 0, numFrames);
        buffer.copyFrom (1, 0, sourceBuffer, 0, 0, numFrames);
    }

    loadedFileName = file.getFileName();
    loaded = true;
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
    return data[ipos] + (data[ipos + 1] - data[ipos]) * frac;
}
