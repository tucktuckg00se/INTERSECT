#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <vector>
#include "Constants.h"
#include "RtText.h"
#include "audio/SampleData.h"
#include "audio/SpscQueue.h"

// RealtimeSanitizer builds (-DINTERSECT_SANITIZE=realtime, Clang 20+) mark processBlock as
// nonblocking, so RTSan reports any allocation, free or lock reached from it at run time.
#if defined (INTERSECT_RTSAN)
 #define INTERSECT_NONBLOCKING [[clang::nonblocking]]
#else
 #define INTERSECT_NONBLOCKING
#endif
#include "audio/SliceManager.h"
#include "audio/VoicePool.h"
#include "audio/LazyChopEngine.h"
#include "audio/StemModelDownloadJob.h"
#include "audio/OrtBundleDownloadJob.h"
#include "urlimport/UrlImportJob.h"
#include "UpdateCheck.h"
#include "audio/StemSeparationJob.h"
#include "audio/AuditionPlayer.h"
#include "UndoManager.h"
#include "params/ParamFields.h"
#include "params/SampleParams.h"
#include "params/SampleParamTable.h"
#include "params/ParamUndoState.h"
#include "params/ParamIds.h"
#include "params/ParamLayout.h"

struct SessionState;

class IntersectProcessor : public juce::AudioProcessor,
                           public ParamFieldIds,   // FieldBpm, ... (IntersectProcessor::FieldBpm)
                           private juce::AsyncUpdater,
                           private juce::Timer       // message-thread housekeeping for the audio thread
{
public:
    IntersectProcessor();
    ~IntersectProcessor() override;

    using juce::AudioProcessor::processBlock;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) INTERSECT_NONBLOCKING override;

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
        CmdCreateSlice,
        CmdDeleteSlice,
        CmdDeleteSessionSample,
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
        CmdFileLoadCompleted,
        CmdFileLoadFailed,
        CmdUndo,
        CmdRedo,
        CmdBeginGesture,
        CmdPanic,
        CmdSelectSlice,
        CmdSetRootNote,
        CmdStemSeparate,
        CmdSetSampleParam,   // intParam1 = sampleId, intParam2 = field, floatParam1 = value (SAMPLE-tab units)
    };

    enum LoadKind
    {
        LoadKindReplace = 0,
        LoadKindPreserveSlices = 2,
    };

    enum SampleAvailabilityState
    {
        SampleStateEmpty = 0,
        SampleStateLoaded,
        SampleStateMissingAwaitingRelink,
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
    void loadFilesAsync (const std::vector<juce::File>& files, bool append);
    // ADD: appends existing audio files to the session (or loads them into an empty one) as one
    // undo step. Message thread. Here rather than in the editor so a URL import that finishes
    // while the editor is closed still lands in the kit.
    void addFilesToKit (const std::vector<juce::File>& files);
    // Points session samples at new files (sampleId -> file) and reloads the kit with the same
    // ids, so params, stem metadata and slices are kept. Message thread; takes one undo step.
    void relinkSessionSamplesAsync (const std::vector<std::pair<int, juce::File>>& relinks);
    void reorderSessionSampleAsync (int sourceSampleId, int targetIndex);
    void deleteSessionSampleAsync (int sampleId);
    void startStemSeparation (int sampleId,
                              const StemModelId& modelId,
                              const StemSelection& stemSelection,
                              StemExportMode exportMode,
                              const juce::File& outputFolder = {});
    void cancelStemSeparation();
    void startStemModelDownload (const std::vector<StemModelId>& modelIds);
    void cancelStemModelDownload();
    juce::File getStemModelFolder() const;
    juce::File getResolvedStemModelFolder() const;
    void setStemModelFolder (const juce::File& modelFolder);
    StemComputeDevice getStemComputeDevice() const noexcept { return stemComputeDevice; }
    void setStemComputeDevice (StemComputeDevice device) noexcept { stemComputeDevice = device; }
    std::vector<StemModelId> getInstalledStemModels() const;
    // Models where only an earlier version is installed (the stem view shows UPDATE).
    std::vector<StemModelId> getStemModelUpdates() const;
    bool isStemModelInstalled (const StemModelId& modelId) const;
    // Message thread: the last failed stem job's diagnostics, while statusText still shows its error.
    juce::String getStemFailureDiagnostics (const juce::String& statusText) const;
    const StemModelDownloadJob& getStemModelDownloadJob() const { return stemModelDownloadJob; }
    // Re-fetches the stem-model manifest in the background unless the cache is fresh or a fetch is
    // already running. Message thread. getStemManifestGeneration() changes when a refresh installed
    // a different manifest, so the stem view knows to reload its catalog.
    void refreshStemModelManifestIfStale();
    int getStemManifestGeneration() const noexcept { return stemManifestGeneration.load (std::memory_order_acquire); }

    // ORT runtime bundle management
    juce::File getOrtRootFolder() const;
    std::vector<OrtBundleId> getInstalledOrtBundles() const;
    juce::String getActiveOrtBundleDirectoryName() const;
    bool setActiveOrtBundle (OrtBundleId bundleId);
    void startOrtBundleDownload (OrtBundleId bundleId);
    void cancelOrtBundleDownload();
    OrtBundleDownloadJob& getOrtBundleDownloadJob() { return ortBundleDownloadJob; }
    // Newer-release check shown in Settings > General; one checker shared by all instances.
    UpdateChecker& getUpdateChecker() { return *updateChecker; }

    // Import from URL (yt-dlp; see urlimport/UrlImportLogic.h). Message thread.
    struct UrlImportSettings
    {
        bool askEachTime = false;   // a save dialog after each import instead of the folder
        juce::File folder;          // empty: getDefaultUrlImportFolder()
        UrlImport::Channel channel = UrlImport::Channel::stable;
        UrlImport::OutputFormat format;   // WAV 32-bit float at the source's rate unless changed
    };
    const UrlImportSettings& getUrlImportSettings() const noexcept { return urlImportSettings; }
    void setUrlImportSettings (const UrlImportSettings& settings) { urlImportSettings = settings; }
    /** ~/Music/INTERSECT Downloads. */
    static juce::File getDefaultUrlImportFolder();
    juce::File getResolvedUrlImportFolder() const;
    void startUrlImport (const juce::String& url, bool allowLong = false);
    void cancelUrlImport() { urlImportJob.cancel(); }
    void startUrlToolsUpdate();
    void startUrlToolsRemoval();
    UrlImport::UrlImportJob& getUrlImportJob() { return urlImportJob; }
    /** How the last import ended; serial changes with every import that ends. */
    struct UrlImportOutcome
    {
        UrlImport::JobResult::Status status = UrlImport::JobResult::Status::completed;
        juce::String url;
        juce::String message;
        juce::String details;
        int serial = 0;
    };
    const UrlImportOutcome& getLastUrlImportOutcome() const noexcept { return lastUrlImportOutcome; }
    /** An import finished in "ask each time" mode: the editor asks where to save it (once). */
    std::optional<UrlImport::JobResult> takePendingUrlImportSave();
    void placeUrlImport (const juce::File& source, const juce::File& destination,
                         const juce::File& workFolder, const juce::String& title);
    void discardUrlImport (const juce::File& workFolder);
    void showTransientStatusMessage (const juce::String& text, bool isWarning)
    {
        setUiStatusMessage (text, isWarning);
    }

    // Built-in presets (.intersectpreset): a full kit (samples, slices, parameters) as a file.
    // Message thread only. File work runs on fileLoadPool; results land via handleAsyncUpdate.
    void savePresetAsync (const juce::File& destination, bool embedSamples);
    void loadPresetAsync (const juce::File& presetFile);
    /** Copies a preset file elsewhere; with embedSamples the copy carries every sample it can find. */
    void exportPresetAsync (const juce::File& source, const juce::File& destination, bool embedSamples);
    // Bumped each time a preset save finishes successfully, so the editor can reveal the file.
    uint32_t getPresetSaveVersion() const noexcept { return presetSaveVersion; }
    juce::File getLastSavedPresetFile() const { return lastSavedPresetFile; }

    // Browser audition: previews a file on the main output without touching the kit.
    // Message thread only. Decoding runs on auditionPool; playback is mixed in processBlock.
    /** Plays the file from the start (decoding it first unless it is the loaded clip). */
    void auditionFileAsync (const juce::File& file);
    void pauseAudition()  { auditionPlayer.pause(); }
    void resumeAudition() { auditionPlayer.resume(); }
    void stopAudition();
    void setAuditionGainDb (float db) { auditionPlayer.setGainDb (db); }
    /** Current audition state for the browser. Also frees clips the audio thread has let go of. */
    AuditionStatus pollAuditionStatus();

    // Header status text. Sized well past 255 bytes so ONNX Runtime / GPU errors
    // (which embed long build paths) are not cut off when copied.
    using StatusText = RtText<2048>;

    struct UiSliceSnapshot
    {
        struct UiSessionSample
        {
            int sampleId = 0;
            int startSample = 0;
            int numFrames = 0;
            RtText<256> fileName;
            SampleParams params;   // this sample's own sample-level parameters
            SampleData::SessionSample::Availability availability = SampleData::SessionSample::Availability::loaded;
            int parkedSlices = 0;  // slices kept for a placeholder until it is relinked

            bool isPlaceholder() const { return availability != SampleData::SessionSample::Availability::loaded; }
        };

        int numSessionSamples = 0;
        int numMissingSessionSamples = 0;
        int selectedSessionSampleId = -1;
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
        StatusText statusMessage;
        StemJobState stemJobState = StemJobState::idle;
        float stemJobProgress = 0.0f;
        int stemJobSourceSampleId = -1;
        StemModelDownloadState stemDownloadState = StemModelDownloadState::idle;
        float stemDownloadProgress = 0.0f;
        std::array<UiSessionSample, SampleData::kMaxSessionSamples> sessionSamples {};
        std::array<Slice, SliceManager::kMaxSlices> slices {};

        /** The sample-level parameters of a session sample (factory defaults if unknown). */
        const SampleParams& sampleParamsFor (int sampleId) const
        {
            static const SampleParams defaults = SampleParams::factoryDefaults();
            for (int i = 0; i < numSessionSamples; ++i)
                if (sessionSamples[(size_t) i].sampleId == sampleId)
                    return sessionSamples[(size_t) i].params;
            return defaults;
        }
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

    // True while any session sample is a placeholder awaiting relink
    std::atomic<bool> sampleMissing { false };
    std::atomic<int> sampleAvailability { (int) SampleStateEmpty };
    std::atomic<int> selectedSessionSampleId { -1 };

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
    };

    // A finished decode, handed from the decode worker to processBlock. The shared_ptr (and its
    // control block) is made on the worker, so taking the sample on the audio thread allocates nothing.
    struct CompletedLoad
    {
        int token = 0;
        LoadKind kind = LoadKindReplace;
        SampleData::SnapshotPtr sample;
    };

    struct UiStatusMessage
    {
        enum class Source
        {
            none = 0,
            generic,
            droppedCommands,
            midiLimit,
            transientInfo,   // non-warning message that clears itself like a warning
        };

        StatusText text;
        bool isWarning = false;
        Source source = Source::none;
        int64_t shownAtMs = 0;
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

    struct RestoreOptions
    {
        // Maps a sample path stored in the state to the file to load. Null = use paths as saved.
        std::function<juce::File (const juce::String&)> resolveSamplePath;
        // Keep machine/user preferences (UI scale, NRPN settings, stem compute device) instead of
        // taking them from the state. Used for presets, which travel between machines.
        bool preserveUserPrefs = false;
    };

    struct PresetJobResult
    {
        enum class Kind { save, load, exportCopy };
        Kind kind = Kind::save;
        int generation = 0;
        juce::File presetFile;
        juce::Result result = juce::Result::ok();
        juce::MemoryBlock state;                               // load only
        std::map<juce::String, juce::File> resolvedSamples;    // load only: saved path -> local file
        int missingSamples = 0;                                // load, exportCopy
    };

    // Returns false if the state was rejected; a malformed slice table can leave it partly applied.
    bool restoreState (const void* data, int sizeInBytes, const RestoreOptions& options);
    // Engine side of the saved state (the format itself is in state/SessionState). Capture reads
    // the engine; apply writes it and runs only inside restoreState's ScopedProcessingSuspend.
    SessionState captureSessionState();
    void applySessionState (const SessionState& state, const RestoreOptions& options);
    juce::StringArray getStateSampleFilePaths() const;
    void postPresetJobResult (PresetJobResult result);
    void handlePresetJobCompletionOnMessageThread();
    void applyLoadedPreset (const PresetJobResult& job);
    void handleAuditionCompletionOnMessageThread();

    void drainCommands();
    void handleCommand (const Command& cmd);

    // MIDI edit settings sampled once per block so every event in the block sees one value
    struct MidiEditBlockSettings
    {
        bool enabled   = false;
        int  channel   = 0;
        bool consumeCc = false;
    };
    void processMidiEvent (const juce::MidiMessage& msg, const MidiEditBlockSettings& edit);
    void filterConsumedMidiEditEvents (juce::MidiBuffer& midi, const MidiEditBlockSettings& edit);
    std::optional<MidiEditEvent> tryParseMidiEditMessage (const juce::MidiMessage& msg);
    void handleMidiEditEvent (const MidiEditEvent& event);
    void applyMidiEditZoomSteps (int steps);
    bool beginMidiSliceBoundaryGestureIfNeeded (int sliceIdx, bool isStart);
    void applyMidiSliceBoundarySteps (int sliceIdx, bool isStart, int steps);
    void applyLiveDragBoundsToSlice();
    void setMidiBoundaryPreviewState (int sliceIdx, int startSample, int endSample, bool isStart);
    void commitMidiSliceBoundaryGestureIfIdle (int blockSamples);
    void clearMidiEditGestureState();
    // Message thread: starts a load and returns its token (beginSampleLoad + startSampleLoad).
    int requestSampleLoad (const std::vector<juce::File>& files, LoadKind kind,
                           const std::vector<int>* sampleIds = nullptr,
                           const std::vector<int>* importSliceSampleIds = nullptr);
    // Any thread, atomics only: makes a load of `kind` the newest, so older loads' results are
    // dropped, and returns its token.
    int beginSampleLoad (LoadKind kind) noexcept;
    // Message thread: queues the decode for a token from beginSampleLoad.
    void startSampleLoad (int token, LoadKind kind, const std::vector<juce::File>& files,
                          const std::vector<int>* sampleIds, const std::vector<int>* importSliceSampleIds);

    // A reload the audio thread needs (undo/redo, a sample-rate retry). It calls beginSampleLoad
    // itself; timerCallback starts the decode. Paths are ref-counted strings copied into cleared
    // slots, so building a request allocates and frees nothing on the audio thread.
    struct LoadRequest
    {
        int token = 0;
        LoadKind kind = LoadKindPreserveSlices;
        bool updatesPendingStateFile = false;   // undo/redo: the first path (or none) is the pending state file
        int numSamples = 0;
        std::array<int, SampleData::kMaxSessionSamples> sampleIds {};
        std::array<juce::String, SampleData::kMaxSessionSamples> paths;
        int numImportIds = 0;
        std::array<int, SampleData::kMaxSessionSamples> importIds {};
    };
    SpscQueue<LoadRequest, 32> loadRequests;   // roomy: stale requests are skipped, a dropped one would lose a reload
    void startRequestedLoads();   // message thread
    void clearVoicesBeforeSampleSwap();
    void clampSlicesToSampleBounds();
    void deleteSessionSample (int sampleId);   // audio thread, applies a prepared SampleRemoval

    // Removing a session sample, prepared on the message thread (deleteSessionSampleAsync) so the
    // audio thread only swaps in the shortened kit. `source` is the kit it was built from; the
    // audio thread drops the removal if another load has replaced that kit since.
    struct SampleRemoval
    {
        int sampleId = -1;
        int nextSelectedSampleId = -1;
        SampleData::SnapshotPtr source;
        SampleData::SnapshotPtr rebuilt;   // null when the last sample goes
    };
    SpscQueue<SampleRemoval, 16> sampleRemovals;   // message thread -> audio thread
    void syncAllSliceOwnershipToCurrentSession();
    void syncSliceOwnershipFromAbsolute (Slice& slice, bool clampToSessionBounds = true);
    void syncSliceAbsoluteToCurrentSession (Slice& slice);
    void syncAllSliceAbsolutePositions();
    int findSessionSampleIndexById (int sampleId) const;
    int generateSessionSampleId();
    void publishUiSliceSnapshot();
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void handleStemJobCompletionOnMessageThread();
    void handleDownloadCompletionsOnMessageThread();
    void handleUrlImportCompletionOnMessageThread();
    void updateSampleMissingState();
    void deselectParkedSlice();
    bool isPlaceholderSample (int sampleId) const;
    void setUiStatusMessage (const juce::String& text, bool isWarning,
                             UiStatusMessage::Source source = UiStatusMessage::Source::generic);
    void setUiStatusMessage (const StatusText& text,
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
    // Sample-level parameters (audio thread). Every slice resolves against its own sample's
    // set; previews (lazy chop, shift preview) use the selected sample's set.
    const SampleParams& paramsForSample (int sampleId) const { return sampleParams.get (sampleId); }
    const SampleParams& selectedSampleParams() const;
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
    std::atomic<bool> pendingSetSampleParam { false };
    std::atomic<uint64_t> pendingSetSampleParamPayload { 0 };
    std::atomic<int> pendingSetSampleParamSampleId { -1 };
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

    juce::File stemModelFolder;
    StemComputeDevice stemComputeDevice = StemComputeDevice::cpu;
    StemModelDownloadJob stemModelDownloadJob;
    std::atomic<int> stemManifestGeneration { 0 };
    StemManifestRefreshJob stemManifestRefreshJob;   // after stemManifestGeneration: its callback uses it
    OrtBundleDownloadJob ortBundleDownloadJob;
    juce::SharedResourcePointer<UpdateChecker> updateChecker;
    UrlImport::UrlImportJob urlImportJob;
    UrlImportSettings urlImportSettings;                            // message thread
    UrlImportOutcome lastUrlImportOutcome;                          // message thread
    std::optional<UrlImport::JobResult> pendingUrlImportSave;       // message thread
    // Audio thread writes, message thread reads: lets a load say why it can't finish (no audio device).
    std::atomic<juce::uint32> lastProcessBlockMs { 0 };
    const juce::uint32 createdMs = juce::Time::getMillisecondCounter();
    void warnIfAudioNotRunning();

    StemSeparationJob stemJob;
    juce::String lastStemFailureMessage;       // message thread only
    juce::String lastStemFailureDiagnostics;   // message thread only
    std::atomic<bool> stemCompletionQueued { false };

    // Stem metadata keyed by session sample ID.
    // Stored separately because DecodedSample is immutable (const shared_ptr).
    struct StemMetaEntry
    {
        int sampleId = -1;
        StemMetadata meta;
    };
    std::array<StemMetaEntry, SampleData::kMaxSessionSamples> stemMetaEntries {};
    int stemMetaEntryCount = 0;

    SampleParamTable sampleParams;   // audio-thread owned; restoreState writes it like stemMetaEntries
    void setStemMeta (int sampleId, const StemMetadata& meta);
    StemMetadata getStemMeta (int sampleId) const;

    // Pending stem metadata to apply after the import load completes
    struct PendingStemImport
    {
        std::vector<StemMetadata> metas;   // one per imported file, built on the message thread
        int parentSourceSampleId = -1;
    };
    std::atomic<PendingStemImport*> pendingStemImport { nullptr };

    // Heap objects the audio thread lets go of. processBlock moves them in here instead of freeing
    // them; timerCallback() frees them on the message thread.
    struct RetiredObjects
    {
        SampleData::SnapshotPtr sample;
        std::unique_ptr<CompletedLoad> load;
        std::unique_ptr<FailedLoadResult> failure;
        std::unique_ptr<PendingStemImport> stemImport;
    };
    SpscQueue<RetiredObjects, 64> retiredObjects;
    void retire (RetiredObjects&& objects) noexcept;   // audio thread

    // Presets. A newer preset load, a sample load or a host state restore bumps the generation,
    // which makes any in-flight preset load drop its result. Declared before fileLoadPool so
    // these outlive the pool's worker thread.
    std::atomic<int> presetLoadGeneration { 0 };
    juce::CriticalSection presetJobLock;
    std::vector<PresetJobResult> completedPresetJobs;   // guarded by presetJobLock
    uint32_t presetSaveVersion = 0;                      // message thread only
    juce::File lastSavedPresetFile;                      // message thread only

    juce::ThreadPool fileLoadPool { 1 };

    // Audition. The player is read by the audio thread; the rest is message-thread state plus the
    // decode result handed over under auditionLock. auditionPool is declared after them so its
    // worker stops before they are destroyed.
    struct AuditionJobResult
    {
        int generation = 0;
        std::shared_ptr<const AuditionClip> clip;
    };
    static constexpr double kMaxAuditionSeconds = 120.0;   // longer files preview their start
    AuditionPlayer auditionPlayer;
    std::atomic<int> auditionGeneration { 0 };
    juce::File auditionFile;                                            // message thread only
    AuditionStatus::State auditionPendingState = AuditionStatus::State::idle; // decoding/failed, message thread
    juce::CriticalSection auditionLock;
    std::optional<AuditionJobResult> completedAudition;                 // guarded by auditionLock
    juce::ThreadPool auditionPool { 1 };
    std::atomic<int> nextLoadToken { 0 };
    std::atomic<int> nextSessionSampleId { 0 };
    std::atomic<int> latestLoadToken { 0 };
    std::atomic<int> latestLoadKind { (int) LoadKindReplace };
    std::atomic<CompletedLoad*> completedLoadData { nullptr };
    std::atomic<FailedLoadResult*> completedLoadFailure { nullptr };
    std::array<UiSliceSnapshot, 2> uiSliceSnapshots {};
    std::atomic<int> uiSliceSnapshotIndex { 0 };
    std::atomic<bool> uiSnapshotDirty { true };
    std::atomic<uint32_t> uiSnapshotVersion { 0 };
    PendingSliceTimelineRemap pendingSliceTimelineRemap;

    std::array<bool, kMidiNoteCount> heldNotes {};

    std::array<ParamUndoState, 2> pendingParamRestoreStates {};
    std::atomic<int> pendingParamRestoreIndex { -1 };
    std::array<UiStatusMessage, 2> uiStatusMessages {};
    std::atomic<int> uiStatusMessageIndex { 0 };
    std::atomic<int> pendingStateRestoreToken { 0 };
    juce::File pendingStateFile;
    mutable juce::CriticalSection pendingStateFileLock;

    // Cached parameter pointers
    std::atomic<float>* maxVoicesParam   = nullptr;
    std::atomic<float>* uiScaleParam          = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntersectProcessor)
};
