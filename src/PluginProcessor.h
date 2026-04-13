#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <optional>
#include <vector>
#include "Constants.h"
#include "RtText.h"
#include "audio/SampleData.h"
#include "audio/SliceManager.h"
#include "audio/VoicePool.h"
#include "audio/LazyChopEngine.h"
#include "UndoManager.h"
#include "params/GlobalParamSnapshot.h"
#include "params/ParamUndoState.h"
#include "params/ParamIds.h"
#include "params/ParamLayout.h"

class IntersectProcessor : public juce::AudioProcessor
{
public:
    IntersectProcessor();
    ~IntersectProcessor() override;

    using juce::AudioProcessor::processBlock;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    const juce::String getName() const override { return "INTERSECT"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 5.0; }  // max ADSR release is 5000 ms

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    void setStandaloneTransportBpm (float newBpm) noexcept;
    float getStandaloneTransportBpm() const noexcept;

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
        CmdRepackMidi,
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
        FieldRepitchMode,
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
        FieldFilterEnabled,
        FieldFilterType,
        FieldFilterSlope,
        FieldFilterCutoff,
        FieldFilterReso,
        FieldFilterDrive,
        FieldFilterKeyTrack,
        FieldFilterEnvAttack,
        FieldFilterEnvDecay,
        FieldFilterEnvSustain,
        FieldFilterEnvRelease,
        FieldFilterEnvAmount,
        FieldFilterAsym,
        FieldCrossfade,
        FieldLoopStart,
        FieldLoopLength,
        FieldHighNote,
        FieldSliceRootNote,
    };

    enum class MidiEditAction
    {
        none = 0,
        zoom,
        sliceStart,
        sliceEnd,
    };

    struct MidiEditEvent
    {
        MidiEditAction action = MidiEditAction::none;
        int steps = 0;
    };

    struct MidiEditParserState   // audio-thread only, plain arrays
    {
        uint8_t nrpnMsb[16]{};
        uint8_t nrpnLsb[16]{};
    };

    struct MidiEditState
    {
        // Cross-thread config (UI writes, audio reads):
        std::atomic<bool> enabled            { false };
        std::atomic<int>  channel            { 0 };     // 0=omni, 1-16
        std::atomic<bool> consumeMidiEditCc  { true };

        // Audio-thread-only session state (plain):
        int  activeGestureSlice { -1 };
        int  gestureIdleSamples {  0 };
        bool previewActive      { false };
        bool gestureOpen        { false };
        bool activeBoundaryIsStart { true };
    };

    struct Command
    {
        CommandType type = CmdNone;
        int intParam1 = 0;
        int intParam2 = 0;
        float floatParam1 = 0.0f;
        uint64_t lockBitParam = 0;
        juce::File fileParam;
        // Fixed-size array avoids heap allocation/deallocation on the audio thread.
        std::array<int, 128> positions {};
        int numPositions = 0;
        // Explicit target slice index, captured at push time.
        // -1 means "no explicit target" (legacy commands that don't need one).
        int sliceIdx = -1;
    };

    void pushCommand (Command cmd);
    bool enqueueUiUndoSnapshot();

    std::atomic<bool> pendingEndGesture { false };

    void loadFileAsync (const juce::File& file);
    void relinkFileAsync (const juce::File& file);

    struct UiSliceSnapshot
    {
        int numSlices = 0;
        int selectedSlice = -1;
        int rootNote = kDefaultRootNote;
        bool sampleLoaded = false;
        bool sampleMissing = false;
        int sampleNumFrames = 0;
        double sampleSampleRate = 0.0;
        RtText<512> sampleFileName;
        bool hasStatusMessage = false;
        bool statusIsWarning = false;
        RtText<256> statusMessage;
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

    void markUiSnapshotDirty() { uiSnapshotDirty.store (true, std::memory_order_release); }

    struct MidiBoundaryPreviewState
    {
        int sliceIdx = -1;
        int startSample = 0;
        int endSample = 0;
        bool active = false;
        bool editingStart = false;
        bool editingEnd = false;
    };

    bool getMidiBoundaryPreviewState (MidiBoundaryPreviewState& out) const
    {
        if (! midiBoundaryPreviewActive.load (std::memory_order_acquire))
            return false;

        out.sliceIdx = midiBoundaryPreviewSliceIdx.load (std::memory_order_relaxed);
        out.startSample = midiBoundaryPreviewStart.load (std::memory_order_relaxed);
        out.endSample = midiBoundaryPreviewEnd.load (std::memory_order_relaxed);
        const int editedEdge = midiBoundaryPreviewEditedEdge.load (std::memory_order_relaxed);
        out.active = true;
        out.editingStart = (editedEdge == 1);
        out.editingEnd = (editedEdge == 2);
        return true;
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
    std::atomic<float> standaloneTransportBpm { 120.0f };

    // MIDI-selects-slice toggle
    std::atomic<bool> midiSelectsSlice { false };

    // Snap-to-zero-crossing toggle
    std::atomic<bool> snapToZeroCrossing { false };

    // Middle C octave convention (synced from editor for warning messages)
    std::atomic<int> middleCOctave { 4 };

    // Undo/redo
    UndoManager undoMgr;

    // Deferred parameter restore: message thread picks up pending POD state.
    // Returns true if a restore was applied.
    bool applyDeferredParamRestore();

    // Shift preview request: -2=no-op, -1=stop, >=0=start at this sample position
    std::atomic<int> shiftPreviewRequest { -2 };

    // Live slice bounds during edge/move drag — updated every mouseDrag, no undo.
    // Audio thread applies these each block so note-ons during drag use current bounds.
    // Set liveDragSliceIdx to -1 to deactivate.
    std::atomic<int> liveDragSliceIdx   { -1 };
    std::atomic<int> liveDragBoundsStart {  0 };
    std::atomic<int> liveDragBoundsEnd   {  0 };
    std::atomic<int> liveDragOwner       {  0 };   // 0=none, 1=mouse, 2=midi

    std::atomic<int> midiBoundaryPreviewSliceIdx { -1 };
    std::atomic<int> midiBoundaryPreviewStart { 0 };
    std::atomic<int> midiBoundaryPreviewEnd { 0 };
    std::atomic<int> midiBoundaryPreviewEditedEdge { 0 };   // 0=none, 1=start, 2=end
    std::atomic<bool> midiBoundaryPreviewActive { false };

    MidiEditState midiEditState;

    // Missing sample state (for relink UI)
    std::atomic<bool> sampleMissing { false };
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
        RtText<512> fileName;
        RtText<4096> filePath;
    };

    struct MissingFileInfo
    {
        RtText<512> fileName;
        RtText<4096> filePath;
    };

    struct UiStatusMessage
    {
        enum class Source
        {
            none = 0,
            generic,
            droppedCommands,
            midiLimit,
        };

        RtText<256> text;
        bool isWarning = false;
        Source source = Source::none;
    };

    struct PendingSliceTimelineRemap
    {
        std::atomic<bool> active { false };
        std::atomic<int> expectedLoadToken { 0 };
        std::atomic<int> savedStateVersion { 0 };
        std::atomic<int> savedDecodedNumFrames { 0 };
        std::atomic<double> savedDecodedSampleRate { 0.0 };
        std::atomic<int> savedSourceNumFrames { 0 };
        std::atomic<double> savedSourceSampleRate { 0.0 };
    };

    void drainCommands();
    void handleCommand (const Command& cmd);
    void processMidi (juce::MidiBuffer& midi);
    std::optional<MidiEditEvent> tryParseMidiEditMessage (const juce::MidiMessage& msg);
    void handleMidiEditEvent (const MidiEditEvent& event);
    void applyMidiEditZoomSteps (int steps);
    bool beginMidiSliceBoundaryGestureIfNeeded (int sliceIdx, bool isStart);
    void applyMidiSliceBoundarySteps (int sliceIdx, bool isStart, int steps);
    void applyLiveDragBoundsToSlice();
    void setMidiBoundaryPreviewState (int sliceIdx, int startSample, int endSample, bool isStart);
    void commitMidiSliceBoundaryGestureIfIdle (int blockSamples);
    void clearMidiEditGestureState();
    int requestSampleLoad (const juce::File& file, LoadKind kind);
    void clearVoicesBeforeSampleSwap();
    void clampSlicesToSampleBounds();
    void publishUiSliceSnapshot();
    void setMissingFileInfo (const RtText<512>& fileName, const RtText<4096>& filePath);
    void clearMissingFileInfo();
    const MissingFileInfo& getMissingFileInfo() const;
    void setUiStatusMessage (const juce::String& text, bool isWarning,
                             UiStatusMessage::Source source = UiStatusMessage::Source::generic);
    void setUiStatusMessage (const RtText<256>& text,
                             bool isWarning,
                             UiStatusMessage::Source source = UiStatusMessage::Source::generic);
    void clearUiStatusMessage();
    bool clearDroppedCommandWarning();
    void setDroppedCommandWarning (uint32_t droppedCount, uint32_t droppedCriticalCount);
    const UiStatusMessage& getUiStatusMessage() const;
    void setPendingStateFile (const juce::File& file);
    void clearPendingStateFile();
    juce::File getPendingStateFile() const;
    ParamUndoState captureParamUndoState() const;
    void applyParamUndoState (const ParamUndoState& state);
    void clearPendingSliceTimelineRemap();
    void primePendingSliceTimelineRemap (int savedStateVersion,
                                         int savedDecodedNumFrames,
                                         double savedDecodedSampleRate,
                                         int savedSourceNumFrames,
                                         double savedSourceSampleRate);
    bool applyPendingSliceTimelineRemap();
    UndoManager::Snapshot makeSnapshot();
    void captureSnapshot();
    void restoreSnapshot (const UndoManager::Snapshot& snap);
    GlobalParamSnapshot loadGlobalParamSnapshot() const;
    bool enqueueOverflowCommand (Command cmd);
    void drainOverflowCommands (bool& handledAny);
    bool enqueueCoalescedCommand (const Command& cmd);
    void drainCoalescedCommands (bool& handledAny);

    // Command FIFO
    static constexpr int kFifoSize = 256;
    std::array<Command, kFifoSize> commandBuffer;
    juce::AbstractFifo commandFifo { kFifoSize };
    std::atomic<uint32_t> droppedCommandCount { 0 };
    std::atomic<uint32_t> droppedCriticalCommandCount { 0 };
    std::atomic<uint32_t> droppedCommandTotal { 0 };
    std::atomic<uint32_t> droppedCriticalCommandTotal { 0 };
    static constexpr int kOverflowFifoSize = 32;
    std::array<Command, kOverflowFifoSize> overflowCommandBuffer {};
    std::atomic<int> overflowReadIndex { 0 };
    std::atomic<int> overflowWriteIndex { 0 };
    std::atomic<bool> pendingSetSliceParam { false };
    std::atomic<uint64_t> pendingSetSliceParamPayload { 0 };
    std::atomic<int> pendingSetSliceParamIdx { -1 };
    std::atomic<bool> pendingSetSliceBounds { false };
    std::atomic<uint32_t> pendingSetSliceBoundsSequence { 0 };
    std::atomic<int> pendingSetSliceBoundsIdx { -1 };
    std::atomic<int> pendingSetSliceBoundsStart { 0 };
    std::atomic<int> pendingSetSliceBoundsEnd { 0 };

    MidiEditParserState midiEditParser;

    double currentSampleRate = 44100.0;
    bool gestureSnapshotCaptured = false;
    int blocksSinceGestureActivity = 0;
    // UI-thread undo snapshot queue for direct APVTS writes
    static constexpr int kUiUndoFifoSize = 4;
    std::array<UndoManager::Snapshot, kUiUndoFifoSize> uiUndoBuffer {};
    juce::AbstractFifo uiUndoFifo { kUiUndoFifoSize };

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
    PendingSliceTimelineRemap pendingSliceTimelineRemap;

    std::array<bool, kMidiNoteCount> heldNotes {};

    std::array<ParamUndoState, 2> pendingParamRestoreStates {};
    std::atomic<int> pendingParamRestoreIndex { -1 };
    std::array<MissingFileInfo, 2> missingFileInfos {};
    std::atomic<int> missingFileInfoIndex { 0 };
    std::array<UiStatusMessage, 2> uiStatusMessages {};
    std::atomic<int> uiStatusMessageIndex { 0 };
    std::atomic<int> pendingStateRestoreToken { 0 };
    juce::File pendingStateFile;
    mutable juce::CriticalSection pendingStateFileLock;

    // Cached parameter pointers
    std::atomic<float>* masterVolParam  = nullptr;
    std::atomic<float>* bpmParam        = nullptr;
    std::atomic<float>* pitchParam      = nullptr;
    std::atomic<float>* algoParam       = nullptr;
    std::atomic<float>* repitchModeParam = nullptr;
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
    std::atomic<float>* filterEnabledParam    = nullptr;
    std::atomic<float>* filterTypeParam       = nullptr;
    std::atomic<float>* filterSlopeParam      = nullptr;
    std::atomic<float>* filterCutoffParam     = nullptr;
    std::atomic<float>* filterResoParam       = nullptr;
    std::atomic<float>* filterDriveParam      = nullptr;
    std::atomic<float>* filterAsymParam       = nullptr;
    std::atomic<float>* filterKeyTrackParam   = nullptr;
    std::atomic<float>* filterEnvAttackParam  = nullptr;
    std::atomic<float>* filterEnvDecayParam   = nullptr;
    std::atomic<float>* filterEnvSustainParam = nullptr;
    std::atomic<float>* filterEnvReleaseParam = nullptr;
    std::atomic<float>* filterEnvAmountParam  = nullptr;
    std::atomic<float>* uiScaleParam          = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntersectProcessor)
};
