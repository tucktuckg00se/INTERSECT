#pragma once
#include "StemSeparation.h"
#include <atomic>

class OrtBundleDownloadJob : private juce::Thread
{
public:
    OrtBundleDownloadJob();
    ~OrtBundleDownloadJob() override;

    bool start (juce::File ortRoot, OrtBundleId bundleId, juce::String ortVersion);
    void cancel();

    StemModelDownloadState getState() const { return state.load (std::memory_order_acquire); }
    float getProgress() const { return progress.load (std::memory_order_acquire); }
    OrtBundleId getCurrentBundleId() const { return currentBundleId.load (std::memory_order_acquire); }
    juce::String getStatusText() const;
    juce::String consumeResultMessage();

private:
    void run() override;

    std::atomic<StemModelDownloadState> state { StemModelDownloadState::idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<OrtBundleId> currentBundleId { OrtBundleId::linuxX64Cpu };
    std::atomic<bool> shouldCancel { false };

    juce::CriticalSection lock;
    juce::String statusText;
    juce::String resultMessage;
    juce::File rootFolder;
    juce::String ortVersion;
    OrtBundleId requestedBundle { OrtBundleId::linuxX64Cpu };
};
