#include "BpmDetector.h"
#include "minibpm/src/MiniBpm.h"
#include <algorithm>

BpmDetector::BpmDetector()
    : juce::Thread ("BpmDetector")
{
    formatManager.registerBasicFormats();
    startThread (juce::Thread::Priority::background);
}

BpmDetector::~BpmDetector()
{
    signalThreadShouldExit();
    wakeEvent.signal();
    stopThread (2000);
}

void BpmDetector::enqueue (int sampleId, const juce::String& filePath, bool manual)
{
    if (sampleId < 0 || filePath.isEmpty())
        return;

    {
        const juce::ScopedLock sl (queueLock);

        auto existing = std::find_if (queue.begin(), queue.end(),
                                      [sampleId] (const Request& q) { return q.sampleId == sampleId; });
        if (existing != queue.end())
        {
            // Already queued. An auto duplicate is a no-op; a manual request
            // upgrades the entry and moves it to the head so the popup shows.
            if (manual)
            {
                queue.erase (existing);
                queue.push_front (Request { sampleId, filePath, true });
                wakeEvent.signal();
            }
            return;
        }

        Request req { sampleId, filePath, manual };
        if (manual)
            queue.push_front (req);
        else
            queue.push_back (req);
    }

    wakeEvent.signal();
}

bool BpmDetector::isPendingOrRunning (int sampleId) const
{
    if (activeSampleId.load (std::memory_order_acquire) == sampleId)
        return true;

    const juce::ScopedLock sl (queueLock);
    for (const auto& r : queue)
        if (r.sampleId == sampleId)
            return true;
    return false;
}

bool BpmDetector::isBusy() const
{
    if (activeSampleId.load (std::memory_order_acquire) >= 0)
        return true;
    const juce::ScopedLock sl (queueLock);
    return ! queue.empty();
}

std::vector<BpmDetector::Result> BpmDetector::takeResults()
{
    const juce::ScopedLock sl (resultsLock);
    std::vector<Result> out;
    out.swap (results);
    return out;
}

void BpmDetector::run()
{
    while (! threadShouldExit())
    {
        Request req;
        bool haveReq = false;
        {
            const juce::ScopedLock sl (queueLock);
            if (! queue.empty())
            {
                req = queue.front();
                queue.pop_front();
                haveReq = true;
            }
        }

        if (! haveReq)
        {
            wakeEvent.wait (-1);
            continue;
        }

        activeSampleId.store (req.sampleId, std::memory_order_release);
        Result result = process (req);
        activeSampleId.store (-1, std::memory_order_release);

        if (threadShouldExit())
            return;

        {
            const juce::ScopedLock sl (resultsLock);
            results.push_back (std::move (result));
        }

        if (onComplete)
            onComplete();
    }
}

BpmDetector::Result BpmDetector::process (const Request& req)
{
    Result result;
    result.sampleId = req.sampleId;
    result.manual = req.manual;

    std::unique_ptr<juce::AudioFormatReader> reader (
        formatManager.createReaderFor (juce::File (req.filePath)));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return result;  // unreadable → bestBpm stays 0

    const double lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;
    if (lengthSeconds < kMinLengthSeconds)
        return result;  // too short → bestBpm stays 0

    breakfastquay::MiniBPM estimator ((float) reader->sampleRate);
    estimator.setBPMRange (kMinBpm, kMaxBpm);

    const int numChannels = (int) reader->numChannels;
    constexpr int kChunk = 65536;
    juce::AudioBuffer<float> chunkBuffer (juce::jmax (1, numChannels), kChunk);
    std::vector<float> mono ((size_t) kChunk);

    juce::int64 pos = 0;
    const juce::int64 total = reader->lengthInSamples;
    while (pos < total)
    {
        if (threadShouldExit())
            return result;

        const int n = (int) juce::jmin ((juce::int64) kChunk, total - pos);
        chunkBuffer.clear();
        reader->read (&chunkBuffer, 0, n, pos, true, numChannels > 1);

        const float* left = chunkBuffer.getReadPointer (0);
        if (numChannels > 1)
        {
            const float* right = chunkBuffer.getReadPointer (1);
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] = (left[i] + right[i]) * 0.5f;
        }
        else
        {
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] = left[i];
        }

        estimator.process (mono.data(), n);
        pos += n;
    }

    result.bestBpm = estimator.estimateTempo();
    if (result.bestBpm > 0.0)
        result.candidates = estimator.getTempoCandidates();
    return result;
}
