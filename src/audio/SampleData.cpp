#include "SampleData.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace
{

static const juce::AudioBuffer<float> kEmptyBuffer;
static const std::array<SampleData::PeakMipmap, SampleData::kNumMipmapLevels> kEmptyMipmaps;

void buildMipmapsForBuffer (const juce::AudioBuffer<float>& src,
                            std::array<SampleData::PeakMipmap, SampleData::kNumMipmapLevels>& outMipmaps)
{
    int numFrames = src.getNumSamples();
    if (numFrames <= 0 || src.getNumChannels() < 1)
    {
        for (auto& m : outMipmaps)
        {
            m.samplesPerPeak = 0;
            m.maxPeaks.clear();
            m.minPeaks.clear();
        }
        return;
    }

    const float* dataL = src.getReadPointer (0);
    const float* dataR = src.getNumChannels() > 1 ? src.getReadPointer (1) : dataL;
    if (dataL == nullptr)
        return;

    static constexpr int kBlockSizes[SampleData::kNumMipmapLevels] = { 64, 512, 4096 };

    for (int level = 0; level < SampleData::kNumMipmapLevels; ++level)
    {
        auto& m = outMipmaps[(size_t) level];
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
} // namespace

SampleData::SampleData() = default;

std::unique_ptr<SampleData::DecodedSample> SampleData::decodeFromFile (const juce::File& file,
                                                                        double projectSampleRate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
        return nullptr;

    auto numFrames = (int) reader->lengthInSamples;
    auto numChannels = (int) reader->numChannels;
    auto sourceSampleRate = reader->sampleRate;

    juce::AudioBuffer<float> sourceBuffer (numChannels, numFrames);
    reader->read (&sourceBuffer, 0, numFrames, 0, true, true);

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

    juce::AudioBuffer<float> newBuffer (2, numFrames);

    if (numChannels >= 2)
    {
        newBuffer.copyFrom (0, 0, sourceBuffer, 0, 0, numFrames);
        newBuffer.copyFrom (1, 0, sourceBuffer, 1, 0, numFrames);
    }
    else
    {
        newBuffer.copyFrom (0, 0, sourceBuffer, 0, 0, numFrames);
        newBuffer.copyFrom (1, 0, sourceBuffer, 0, 0, numFrames);
    }

    auto decoded = std::make_unique<DecodedSample>();
    decoded->buffer = std::move (newBuffer);
    decoded->fileName = file.getFileName();
    decoded->filePath = file.getFullPathName();
    buildMipmapsForBuffer (decoded->buffer, decoded->peakMipmaps);
    return decoded;
}

void SampleData::applyDecodedSample (std::unique_ptr<DecodedSample> decoded)
{
    if (decoded == nullptr)
        return;

    loadedFileName = decoded->fileName;
    loadedFilePath = decoded->filePath;
    int frames = decoded->buffer.getNumSamples();

    // Convert to shared_ptr — no buffer copy, no heap allocation.
    auto shared = std::shared_ptr<const DecodedSample> (decoded.release());

    // Audio-thread reference (non-atomic, only written here on audio thread).
    activeDecoded = shared;
    numFrames.store (frames, std::memory_order_release);

    // Publish the same object to UI via atomic snapshot.
#if INTERSECT_HAS_STD_ATOMIC_SHARED_PTR
    snapshot.store (shared, std::memory_order_release);
#else
    std::atomic_store_explicit (&snapshot, shared, std::memory_order_release);
#endif
    loaded.store (true, std::memory_order_release);
}

bool SampleData::loadFromFile (const juce::File& file, double projectSampleRate)
{
    auto decoded = decodeFromFile (file, projectSampleRate);
    if (decoded == nullptr)
        return false;
    applyDecodedSample (std::move (decoded));
    return true;
}

void SampleData::clear()
{
    activeDecoded.reset();
    numFrames.store (0, std::memory_order_release);
#if INTERSECT_HAS_STD_ATOMIC_SHARED_PTR
    snapshot.store (std::shared_ptr<const DecodedSample> {}, std::memory_order_release);
#else
    std::atomic_store_explicit (&snapshot, std::shared_ptr<const DecodedSample> {},
                                std::memory_order_release);
#endif
    loadedFileName.clear();
    loadedFilePath.clear();
    loaded.store (false, std::memory_order_release);
}

SampleData::SnapshotPtr SampleData::getSnapshot() const
{
#if INTERSECT_HAS_STD_ATOMIC_SHARED_PTR
    return snapshot.load (std::memory_order_acquire);
#else
    return std::atomic_load_explicit (&snapshot, std::memory_order_acquire);
#endif
}

const juce::AudioBuffer<float>& SampleData::getBuffer() const
{
    if (activeDecoded)
        return activeDecoded->buffer;
    return kEmptyBuffer;
}

const std::array<SampleData::PeakMipmap, SampleData::kNumMipmapLevels>& SampleData::getMipmaps() const
{
    if (activeDecoded)
        return activeDecoded->peakMipmaps;
    return kEmptyMipmaps;
}

float SampleData::getInterpolatedSample (double pos, int channel) const
{
    if (! activeDecoded || channel < 0 || channel > 1)
        return 0.0f;

    const auto& buf = activeDecoded->buffer;
    int ipos = (int) pos;
    float frac = (float) (pos - ipos);

    if (ipos < 0 || ipos >= buf.getNumSamples() - 1)
        return 0.0f;

    auto* data = buf.getReadPointer (channel);
    if (data == nullptr)
        return 0.0f;
    return data[ipos] + (data[ipos + 1] - data[ipos]) * frac;
}
