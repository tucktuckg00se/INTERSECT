#pragma once
#include "StemSeparation.h"
#include <atomic>

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

private:
    void run() override;

    std::atomic<StemModelDownloadState> state { StemModelDownloadState::idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<StemModelId> currentModelId { StemModelId::bsRoformer2stem };
    std::atomic<bool> shouldCancel { false };

    juce::CriticalSection lock;
    juce::String statusText;
    juce::String resultMessage;
    juce::File downloadFolder;
    std::vector<StemModelId> requestedModels;
};
