#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>
#include "audio/SampleData.h"
#include "audio/SliceManager.h"
#include "audio/VoicePool.h"
#include "audio/LazyChopEngine.h"
#include "UndoManager.h"
#include "params/ParamIds.h"
#include "params/ParamLayout.h"

class IntersectProcessor : public juce::AudioProcessor
{
public:
    IntersectProcessor();
    ~IntersectProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
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
        CmdSetSliceBounds,
        CmdDuplicateSlice,
        CmdSplitSlice,
        CmdTransientChop,
        CmdRelinkFile,
        CmdFileLoadCompleted,
        CmdFileLoadFailed,
        CmdUndo,
        CmdRedo,
        CmdBeginGesture,
        CmdPanic,
        CmdSelectSlice,
        CmdSetRootNote,
    };

    enum LoadKind
    {
        LoadKindReplace = 0,
        LoadKindRelink = 1,
    };

    enum SampleAvailabilityState
    {
        SampleStateEmpty = 0,
        SampleStateLoaded,
        SampleStateMissingAwaitingRelink,
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
        FieldMidiNote,
        FieldStretchEnabled,
        FieldTonality,
        FieldFormant,
        FieldFormantComp,
        FieldGrainMode,
        FieldVolume,
        FieldReleaseTail,
        FieldReverse,
        FieldOutputBus,
        FieldLoop,
        FieldOneShot,
        FieldCentsDetune,
    };

    struct Command
    {
        CommandType type = CmdNone;
        int intParam1 = 0;
        int intParam2 = 0;
        float floatParam1 = 0.0f;
        juce::File fileParam;
        // Fixed-size array avoids heap allocation/deallocation on the audio thread.
        std::array<int, 128> positions {};
        int numPositions = 0;
    };

    void pushCommand (Command cmd);
    void loadFileAsync (const juce::File& file);
    void relinkFileAsync (const juce::File& file);

    struct UiSliceSnapshot
    {
        int numSlices = 0;
        int selectedSlice = -1;
        int rootNote = 36;
        bool sampleLoaded = false;
        bool sampleMissing = false;
        int sampleNumFrames = 0;
        juce::String sampleFileName;
        std::array<Slice, SliceManager::kMaxSlices> slices {};
    };

    const UiSliceSnapshot& getUiSliceSnapshot() const
    {
        return uiSliceSnapshots[(size_t) uiSliceSnapshotIndex.load (std::memory_order_acquire)];
    }

    uint32_t getUiSliceSnapshotVersion() const
    {
        return uiSnapshotVersion.load (std::memory_order_acquire);
    }

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

    // Snap-to-zero-crossing toggle
    std::atomic<bool> snapToZeroCrossing { false };

    // Undo/redo
    UndoManager undoMgr;

    // Shift preview request: -2=no-op, -1=stop, >=0=start at this sample position
    std::atomic<int> shiftPreviewRequest { -2 };

    // Live slice bounds during edge/move drag — updated every mouseDrag, no undo.
    // Audio thread applies these each block so note-ons during drag use current bounds.
    // Set liveDragSliceIdx to -1 to deactivate.
    std::atomic<int> liveDragSliceIdx   { -1 };
    std::atomic<int> liveDragBoundsStart {  0 };
    std::atomic<int> liveDragBoundsEnd   {  0 };

    // Missing sample state (for relink UI)
    std::atomic<bool> sampleMissing { false };
    juce::String missingFilePath;
    std::atomic<int> sampleAvailability { (int) SampleStateEmpty };

    SampleAvailabilityState getSampleAvailabilityState() const
    {
        return (SampleAvailabilityState) sampleAvailability.load (std::memory_order_relaxed);
    }

private:
    struct FailedLoadResult
    {
        int token = 0;
        LoadKind kind = LoadKindReplace;
        juce::File file;
    };

    void drainCommands();
    void handleCommand (const Command& cmd);
    void processMidi (const juce::MidiBuffer& midi);
    void requestSampleLoad (const juce::File& file, LoadKind kind);
    void clearVoicesBeforeSampleSwap();
    void clampSlicesToSampleBounds();
    void publishUiSliceSnapshot();
    UndoManager::Snapshot makeSnapshot();
    void captureSnapshot();
    void restoreSnapshot (const UndoManager::Snapshot& snap);
    bool enqueueOverflowCommand (Command cmd);
    void drainOverflowCommands (bool& handledAny);
    bool enqueueCoalescedCommand (const Command& cmd);
    void drainCoalescedCommands (bool& handledAny);

    // Command FIFO
    static constexpr int kFifoSize = 256;
    std::array<Command, kFifoSize> commandBuffer;
    juce::AbstractFifo commandFifo { kFifoSize };
    std::atomic<uint32_t> droppedCommandCount { 0 };
    std::atomic<uint32_t> droppedCommandTotal { 0 };
    std::atomic<uint32_t> droppedCriticalCommandTotal { 0 };
    static constexpr int kOverflowFifoSize = 32;
    std::array<Command, kOverflowFifoSize> overflowCommandBuffer {};
    std::atomic<int> overflowReadIndex { 0 };
    std::atomic<int> overflowWriteIndex { 0 };
    std::atomic<bool> pendingSetSliceParam { false };
    std::atomic<int> pendingSetSliceParamField { 0 };
    std::atomic<float> pendingSetSliceParamValue { 0.0f };
    std::atomic<bool> pendingSetSliceBounds { false };
    std::atomic<int> pendingSetSliceBoundsIdx { -1 };
    std::atomic<int> pendingSetSliceBoundsStart { 0 };
    std::atomic<int> pendingSetSliceBoundsEnd { 0 };

    double currentSampleRate = 44100.0;
    bool gestureSnapshotCaptured = false;
    int blocksSinceGestureActivity = 0;

    juce::ThreadPool fileLoadPool { 1 };
    std::atomic<int> nextLoadToken { 0 };
    std::atomic<int> latestLoadToken { 0 };
    std::atomic<int> latestLoadKind { (int) LoadKindReplace };
    std::atomic<SampleData::DecodedSample*> completedLoadData { nullptr };
    std::atomic<FailedLoadResult*> completedLoadFailure { nullptr };
    std::array<UiSliceSnapshot, 2> uiSliceSnapshots {};
    std::atomic<int> uiSliceSnapshotIndex { 0 };
    std::atomic<bool> uiSnapshotDirty { true };
    std::atomic<uint32_t> uiSnapshotVersion { 0 };

    bool heldNotes[128] = {};

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
    std::atomic<float>* stretchParam    = nullptr;
    std::atomic<float>* tonalityParam   = nullptr;
    std::atomic<float>* formantParam    = nullptr;
    std::atomic<float>* formantCompParam = nullptr;
    std::atomic<float>* grainModeParam   = nullptr;
    std::atomic<float>* releaseTailParam = nullptr;
    std::atomic<float>* reverseParam     = nullptr;
    std::atomic<float>* loopParam        = nullptr;
    std::atomic<float>* oneShotParam     = nullptr;
    std::atomic<float>* maxVoicesParam   = nullptr;
    std::atomic<float>* centsDetuneParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntersectProcessor)
};
