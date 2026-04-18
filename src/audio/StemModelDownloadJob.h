#pragma once
#include "StemSeparation.h"
#include <atomic>
#include <functional>

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
    StemModelId getCurrentModelId() const { return currentModelId.load (std::memory_order_acquire); }
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
    std::atomic<StemModelId> currentModelId { StemModelId::bsRoformerSw6stem };
    std::atomic<bool> shouldCancel { false };

    juce::CriticalSection lock;
    juce::String statusText;
    juce::String resultMessage;
    juce::File downloadFolder;
    std::vector<StemModelId> requestedModels;
};
