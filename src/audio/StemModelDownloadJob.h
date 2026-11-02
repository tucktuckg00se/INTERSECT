#pragma once
#include "StemSeparation.h"
#include <atomic>
#include <functional>

// Manifest URL of the newest release in this build's stem-model series (GitHub releases API),
// or the built-in baseline release's manifest when that fails. Blocking: worker threads only.
juce::String resolveStemModelManifestUrl (juce::Thread& thread);

// Downloads the series' newest manifest into modelFolder's cache file. The cache is only replaced
// by a manifest with a `models` array. Blocking: worker threads only.
bool downloadStemModelManifest (const juce::File& modelFolder, juce::Thread& thread,
                                std::atomic<bool>& shouldCancel, juce::String& errorMessage);

// Background refresh of the cached stem-model manifest, so model fixes published in the series
// reach users who already have models installed (an installed model never starts a download).
class StemManifestRefreshJob : private juce::Thread
{
public:
    StemManifestRefreshJob();
    ~StemManifestRefreshJob() override;

    // False when a refresh is already running.
    bool start (juce::File modelFolder);
    bool isRunning() const { return isThreadRunning(); }

    // Called on the job's thread when a refresh installed a manifest that differs from the cached one.
    // Set before start().
    std::function<void()> onManifestChanged;

private:
    void run() override;

    std::atomic<bool> shouldCancel { false };
    juce::File folder;
};

class StemModelDownloadJob : private juce::Thread
{
public:
    StemModelDownloadJob();
    ~StemModelDownloadJob() override;

    bool start (juce::File modelFolder, std::vector<StemModelId> modelIds);
    void cancel();

    StemModelDownloadState getState() const { return state.load (std::memory_order_acquire); }
    float getProgress() const { return progress.load (std::memory_order_acquire); }
    juce::String getStatusText() const;
    // Model currently being downloaded (empty when idle).
    StemModelId getCurrentModelId() const;
    juce::String consumeResultMessage();

    // Invoked from the job's thread when run() returns. Set by the owner
    // before start(); read only inside run(). Used to notify the owner that a
    // terminal state (completed/failed/cancelled) has been reached so the
    // audio thread does not have to poll.
    std::function<void()> onTerminalState;

private:
    void run() override;

    std::atomic<StemModelDownloadState> state { StemModelDownloadState::idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> shouldCancel { false };

    juce::CriticalSection lock;
    juce::String statusText;
    juce::String resultMessage;
    StemModelId currentModelId;
    juce::File downloadFolder;
    std::vector<StemModelId> requestedModels;
};
