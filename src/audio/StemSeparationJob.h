#pragma once
#include "StemSeparation.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>

juce::String getStemGpuAvailabilityError();

class StemSeparationJob : private juce::Thread
{
public:
    StemSeparationJob();
    ~StemSeparationJob() override;

    // Launch a separation job. Returns false if already running.
    bool start (const juce::AudioBuffer<float>& sourceAudio,
                double sampleRate,
                int sourceSampleId,
                const juce::String& sourceName,
                StemModelId modelId,
                const StemModelCatalogEntry& catalogEntry,
                StemSelectionMask stemSelectionMask,
                StemComputeDevice computeDevice,
                const juce::File& modelPath,
                const juce::File& outputDir);

    StemJobState getState() const { return state.load (std::memory_order_acquire); }
    float getProgress() const { return progress.load (std::memory_order_acquire); }
    int getSourceSampleId() const { return sourceSampleId.load (std::memory_order_acquire); }

    // Consume the result and reset to idle. Call only when state is completed or failed.
    StemJobResult consumeResult();

    void cancel();

private:
    void run() override;

    std::atomic<StemJobState> state { StemJobState::idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<int> sourceSampleId { -1 };
    std::atomic<bool> shouldCancel { false };

    // Guarded by resultLock
    juce::CriticalSection resultLock;
    StemJobResult result;

    // Copied at start(), read only by the job thread
    juce::AudioBuffer<float> audioBuffer;
    double jobSampleRate = 44100.0;
    juce::String jobSourceName;
    StemModelId jobModelId = StemModelId::bsRoformerSw6stem;
    StemModelCatalogEntry jobCatalogEntry;
    StemSelectionMask jobStemSelectionMask = 0;
    StemComputeDevice jobComputeDevice = StemComputeDevice::cpu;
    juce::File jobModelPath;
    juce::File jobOutputDir;
};
