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
    return decodeFromFiles (std::vector<juce::File> { file }, projectSampleRate);
}

std::unique_ptr<SampleData::DecodedSample> SampleData::decodeFromFiles (const std::vector<juce::File>& files,
                                                                        double projectSampleRate,
                                                                        const std::vector<int>* sampleIds)
{
    if (files.empty())
        return nullptr;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    auto decoded = std::make_unique<DecodedSample>();
    int totalFrames = 0;
    double targetSampleRate = 0.0;
    int totalSourceFrames = 0;
    double firstSourceSampleRate = 0.0;

    struct DecodedRegion
    {
        juce::AudioBuffer<float> buffer;
        SessionSample meta;
    };

    std::vector<DecodedRegion> regions;
    regions.reserve (files.size());

    for (size_t i = 0; i < files.size(); ++i)
    {
        const auto& file = files[i];
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr)
            return nullptr;

        auto numFrames = (int) reader->lengthInSamples;
        auto numChannels = (int) reader->numChannels;
        const int sourceNumFrames = numFrames;
        const double sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        if (targetSampleRate <= 0.0)
            targetSampleRate = projectSampleRate > 0.0 ? projectSampleRate : sourceSampleRate;
        if (firstSourceSampleRate <= 0.0)
            firstSourceSampleRate = sourceSampleRate;

        juce::AudioBuffer<float> sourceBuffer (juce::jmax (1, numChannels), numFrames);
        reader->read (&sourceBuffer, 0, numFrames, 0, true, true);

        if (std::abs (sourceSampleRate - targetSampleRate) > 0.01)
        {
            const double ratio = sourceSampleRate / targetSampleRate;
            const int resampledLen = (int) std::ceil (numFrames / ratio);
            juce::AudioBuffer<float> resampledBuffer (juce::jmax (1, numChannels), resampledLen);

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

        juce::AudioBuffer<float> stereoBuffer (2, numFrames);
        if (numChannels >= 2)
        {
            stereoBuffer.copyFrom (0, 0, sourceBuffer, 0, 0, numFrames);
            stereoBuffer.copyFrom (1, 0, sourceBuffer, 1, 0, numFrames);
        }
        else
        {
            stereoBuffer.copyFrom (0, 0, sourceBuffer, 0, 0, numFrames);
            stereoBuffer.copyFrom (1, 0, sourceBuffer, 0, 0, numFrames);
        }

        DecodedRegion region;
        region.buffer = std::move (stereoBuffer);
        region.meta.sampleId = sampleIds != nullptr && i < sampleIds->size() ? (*sampleIds)[i] : (int) i;
        region.meta.fileName = file.getFileName();
        region.meta.filePath = file.getFullPathName();
        region.meta.startFrame = totalFrames;
        region.meta.numFrames = numFrames;
        region.meta.sourceNumFrames = sourceNumFrames;
        region.meta.sourceSampleRate = sourceSampleRate;
        totalFrames += numFrames;
        totalSourceFrames += sourceNumFrames;
        regions.push_back (std::move (region));
    }

    decoded->buffer.setSize (2, totalFrames);
    int writePos = 0;
    for (auto& region : regions)
    {
        const int regionFrames = region.buffer.getNumSamples();
        decoded->buffer.copyFrom (0, writePos, region.buffer, 0, 0, regionFrames);
        decoded->buffer.copyFrom (1, writePos, region.buffer, 1, 0, regionFrames);
        decoded->sessionSamples.push_back (region.meta);
        writePos += regionFrames;
    }

    if (! decoded->sessionSamples.empty())
    {
        decoded->fileName = decoded->sessionSamples.front().fileName;
        decoded->filePath = decoded->sessionSamples.front().filePath;
    }
    decoded->decodedNumFrames = totalFrames;
    decoded->decodedSampleRate = targetSampleRate;
    decoded->sourceNumFrames = totalSourceFrames;
    decoded->sourceSampleRate = firstSourceSampleRate;
    buildMipmapsForBuffer (decoded->buffer, decoded->peakMipmaps);
    return decoded;
}

std::unique_ptr<SampleData::DecodedSample> SampleData::rebuildWithSessionSamples (const DecodedSample& source,
                                                                                  const std::vector<SessionSample>& sessionSamples)
{
    auto rebuilt = std::make_unique<DecodedSample>();

    if (sessionSamples.empty())
        return rebuilt;

    int totalFrames = 0;
    int totalSourceFrames = 0;
    for (const auto& sample : sessionSamples)
    {
        totalFrames += juce::jmax (0, sample.numFrames);
        totalSourceFrames += juce::jmax (0, sample.sourceNumFrames);
    }

    rebuilt->buffer.setSize (2, totalFrames);
    rebuilt->decodedNumFrames = totalFrames;
    rebuilt->decodedSampleRate = source.decodedSampleRate;
    rebuilt->sourceNumFrames = totalSourceFrames;
    rebuilt->sourceSampleRate = source.sourceSampleRate;

    int writePos = 0;
    for (const auto& sample : sessionSamples)
    {
        const int sourceStart = juce::jlimit (0, source.buffer.getNumSamples(), sample.startFrame);
        const int availableFrames = juce::jmax (0, source.buffer.getNumSamples() - sourceStart);
        const int copyFrames = juce::jmin (juce::jmax (0, sample.numFrames), availableFrames);

        SampleData::SessionSample rebuiltSample = sample;
        rebuiltSample.startFrame = writePos;
        rebuiltSample.numFrames = copyFrames;
        rebuilt->sessionSamples.push_back (rebuiltSample);

        if (copyFrames > 0)
        {
            rebuilt->buffer.copyFrom (0, writePos, source.buffer, 0, sourceStart, copyFrames);
            rebuilt->buffer.copyFrom (1, writePos, source.buffer, 1, sourceStart, copyFrames);
            writePos += copyFrames;
        }
    }

    if (! rebuilt->sessionSamples.empty())
    {
        rebuilt->fileName = rebuilt->sessionSamples.front().fileName;
        rebuilt->filePath = rebuilt->sessionSamples.front().filePath;
    }

    buildMipmapsForBuffer (rebuilt->buffer, rebuilt->peakMipmaps);
    return rebuilt;
}

void SampleData::applyDecodedSample (std::unique_ptr<DecodedSample> decoded)
{
    if (decoded == nullptr)
        return;

    const int frames = decoded->decodedNumFrames > 0 ? decoded->decodedNumFrames
                                                     : decoded->buffer.getNumSamples();

    // Convert to shared_ptr — no buffer copy, no heap allocation.
    auto shared = std::shared_ptr<const DecodedSample> (decoded.release());

    // Audio-thread reference (non-atomic, only written here on audio thread).
    activeDecoded = shared;
    numFrames.store (frames, std::memory_order_release);
    decodedSampleRate.store (shared->decodedSampleRate, std::memory_order_release);
    sourceNumFrames.store (shared->sourceNumFrames, std::memory_order_release);
    sourceSampleRate.store (shared->sourceSampleRate, std::memory_order_release);

    // Publish the same object to UI via atomic snapshot.
#if INTERSECT_HAS_STD_ATOMIC_SHARED_PTR
    snapshot.store (shared, std::memory_order_release);
#else
    std::atomic_store_explicit (&snapshot, shared, std::memory_order_release);
#endif
    loaded.store (true, std::memory_order_release);
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
    loaded.store (false, std::memory_order_release);
    decodedSampleRate.store (0.0, std::memory_order_release);
    sourceNumFrames.store (0, std::memory_order_release);
    sourceSampleRate.store (0.0, std::memory_order_release);
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

int SampleData::getNumSessionSamples() const
{
    return activeDecoded != nullptr ? (int) activeDecoded->sessionSamples.size() : 0;
}

const SampleData::SessionSample* SampleData::findSessionSampleById (int sampleId) const
{
    if (! activeDecoded)
        return nullptr;

    for (const auto& sample : activeDecoded->sessionSamples)
        if (sample.sampleId == sampleId)
            return &sample;
    return nullptr;
}

const std::vector<SampleData::SessionSample>& SampleData::getSessionSamples() const
{
    static const std::vector<SessionSample> empty;
    if (activeDecoded)
        return activeDecoded->sessionSamples;
    return empty;
}

float SampleData::interpolateCubic (float y0, float y1, float y2, float y3, float frac)
{
    const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float a2 = -0.5f * y0 + 0.5f * y2;
    const float a3 = y1;

    return ((a0 * frac + a1) * frac + a2) * frac + a3;
}

float SampleData::getSampleAtFrame (int frame, int channel) const
{
    if (! activeDecoded || channel < 0 || channel > 1)
        return 0.0f;

    const auto& buf = activeDecoded->buffer;
    if (frame < 0 || frame >= buf.getNumSamples())
        return 0.0f;

    auto* data = buf.getReadPointer (channel);
    return data != nullptr ? data[frame] : 0.0f;
}

float SampleData::getInterpolatedSample (double pos, int channel) const
{
    if (! activeDecoded || channel < 0 || channel > 1)
        return 0.0f;

    const auto& buf = activeDecoded->buffer;
    int ipos = (int) pos;
    float frac = (float) (pos - ipos);

    if (ipos < 0 || ipos >= buf.getNumSamples())
        return 0.0f;
    if (ipos == buf.getNumSamples() - 1)
    {
        auto* data = buf.getReadPointer (channel);
        return data != nullptr ? data[ipos] : 0.0f;
    }

    auto* data = buf.getReadPointer (channel);
    if (data == nullptr)
        return 0.0f;
    return data[ipos] + (data[ipos + 1] - data[ipos]) * frac;
}
