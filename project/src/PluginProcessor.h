#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/SampleData.h"
#include "audio/SliceManager.h"
#include "audio/VoicePool.h"
#include "audio/LazyChopEngine.h"
#include "params/ParamIds.h"
#include "params/ParamLayout.h"

class IntersectProcessor : public juce::AudioProcessor
{
public:
    IntersectProcessor();
    ~IntersectProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "INTERSECT"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Command FIFO for thread-safe communication from UI
    enum CommandType
    {
        CmdNone = 0,
        CmdLoadFile,
        CmdCreateSlice,
        CmdDeleteSlice,
        CmdLazyChopStart,
        CmdLazyChopStop,
        CmdStretch,
        CmdToggleLock,
        CmdSetSliceParam,
        CmdDuplicateSlice,
    };

    // Param field identifiers for CmdSetSliceParam
    enum SliceParamField
    {
        FieldBpm = 0,
        FieldPitch,
        FieldAlgorithm,
        FieldAttack,
        FieldDecay,
        FieldSustain,
        FieldRelease,
        FieldMuteGroup,
        FieldPingPong,
        FieldMidiNote,
        FieldStretchEnabled,
        FieldTonality,
        FieldFormant,
        FieldFormantComp,
        FieldGrainMode,
        FieldVolume,
    };

    struct Command
    {
        CommandType type = CmdNone;
        int intParam1 = 0;
        int intParam2 = 0;
        float floatParam1 = 0.0f;
        juce::File fileParam;
    };

    void pushCommand (Command cmd);

    // Public state for UI access
    SampleData     sampleData;
    SliceManager   sliceManager;
    VoicePool      voicePool;
    LazyChopEngine lazyChop;

    juce::AudioProcessorValueTreeState apvts;

    // UI state (atomic for thread safety)
    std::atomic<float> zoom   { 1.0f };
    std::atomic<float> scroll { 0.0f };

    // DAW BPM (read from playhead)
    std::atomic<float> dawBpm { 120.0f };

    // MIDI-selects-slice toggle
    std::atomic<bool> midiSelectsSlice { false };

private:
    void drainCommands();
    void handleCommand (const Command& cmd);
    void processMidi (juce::MidiBuffer& midi);

    // Command FIFO
    static constexpr int kFifoSize = 64;
    std::array<Command, kFifoSize> commandBuffer;
    juce::AbstractFifo commandFifo { kFifoSize };

    double currentSampleRate = 44100.0;

    // Cached parameter pointers
    std::atomic<float>* masterVolParam  = nullptr;
    std::atomic<float>* bpmParam        = nullptr;
    std::atomic<float>* pitchParam      = nullptr;
    std::atomic<float>* algoParam       = nullptr;
    std::atomic<float>* attackParam     = nullptr;
    std::atomic<float>* decayParam      = nullptr;
    std::atomic<float>* sustainParam    = nullptr;
    std::atomic<float>* releaseParam    = nullptr;
    std::atomic<float>* muteGroupParam  = nullptr;
    std::atomic<float>* pingPongParam   = nullptr;
    std::atomic<float>* stretchParam    = nullptr;
    std::atomic<float>* tonalityParam   = nullptr;
    std::atomic<float>* formantParam    = nullptr;
    std::atomic<float>* formantCompParam = nullptr;
    std::atomic<float>* grainModeParam   = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntersectProcessor)
};
