#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <deque>
#include <functional>
#include <vector>

/**
 * Background BPM detection for session samples.
 *
 * A single worker thread drains a FIFO queue of detection requests, reading the
 * audio file from disk (so a request can be queued the instant a sample is
 * imported, without waiting for the timeline decode to finish), mixing it to
 * mono and running the MiniBPM fixed-tempo estimator. Results are pushed to a
 * lock-guarded vector and the owner is notified via onComplete() so it can drain
 * them on the message thread.
 *
 * Requests carry a session sampleId; the owner re-validates on completion that
 * the sample still exists before applying the detected BPM. Manual requests
 * (user pressed the BPM button) take priority over auto-on-import requests and
 * surface a candidates popup; auto requests apply silently.
 */
class BpmDetector : private juce::Thread
{
public:
    struct Result
    {
        int sampleId = -1;
        bool manual = false;
        double bestBpm = 0.0;               // 0 = could not detect (too short / unreadable)
        std::vector<double> candidates;     // ranked best-first (best == bestBpm)
    };

    BpmDetector();
    ~BpmDetector() override;

    // Message thread. Enqueue a detection request. Manual requests jump to the
    // head of the queue; duplicate queued ids are coalesced (a manual request
    // promotes an already-queued entry).
    void enqueue (int sampleId, const juce::String& filePath, bool manual);

    // Message thread. True while the given sample is queued or actively being
    // processed — drives the sample-lane busy indicator.
    bool isPendingOrRunning (int sampleId) const;
    bool isBusy() const;

    // Message thread. Drain completed results (empties the internal buffer).
    std::vector<Result> takeResults();

    // Set by the owner before first enqueue; invoked from the worker thread when
    // a result becomes available. Typically wired to triggerAsyncUpdate().
    std::function<void()> onComplete;

    // Detection tempo range (MiniBPM default 55-190).
    static constexpr double kMinBpm = 55.0;
    static constexpr double kMaxBpm = 190.0;
    // Clips shorter than this cannot yield a reliable fixed-tempo estimate.
    static constexpr double kMinLengthSeconds = 2.0;

private:
    struct Request
    {
        int sampleId = -1;
        juce::String filePath;
        bool manual = false;
    };

    void run() override;
    Result process (const Request& req);

    juce::AudioFormatManager formatManager;

    mutable juce::CriticalSection queueLock;
    std::deque<Request> queue;
    std::atomic<int> activeSampleId { -1 };
    juce::WaitableEvent wakeEvent;

    juce::CriticalSection resultsLock;
    std::vector<Result> results;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BpmDetector)
};
