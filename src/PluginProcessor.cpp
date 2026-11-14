#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AppFiles.h"
#include "Constants.h"
#include "PresetFile.h"
#include "audio/GrainEngine.h"
#include "audio/AudioAnalysis.h"
#include "audio/SliceEdits.h"
#include "state/SessionState.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>

namespace
{
class SampleDecodeJob final : public juce::ThreadPoolJob
{
public:
    using SuccessFn = std::function<void (int, IntersectProcessor::LoadKind,
                                          std::unique_ptr<SampleData::DecodedSample>)>;
    using FailureFn = std::function<void (int, IntersectProcessor::LoadKind, const juce::File&)>;

    SampleDecodeJob (std::vector<juce::File> sourceFiles,
                     std::vector<int> sourceSampleIds,
                     std::vector<int> sliceImportSampleIds,
                     double targetRate, int loadToken,
                     IntersectProcessor::LoadKind kind,
                     SuccessFn onSuccessIn, FailureFn onFailureIn)
        : juce::ThreadPoolJob ("SampleDecodeJob"),
          files (std::move (sourceFiles)),
          sampleIds (std::move (sourceSampleIds)),
          importSliceSampleIds (std::move (sliceImportSampleIds)),
          sampleRate (targetRate),
          token (loadToken),
          loadKind (kind),
          onSuccess (std::move (onSuccessIn)),
          onFailure (std::move (onFailureIn))
    {
    }

    JobStatus runJob() override
    {
        if (files.empty())
            return jobHasFinished;

        auto decoded = SampleData::decodeFromFiles (files, sampleRate, &sampleIds, &importSliceSampleIds);
        if (shouldExit())
            return jobHasFinished;

        if (decoded != nullptr)
            onSuccess (token, loadKind, std::move (decoded));
        else
            onFailure (token, loadKind, files.front());
        return jobHasFinished;
    }

private:
    std::vector<juce::File> files;
    std::vector<int> sampleIds;
    std::vector<int> importSliceSampleIds;   // REX2 files whose embedded slices should be created
    double sampleRate = 44100.0;
    int token = 0;
    IntersectProcessor::LoadKind loadKind = IntersectProcessor::LoadKindReplace;
    SuccessFn onSuccess;
    FailureFn onFailure;
};

constexpr int kCurrentStateVersion = StateFormat::kCurrentStateVersion;
constexpr std::array<double, 6> kLegacyCommonSampleRates { 44100.0, 48000.0, 88200.0,
                                                           96000.0, 176400.0, 192000.0 };

static bool isCoalescableCommand (IntersectProcessor::CommandType type)
{
    return type == IntersectProcessor::CmdSetSliceParam
        || type == IntersectProcessor::CmdSetSampleParam
        || type == IntersectProcessor::CmdSetSliceBounds;
}

static bool isCriticalCommand (IntersectProcessor::CommandType type)
{
    switch (type)
    {
        case IntersectProcessor::CmdNone:
        case IntersectProcessor::CmdLazyChopStart:
        case IntersectProcessor::CmdLazyChopStop:
        case IntersectProcessor::CmdStretch:
        case IntersectProcessor::CmdToggleLock:
        case IntersectProcessor::CmdSetSliceParam:
        case IntersectProcessor::CmdSetSampleParam:
        case IntersectProcessor::CmdSetSliceBounds:
        case IntersectProcessor::CmdRepackMidi:
        case IntersectProcessor::CmdFileLoadCompleted:
        case IntersectProcessor::CmdFileLoadFailed:
        case IntersectProcessor::CmdBeginGesture:
            return false;

        case IntersectProcessor::CmdCreateSlice:
        case IntersectProcessor::CmdDeleteSlice:
        case IntersectProcessor::CmdDeleteSessionSample:
        case IntersectProcessor::CmdDuplicateSlice:
        case IntersectProcessor::CmdSplitSlice:
        case IntersectProcessor::CmdTransientChop:
        case IntersectProcessor::CmdUndo:
        case IntersectProcessor::CmdRedo:
        case IntersectProcessor::CmdPanic:
        case IntersectProcessor::CmdSelectSlice:
        case IntersectProcessor::CmdSetRootNote:
        case IntersectProcessor::CmdStemSeparate:
            return true;
    }

    return false;
}

constexpr int kNrpnCcMsb  = 99;   // NRPN MSB address byte
constexpr int kNrpnCcLsb  = 98;   // NRPN LSB address byte
constexpr int kNrpnCcIncr = 96;   // Data Increment (CC 96 = value up)
constexpr int kNrpnCcDecr = 97;   // Data Decrement (CC 97 = value down)
constexpr int kMidiEditNrpnZoom       = 8193;
constexpr int kMidiEditNrpnSliceStart = 8194;
constexpr int kMidiEditNrpnSliceEnd   = 8195;
constexpr int kMidiEditMinSliceLength = kMinSliceLengthSamples;
constexpr int kMidiEditStepsPerView   = 192;
constexpr float kMidiEditZoomClampMax = 16384.0f;
constexpr double kMidiEditZoomStepsPerOctave = 6.0;
constexpr double kMidiEditGestureIdleSeconds = 0.3;

union FloatBits { float f; uint32_t u; };

uint64_t packPendingSliceParamPayload (int field, float value)
{
    FloatBits fb;
    fb.f = value;
    return (uint64_t) (uint32_t) field << 32
        | (uint64_t) fb.u;
}

void unpackPendingSliceParamPayload (uint64_t payload, int& field, float& value)
{
    field = (int) (payload >> 32);
    FloatBits fb;
    fb.u = (uint32_t) (payload & 0xFFFFFFFFu);
    value = fb.f;
}

PreviewStretchParams makePreviewStretchParams (const SampleParams& sampleLevel,
                                               float dawBpm,
                                               double sampleRate,
                                               const SampleData* sample)
{
    PreviewStretchParams params;
    params.stretchEnabled = sampleLevel.stretchEnabled;
    params.algorithm = sampleLevel.algorithm;
    params.repitchMode = sampleLevel.repitchMode;
    params.bpm = sampleLevel.bpm;
    params.pitch = sampleLevel.pitchSemitones;
    params.dawBpm = dawBpm;
    params.tonality = sampleLevel.tonalityHz;
    params.formant = sampleLevel.formantSemitones;
    params.formantComp = sampleLevel.formantComp;
    params.grainMode = sampleLevel.grainMode;
    params.sampleRate = sampleRate;
    params.sample = sample;
    return params;
}

VoiceStartParams makeVoiceStartParams (const SampleParams& sampleLevel,
                                       int rootNote,
                                       int note,
                                       float velocity,
                                       float dawBpm)
{
    VoiceStartParams params;
    params.note = note;
    params.velocity = velocity;
    params.sampleBpm = sampleLevel.bpm;
    params.samplePitch = sampleLevel.pitchSemitones;
    params.sampleAlgorithm = sampleLevel.algorithm;
    params.sampleRepitchMode = sampleLevel.repitchMode;
    params.sampleAttackSec = sampleLevel.attackSec;
    params.sampleDecaySec = sampleLevel.decaySec;
    params.sampleSustain = sampleLevel.sustain;
    params.sampleReleaseSec = sampleLevel.releaseSec;
    params.sampleMuteGroup = sampleLevel.muteGroup;
    params.sampleStretch = sampleLevel.stretchEnabled;
    params.dawBpm = dawBpm;
    params.sampleTonality = sampleLevel.tonalityHz;
    params.sampleFormant = sampleLevel.formantSemitones;
    params.sampleFormantComp = sampleLevel.formantComp;
    params.sampleGrainMode = sampleLevel.grainMode;
    params.sampleVolume = sampleLevel.volumeDb;
    params.sampleReleaseTail = sampleLevel.releaseTail;
    params.sampleReverse = sampleLevel.reverse;
    params.sampleLoopMode = sampleLevel.loopMode;
    params.sampleOneShot = sampleLevel.oneShot;
    params.sampleCentsDetune = sampleLevel.centsDetune;
    params.sampleFilterEnabled = sampleLevel.filterEnabled;
    params.sampleFilterType = sampleLevel.filterType;
    params.sampleFilterSlope = sampleLevel.filterSlope;
    params.sampleFilterCutoff = sampleLevel.filterCutoffHz;
    params.sampleFilterReso = sampleLevel.filterReso;
    params.sampleFilterDrive = sampleLevel.filterDrive;
    params.sampleFilterAsym = sampleLevel.filterAsym;
    params.sampleFilterKeyTrack = sampleLevel.filterKeyTrack;
    params.sampleFilterEnvAttackSec = sampleLevel.filterEnvAttackSec;
    params.sampleFilterEnvDecaySec = sampleLevel.filterEnvDecaySec;
    params.sampleFilterEnvSustain = sampleLevel.filterEnvSustain;
    params.sampleFilterEnvReleaseSec = sampleLevel.filterEnvReleaseSec;
    params.sampleFilterEnvAmount = sampleLevel.filterEnvAmount;
    params.sampleCrossfadePct = sampleLevel.crossfadePct;
    params.rootNote = rootNote;
    return params;
}
} // namespace

IntersectProcessor::IntersectProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Main", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Out 2", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 3", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 4", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 5", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 6", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 7", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 8", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 9", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 10", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 11", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 12", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 13", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 14", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 15", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 16", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMETERS", ParamLayout::createLayout())
{
    maxVoicesParam   = apvts.getRawParameterValue (ParamIds::maxVoices);
	    uiScaleParam = apvts.getRawParameterValue (ParamIds::uiScale);

    // Download jobs signal completion from their own thread. Coalesced with
    // stemJob's use of AsyncUpdater; handleAsyncUpdate drains all three.
    stemModelDownloadJob.onTerminalState = [this] { triggerAsyncUpdate(); };
    ortBundleDownloadJob.onTerminalState = [this] { triggerAsyncUpdate(); };
    urlImportJob.onTerminalState = [this] { triggerAsyncUpdate(); };
    stemManifestRefreshJob.onManifestChanged = [this] { stemManifestGeneration.fetch_add (1, std::memory_order_acq_rel); };

	    publishUiSliceSnapshot();
    startTimerHz (20);   // frees what the audio thread retires (timerCallback)
}

IntersectProcessor::~IntersectProcessor()
{
    stopTimer();
        cancelPendingUpdate();
	    auditionPool.removeAllJobs (true, 2000);
	    fileLoadPool.removeAllJobs (true, 5000);
	    auto* pending = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
	    delete pending;
	    auto* failed = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
	    delete failed;
	    auto* stemPending = pendingStemImport.exchange (nullptr, std::memory_order_acq_rel);
	    delete stemPending;
}

const SampleParams& IntersectProcessor::selectedSampleParams() const
{
    return paramsForSample (selectedSessionSampleId.load (std::memory_order_relaxed));
}

void IntersectProcessor::setStandaloneTransportBpm (float newBpm) noexcept
{
    standaloneTransportBpm.store (juce::jlimit (20.0f, 999.0f, newBpm), std::memory_order_relaxed);
}

float IntersectProcessor::getStandaloneTransportBpm() const noexcept
{
    return standaloneTransportBpm.load (std::memory_order_relaxed);
}

void IntersectProcessor::setUiStatusMessage (const juce::String& text, bool isWarning,
                                             UiStatusMessage::Source source)
{
    StatusText rtText;
    rtText.assign (text);
    setUiStatusMessage (rtText, isWarning, source);
}

void IntersectProcessor::setUiStatusMessage (const StatusText& text,
                                             bool isWarning,
                                             UiStatusMessage::Source source)
{
    const int writeIndex = 1 - uiStatusMessageIndex.load (std::memory_order_relaxed);
    auto& status = uiStatusMessages[(size_t) writeIndex];
    status.text = text;
    status.isWarning = isWarning;
    status.source = source;
    status.shownAtMs = juce::Time::currentTimeMillis();
    uiStatusMessageIndex.store (writeIndex, std::memory_order_release);
    uiSnapshotDirty.store (true, std::memory_order_release);
}

void IntersectProcessor::clearUiStatusMessage()
{
    const int writeIndex = 1 - uiStatusMessageIndex.load (std::memory_order_relaxed);
    uiStatusMessages[(size_t) writeIndex] = {};
    uiStatusMessageIndex.store (writeIndex, std::memory_order_release);
}

bool IntersectProcessor::clearDroppedCommandWarning()
{
    if (getUiStatusMessage().source != UiStatusMessage::Source::droppedCommands)
        return false;

    clearUiStatusMessage();
    return true;
}

void IntersectProcessor::setDroppedCommandWarning (uint32_t droppedCount, uint32_t droppedCriticalCount)
{
    StatusText text;
    text.appendAscii ("Warning: dropped ");
    text.appendUnsigned (droppedCount);
    text.appendAscii (" commands");
    if (droppedCriticalCount > 0)
    {
        text.appendAscii (" (");
        text.appendUnsigned (droppedCriticalCount);
        text.appendAscii (" critical)");
    }
    text.appendAscii (". Some edits may not have applied.");
    setUiStatusMessage (text, true, UiStatusMessage::Source::droppedCommands);
}

const IntersectProcessor::UiStatusMessage& IntersectProcessor::getUiStatusMessage() const
{
    return uiStatusMessages[(size_t) uiStatusMessageIndex.load (std::memory_order_acquire)];
}

void IntersectProcessor::timerCallback()
{
    retiredObjects.clear();
    startRequestedLoads();

    // Noticing a finished stem separation here (not in processBlock) keeps the audio thread from
    // posting a message for it, and handles it even when no audio is running.
    const auto stemState = stemJob.getState();
    if ((stemState == StemJobState::completed
         || stemState == StemJobState::failed
         || stemState == StemJobState::cancelled)
        && ! stemCompletionQueued.exchange (true, std::memory_order_acq_rel))
        handleStemJobCompletionOnMessageThread();
}

void IntersectProcessor::retire (RetiredObjects&& objects) noexcept
{
    if (objects.sample == nullptr && objects.load == nullptr && objects.failure == nullptr
        && objects.stemImport == nullptr)
        return;

    // The queue only fills up if the message thread stalls for seconds; the objects are then
    // freed here after all, when the caller's temporary goes away.
    retiredObjects.push (std::move (objects));
}

void IntersectProcessor::handleAsyncUpdate()
{
    handleStemJobCompletionOnMessageThread();
    handleDownloadCompletionsOnMessageThread();
    handleUrlImportCompletionOnMessageThread();
    handlePresetJobCompletionOnMessageThread();
    handleAuditionCompletionOnMessageThread();
}

//==============================================================================
// Import from URL

juce::File IntersectProcessor::getDefaultUrlImportFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("INTERSECT Downloads");
}

juce::File IntersectProcessor::getResolvedUrlImportFolder() const
{
    return urlImportSettings.folder != juce::File() ? urlImportSettings.folder : getDefaultUrlImportFolder();
}

void IntersectProcessor::startUrlImport (const juce::String& url, bool allowLong)
{
    if (urlImportJob.isBusy())
    {
        setUiStatusMessage ("A URL import is already running", true);
        return;
    }

    UrlImport::ImportRequest request;
    request.url = url.trim();
    request.channel = urlImportSettings.channel;
    request.format = urlImportSettings.format;
    request.allowLong = allowLong;
    if (! urlImportSettings.askEachTime)
        request.destinationFolder = getResolvedUrlImportFolder();

    if (! urlImportJob.startImport (request))
    {
        setUiStatusMessage ("Couldn't start the URL import", true);
        return;
    }
    setUiStatusMessage ("Importing from URL...", false);
}

void IntersectProcessor::startUrlToolsUpdate()
{
    if (! urlImportJob.startUpdate (urlImportSettings.channel))
        setUiStatusMessage ("Wait for the current URL import to finish", true);
}

void IntersectProcessor::startUrlToolsRemoval()
{
    if (! urlImportJob.startRemove())
        setUiStatusMessage ("Wait for the current URL import to finish", true);
}

std::optional<UrlImport::JobResult> IntersectProcessor::takePendingUrlImportSave()
{
    auto pending = std::move (pendingUrlImportSave);
    pendingUrlImportSave.reset();
    return pending;
}

void IntersectProcessor::placeUrlImport (const juce::File& source, const juce::File& destination,
                                         const juce::File& workFolder, const juce::String& title)
{
    if (! urlImportJob.startPlace (source, destination, workFolder, title))
        setUiStatusMessage ("Couldn't save the import while another URL task runs", true);
}

void IntersectProcessor::discardUrlImport (const juce::File& workFolder)
{
    if (workFolder != juce::File())
        workFolder.deleteRecursively();
    setUiStatusMessage ("Import discarded", false);
}

void IntersectProcessor::handleUrlImportCompletionOnMessageThread()
{
    auto finished = urlImportJob.consumeResult();
    if (! finished.has_value())
        return;

    using Status = UrlImport::JobResult::Status;
    using Mode = UrlImport::JobResult::Mode;
    auto& result = *finished;

    if (result.mode == Mode::update || result.mode == Mode::remove)
    {
        setUiStatusMessage (result.message, result.status != Status::completed || result.warning.isNotEmpty());
        return;
    }

    // "Ask each time": the file waits in staging until the editor's save dialog places it. With no
    // editor open there is no one to ask, so it goes to the folder instead.
    if (result.mode == Mode::import && result.status == Status::completed && result.workFolder != juce::File())
    {
        if (getActiveEditor() != nullptr)
        {
            pendingUrlImportSave = std::move (result);
            return;
        }
        const auto destination = UrlImport::uniqueFileIn (getResolvedUrlImportFolder(),
                                                          UrlImport::makeOutputFileName (result.title, {}, result.file.getFileExtension()));
        urlImportJob.startPlace (result.file, destination, result.workFolder, result.title);
        return;
    }

    lastUrlImportOutcome.status = result.status;
    lastUrlImportOutcome.url = result.request.url;
    lastUrlImportOutcome.message = result.message;
    lastUrlImportOutcome.details = result.details;
    ++lastUrlImportOutcome.serial;

    switch (result.status)
    {
        case Status::completed:
            addFilesToKit ({ result.file });
            setUiStatusMessage (result.warning.isNotEmpty() ? result.message + ". " + result.warning : result.message,
                                result.warning.isNotEmpty());
            break;
        case Status::cancelled:
            setUiStatusMessage (result.message, false);
            break;
        case Status::failed:
            setUiStatusMessage (result.message, true);
            break;
        case Status::needsLongConfirm:
        case Status::liveRefused:
            break;   // the import panel asks or explains
    }
}

void IntersectProcessor::addFilesToKit (const std::vector<juce::File>& files)
{
    std::vector<juce::File> audio;
    for (const auto& file : files)
        if (file.existsAsFile() && AppFiles::isSupportedAudioFile (file))
            audio.push_back (file);
    if (audio.empty())
        return;

    stopAudition();
    enqueueUiUndoSnapshot();
    const bool append = sampleData.isLoaded();
    loadFilesAsync (audio, append);
    if (! append)
    {
        zoom.store (1.0f);
        scroll.store (0.0f);
    }
}

void IntersectProcessor::handleDownloadCompletionsOnMessageThread()
{
    // Runs on the message thread, so consumeResultMessage() locking is safe.
    // No-op for any job not currently in a terminal state.
    auto drain = [this] (auto& job)
    {
        const auto s = job.getState();
        if (s == StemModelDownloadState::completed)
            setUiStatusMessage (job.consumeResultMessage(), false);
        else if (s == StemModelDownloadState::failed
              || s == StemModelDownloadState::cancelled)
            setUiStatusMessage (job.consumeResultMessage(), true);
    };

    drain (stemModelDownloadJob);
    drain (ortBundleDownloadJob);
}

void IntersectProcessor::handleStemJobCompletionOnMessageThread()
{
    stemCompletionQueued.store (false, std::memory_order_release);

    const auto stemState = stemJob.getState();
    if (stemState != StemJobState::completed
        && stemState != StemJobState::failed
        && stemState != StemJobState::cancelled)
        return;

    bool loadStateChanged = false;

    if (stemState == StemJobState::completed)
    {
        const int sourceSampleId = stemJob.getSourceSampleId();
        auto stemResult = stemJob.consumeResult();
        if (! stemResult.stemFiles.empty())
        {
            auto* pending = new PendingStemImport();
            pending->parentSourceSampleId = sourceSampleId;
            for (const auto& name : stemResult.stemNames)
            {
                StemMetadata meta;
                meta.parentSourceSampleId = sourceSampleId;
                meta.role = stemRoleFromString (name);
                meta.isGenerated = true;
                meta.setName (name);
                pending->metas.push_back (meta);
            }
            auto* old = pendingStemImport.exchange (pending, std::memory_order_acq_rel);
            delete old;

            loadFilesAsync (stemResult.stemFiles, true);
            setUiStatusMessage (stemResult.warningMessage.isNotEmpty()
                                    ? stemResult.warningMessage
                                    : juce::String ("Stems imported"),
                                stemResult.warningMessage.isNotEmpty());
            loadStateChanged = true;
        }
    }
    else if (stemState == StemJobState::cancelled)
    {
        (void) stemJob.consumeResult();
        setUiStatusMessage ("Stem separation cancelled", true);
    }
    else if (stemState == StemJobState::failed)
    {
        auto stemResult = stemJob.consumeResult();
        lastStemFailureMessage = stemResult.errorMessage.isNotEmpty() ? stemResult.errorMessage
                                                                      : juce::String ("Stem separation failed");
        lastStemFailureDiagnostics = stemResult.diagnostics;
        setUiStatusMessage (lastStemFailureMessage, true);
    }

    // The job has closed its model file: a replaced version an update couldn't delete can go now.
    if (stemModelDownloadJob.getState() != StemModelDownloadState::downloading)
        (void) removeAllReplacedStemModelFiles (getResolvedStemModelFolder());

    uiSnapshotDirty.store (true, std::memory_order_release);

    if (loadStateChanged)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void IntersectProcessor::setPendingStateFile (const juce::File& file)
{
    const juce::ScopedLock sl (pendingStateFileLock);
    pendingStateFile = file;
}

void IntersectProcessor::clearPendingStateFile()
{
    const juce::ScopedLock sl (pendingStateFileLock);
    pendingStateFile = juce::File{};
}

juce::File IntersectProcessor::getPendingStateFile() const
{
    const juce::ScopedLock sl (pendingStateFileLock);
    return pendingStateFile;
}

ParamUndoState IntersectProcessor::captureParamUndoState() const
{
    ParamUndoState state;
    if (maxVoicesParam != nullptr)
        state.maxVoices = maxVoicesParam->load (std::memory_order_relaxed);
    return state;
}

bool IntersectProcessor::enqueueUiUndoSnapshot()
{
    // Message thread only — builds a pre-change snapshot from the latest published
    // UI state and queues it for the audio thread to push into undoMgr.
    // Returns false if the FIFO was full (snapshot dropped).
    const auto& ui = getUiSliceSnapshot();

    UndoManager::Snapshot snap;
    if (auto sampleSnap = sampleData.getSnapshot())
    {
        snap.numSessionSamples = juce::jmin ((int) sampleSnap->sessionSamples.size(),
                                             SampleData::kMaxSessionSamples);
        for (int i = 0; i < snap.numSessionSamples; ++i)
            snap.sessionSamples[(size_t) i] = sampleSnap->sessionSamples[(size_t) i];
    }
    snap.selectedSessionSampleId = selectedSessionSampleId.load (std::memory_order_relaxed);
    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
        snap.slices[(size_t) i] = ui.slices[(size_t) i];
    snap.numSlices = ui.numSlices;
    snap.selectedSlice = ui.selectedSlice;
    snap.rootNote = ui.rootNote;
    snap.params = captureParamUndoState();
    // Message thread: take the sample-level params from the published UI snapshot, not the
    // audio-thread table.
    for (int i = 0; i < ui.numSessionSamples; ++i)
        snap.sampleParams.set (ui.sessionSamples[(size_t) i].sampleId, ui.sessionSamples[(size_t) i].params);
    snap.midiSelectsSlice = midiSelectsSlice.load (std::memory_order_relaxed);
    snap.snapToZeroCrossing = snapToZeroCrossing.load (std::memory_order_relaxed);

    const auto scope = uiUndoFifo.write (1);
    if (scope.blockSize1 > 0)
        uiUndoBuffer[(size_t) scope.startIndex1] = std::move (snap);
    else if (scope.blockSize2 > 0)
        uiUndoBuffer[(size_t) scope.startIndex2] = std::move (snap);
    else
        return false; // FIFO full — caller should not latch baseline flag

    return true;
}

void IntersectProcessor::applyParamUndoState (const ParamUndoState& state)
{
    if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (ParamIds::maxVoices)))
        param->setValueNotifyingHost (param->convertTo0to1 (state.maxVoices));
}

bool IntersectProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Main output must be stereo
    if (layouts.outputBuses.isEmpty())
        return false;
    if (layouts.outputBuses[0] != juce::AudioChannelSet::stereo())
        return false;

    // Additional outputs: stereo or disabled
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        if (! layouts.outputBuses[i].isDisabled()
            && layouts.outputBuses[i] != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void IntersectProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    voicePool.prepareToPlay (sampleRate, samplesPerBlock);
    std::fill (std::begin (heldNotes), std::end (heldNotes), false);

    auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap != nullptr
        && ! sampleSnap->sessionSamples.empty()
        && (sampleSnap->decodedSampleRate <= 0.0
            || std::abs (sampleSnap->decodedSampleRate - sampleRate) > 0.01))
    {
        if (sampleData.isLoaded() && sampleData.getNumFrames() > 0)
        {
            primePendingSliceTimelineRemap (kCurrentStateVersion,
                                            sampleData.getNumFrames(),
                                            sampleSnap->decodedSampleRate,
                                            sampleData.getSourceNumFrames(),
                                            sampleData.getSourceSampleRate());
        }
        std::vector<juce::File> files;
        std::vector<int> sampleIds;
        files.reserve (sampleSnap->sessionSamples.size());
        sampleIds.reserve (sampleSnap->sessionSamples.size());
        for (const auto& sample : sampleSnap->sessionSamples)
        {
            files.emplace_back (sample.filePath);
            sampleIds.push_back (sample.sampleId);
        }
        requestSampleLoad (files, LoadKindPreserveSlices, &sampleIds);
    }
}

void IntersectProcessor::releaseResources() {}

int IntersectProcessor::beginSampleLoad (LoadKind kind) noexcept
{
    const int token = nextLoadToken.fetch_add (1, std::memory_order_relaxed) + 1;
    latestLoadToken.store (token, std::memory_order_release);
    latestLoadKind.store ((int) kind, std::memory_order_release);

    if (kind == LoadKindReplace)
        clearPendingSliceTimelineRemap();
    else if (pendingSliceTimelineRemap.active.load (std::memory_order_acquire))
        pendingSliceTimelineRemap.expectedLoadToken.store (token, std::memory_order_release);
    return token;
}

int IntersectProcessor::requestSampleLoad (const std::vector<juce::File>& files, LoadKind kind,
                                           const std::vector<int>* sampleIds,
                                           const std::vector<int>* importSliceSampleIds)
{
    const int token = beginSampleLoad (kind);

    // Keep only the latest completed decode payload (processBlock also skips stale ones).
    delete completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
    delete completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);

    startSampleLoad (token, kind, files, sampleIds, importSliceSampleIds);
    return token;
}

void IntersectProcessor::startRequestedLoads()
{
    LoadRequest request;
    while (loadRequests.pop (request))
    {
        if (request.updatesPendingStateFile)
        {
            if (request.numSamples > 0)
                setPendingStateFile (juce::File (request.paths[0]));
            else
                clearPendingStateFile();
        }

        // Nothing to load, or a newer load has started since the audio thread asked.
        if (request.numSamples == 0 || request.token != latestLoadToken.load (std::memory_order_acquire))
            continue;

        std::vector<juce::File> files;
        std::vector<int> sampleIds;
        std::vector<int> importIds;
        for (int i = 0; i < request.numSamples; ++i)
        {
            files.emplace_back (request.paths[(size_t) i]);
            sampleIds.push_back (request.sampleIds[(size_t) i]);
        }
        for (int i = 0; i < request.numImportIds; ++i)
            importIds.push_back (request.importIds[(size_t) i]);

        startSampleLoad (request.token, request.kind, files, &sampleIds, &importIds);
    }
}

void IntersectProcessor::startSampleLoad (int token, LoadKind kind, const std::vector<juce::File>& files,
                                          const std::vector<int>* sampleIds,
                                          const std::vector<int>* importSliceSampleIds)
{
    if (files.empty())
        return;

    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    std::vector<int> copiedSampleIds;
    if (sampleIds != nullptr)
        copiedSampleIds = *sampleIds;
    else
    {
        copiedSampleIds.reserve (files.size());
        for (size_t i = 0; i < files.size(); ++i)
            copiedSampleIds.push_back (generateSessionSampleId());
    }

    auto onSuccess = [this] (int finishedToken, LoadKind finishedKind,
                             std::unique_ptr<SampleData::DecodedSample> decoded)
    {
        if (finishedToken != latestLoadToken.load (std::memory_order_acquire))
            return;

        auto load = std::make_unique<CompletedLoad>();
        load->token = finishedToken;
        load->kind = finishedKind;
        load->sample = std::move (decoded);   // the shared_ptr's control block is allocated here, off the audio thread
        delete completedLoadData.exchange (load.release(), std::memory_order_acq_rel);
        latestLoadKind.store ((int) finishedKind, std::memory_order_release);
    };

    auto onFailure = [this] (int finishedToken, LoadKind finishedKind, const juce::File& failedFile)
    {
        if (finishedToken != latestLoadToken.load (std::memory_order_acquire))
            return;

        auto* payload = new FailedLoadResult();
        payload->token = finishedToken;
        payload->kind = finishedKind;
        payload->fileName.assign (failedFile.getFileName());
        auto* old = completedLoadFailure.exchange (payload, std::memory_order_acq_rel);
        delete old;
    };

    std::vector<int> copiedImportIds;
    if (importSliceSampleIds != nullptr)
        copiedImportIds = *importSliceSampleIds;

    fileLoadPool.addJob (new SampleDecodeJob (files, copiedSampleIds, std::move (copiedImportIds),
                                              sr, token, kind, onSuccess, onFailure), true);
}

void IntersectProcessor::loadFileAsync (const juce::File& file)
{
    loadFilesAsync (std::vector<juce::File> { file }, false);
}

void IntersectProcessor::warnIfAudioNotRunning()
{
    // Loads finish on the audio thread. The standalone app without an audio output device never
    // runs it, so a load would otherwise wait silently forever.
    if (wrapperType != wrapperType_Standalone)
        return;

    const auto now = juce::Time::getMillisecondCounter();
    const auto last = lastProcessBlockMs.load (std::memory_order_relaxed);
    if (now - createdMs < 3000 || (last != 0 && now - last < 1500))
        return;

    setUiStatusMessage ("No audio output is running, so INTERSECT can't finish loading. "
                        "Choose an output device in Options > Audio/MIDI Settings.", true);
}

void IntersectProcessor::loadFilesAsync (const std::vector<juce::File>& files, bool append)
{
    pendingStateRestoreToken.store (0, std::memory_order_release);
    if (files.empty())
        return;

    warnIfAudioNotRunning();

    presetLoadGeneration.fetch_add (1, std::memory_order_acq_rel);   // a newer load wins over a pending preset

    std::vector<juce::File> orderedFiles;
    std::vector<int> sampleIds;

    if (append)
    {
        if (auto sampleSnap = sampleData.getSnapshot())
        {
            orderedFiles.reserve (sampleSnap->sessionSamples.size() + files.size());
            sampleIds.reserve (sampleSnap->sessionSamples.size() + files.size());
            for (const auto& sample : sampleSnap->sessionSamples)
            {
                orderedFiles.emplace_back (sample.filePath);
                sampleIds.push_back (sample.sampleId);
            }
        }
        // Only the newly appended files get their embedded REX2 slices imported.
        std::vector<int> newSampleIds;
        newSampleIds.reserve (files.size());
        for (const auto& file : files)
        {
            orderedFiles.push_back (file);
            sampleIds.push_back (generateSessionSampleId());
            newSampleIds.push_back (sampleIds.back());
        }
        setPendingStateFile (orderedFiles.front());
        requestSampleLoad (orderedFiles, LoadKindPreserveSlices, &sampleIds, &newSampleIds);
        return;
    }

    orderedFiles = files;
    sampleIds.reserve (files.size());
    for (size_t i = 0; i < files.size(); ++i)
        sampleIds.push_back (generateSessionSampleId());
    setPendingStateFile (orderedFiles.front());
    requestSampleLoad (orderedFiles, LoadKindReplace, &sampleIds, &sampleIds);
}

void IntersectProcessor::relinkSessionSamplesAsync (const std::vector<std::pair<int, juce::File>>& relinks)
{
    auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap == nullptr || sampleSnap->sessionSamples.empty() || relinks.empty())
        return;

    std::vector<juce::File> files;
    std::vector<int> sampleIds;
    files.reserve (sampleSnap->sessionSamples.size());
    sampleIds.reserve (sampleSnap->sessionSamples.size());
    bool changed = false;

    for (const auto& sample : sampleSnap->sessionSamples)
    {
        juce::File file (sample.filePath);
        for (const auto& [sampleId, replacement] : relinks)
        {
            if (sampleId == sample.sampleId && replacement.existsAsFile())
            {
                changed = changed || replacement != file || sample.isPlaceholder();
                file = replacement;
            }
        }
        files.push_back (file);
        sampleIds.push_back (sample.sampleId);
    }

    if (! changed || ! enqueueUiUndoSnapshot())
        return;

    // Same ids, so per-sample params, stem metadata and the parked slices all carry over.
    pendingStateRestoreToken.store (0, std::memory_order_release);
    setPendingStateFile (files.front());
    requestSampleLoad (files, LoadKindPreserveSlices, &sampleIds);
}

void IntersectProcessor::reorderSessionSampleAsync (int sourceSampleId, int targetIndex)
{
    auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap == nullptr || sampleSnap->sessionSamples.empty())
        return;

    std::vector<juce::File> files;
    std::vector<int> sampleIds;
    files.reserve (sampleSnap->sessionSamples.size());
    sampleIds.reserve (sampleSnap->sessionSamples.size());
    int sourceIndex = -1;

    for (int i = 0; i < (int) sampleSnap->sessionSamples.size(); ++i)
    {
        const auto& sample = sampleSnap->sessionSamples[(size_t) i];
        if (sample.sampleId == sourceSampleId)
            sourceIndex = i;
        files.emplace_back (sample.filePath);
        sampleIds.push_back (sample.sampleId);
    }

    if (sourceIndex < 0)
        return;

    targetIndex = juce::jlimit (0, (int) files.size() - 1, targetIndex);
    if (targetIndex == sourceIndex)
        return;

    if (! enqueueUiUndoSnapshot())
        return;

    auto movedFile = files[(size_t) sourceIndex];
    auto movedId = sampleIds[(size_t) sourceIndex];
    files.erase (files.begin() + sourceIndex);
    sampleIds.erase (sampleIds.begin() + sourceIndex);
    files.insert (files.begin() + targetIndex, movedFile);
    sampleIds.insert (sampleIds.begin() + targetIndex, movedId);
    requestSampleLoad (files, LoadKindPreserveSlices, &sampleIds);
}

void IntersectProcessor::deleteSessionSampleAsync (int sampleId)
{
    if (sampleId < 0)
        return;

    // Build the shortened kit here (it copies the remaining audio), so the audio thread only swaps it in.
    const auto source = sampleData.getSnapshot();
    if (source == nullptr || source->sessionSamples.empty())
        return;

    const auto& samples = source->sessionSamples;
    const auto removed = std::find_if (samples.begin(), samples.end(),
                                       [sampleId] (const auto& sample) { return sample.sampleId == sampleId; });
    if (removed == samples.end())
        return;

    SampleRemoval removal;
    removal.sampleId = sampleId;
    removal.source = source;
    if (samples.size() > 1)
    {
        const auto removeIndex = (size_t) std::distance (samples.begin(), removed);
        std::vector<SampleData::SessionSample> remaining;
        remaining.reserve (samples.size() - 1);
        for (size_t i = 0; i < samples.size(); ++i)
            if (i != removeIndex)
                remaining.push_back (samples[i]);

        removal.nextSelectedSampleId = removeIndex < remaining.size() ? remaining[removeIndex].sampleId
                                                                      : remaining.back().sampleId;
        removal.rebuilt = SampleData::rebuildWithSessionSamples (*source, remaining);
    }

    if (! sampleRemovals.push (std::move (removal)))
        return;   // sixteen removals already waiting for the audio thread

    Command cmd;
    cmd.type = CmdDeleteSessionSample;
    cmd.intParam1 = sampleId;
    pushCommand (cmd);
}

void IntersectProcessor::setStemMeta (int sampleId, const StemMetadata& meta)
{
    for (int i = 0; i < stemMetaEntryCount; ++i)
    {
        if (stemMetaEntries[(size_t) i].sampleId == sampleId)
        {
            stemMetaEntries[(size_t) i].meta = meta;
            return;
        }
    }
    if (stemMetaEntryCount < SampleData::kMaxSessionSamples)
    {
        stemMetaEntries[(size_t) stemMetaEntryCount].sampleId = sampleId;
        stemMetaEntries[(size_t) stemMetaEntryCount].meta = meta;
        ++stemMetaEntryCount;
    }
}

StemMetadata IntersectProcessor::getStemMeta (int sampleId) const
{
    for (int i = 0; i < stemMetaEntryCount; ++i)
        if (stemMetaEntries[(size_t) i].sampleId == sampleId)
            return stemMetaEntries[(size_t) i].meta;
    return {};
}

juce::File IntersectProcessor::getStemModelFolder() const
{
    return stemModelFolder;
}

juce::File IntersectProcessor::getResolvedStemModelFolder() const
{
    return stemModelFolder == juce::File() ? getDefaultStemModelFolder() : stemModelFolder;
}

void IntersectProcessor::setStemModelFolder (const juce::File& modelFolder)
{
    stemModelFolder = modelFolder;
}

std::vector<StemModelId> IntersectProcessor::getInstalledStemModels() const
{
    return scanInstalledStemModels (getResolvedStemModelFolder());
}

std::vector<StemModelId> IntersectProcessor::getStemModelUpdates() const
{
    return scanStemModelUpdates (getResolvedStemModelFolder());
}

juce::String IntersectProcessor::getStemFailureDiagnostics (const juce::String& statusText) const
{
    // Only while the status bar still shows that failure (it may be shortened to fit).
    return statusText.isNotEmpty() && lastStemFailureMessage.startsWith (statusText) ? lastStemFailureDiagnostics
                                                                                     : juce::String();
}

bool IntersectProcessor::isStemModelInstalled (const StemModelId& modelId) const
{
    const auto file = resolveStemModelFile (getResolvedStemModelFolder(), modelId);
    return file != juce::File() && file.existsAsFile();
}

void IntersectProcessor::startStemModelDownload (const std::vector<StemModelId>& modelIds)
{
    if (modelIds.empty())
        return;

    if (stemModelDownloadJob.getState() == StemModelDownloadState::downloading)
    {
        setUiStatusMessage ("Model download already in progress", true);
        return;
    }

    if (! stemModelDownloadJob.start (getResolvedStemModelFolder(), modelIds))
    {
        setUiStatusMessage ("Unable to start model download", true);
        return;
    }

    setUiStatusMessage ("Downloading stem models...", false);
    uiSnapshotDirty.store (true, std::memory_order_release);
}

void IntersectProcessor::refreshStemModelManifestIfStale()
{
    // A model download fetches the manifest itself.
    if (stemModelDownloadJob.getState() == StemModelDownloadState::downloading || stemManifestRefreshJob.isRunning())
        return;

    // Old versions an update couldn't delete because a separation still had them open.
    (void) removeAllReplacedStemModelFiles (getResolvedStemModelFolder());

    const auto folder = getResolvedStemModelFolder();
    const auto cached = getStemModelManifestFile (folder);
    constexpr juce::int64 refreshIntervalMs = 6 * 60 * 60 * 1000;
    if (cached.existsAsFile()
        && (juce::Time::getCurrentTime() - cached.getLastModificationTime()).inMilliseconds() < refreshIntervalMs)
        return;

    (void) stemManifestRefreshJob.start (folder);
}

void IntersectProcessor::cancelStemModelDownload()
{
    stemModelDownloadJob.cancel();
}

juce::File IntersectProcessor::getOrtRootFolder() const
{
    return getDefaultOrtRootFolder();
}

std::vector<OrtBundleId> IntersectProcessor::getInstalledOrtBundles() const
{
    return scanInstalledOrtBundles (getOrtRootFolder());
}

juce::String IntersectProcessor::getActiveOrtBundleDirectoryName() const
{
    return readActiveOrtBundleDirectoryName (getOrtRootFolder());
}

bool IntersectProcessor::setActiveOrtBundle (OrtBundleId bundleId)
{
    if (const auto* entry = findOrtBundleCatalogEntry (bundleId))
    {
        if (writeActiveOrtBundleDirectoryName (getOrtRootFolder(), entry->directoryName))
        {
            setUiStatusMessage (juce::String ("Activated ") + entry->menuLabel
                                + " (restart INTERSECT to use it)", false);
            return true;
        }
    }
    setUiStatusMessage ("Failed to activate ONNX Runtime bundle", true);
    return false;
}

void IntersectProcessor::startOrtBundleDownload (OrtBundleId bundleId)
{
    if (ortBundleDownloadJob.getState() == StemModelDownloadState::downloading)
    {
        setUiStatusMessage ("ONNX Runtime download already in progress", true);
        return;
    }

    if (! ortBundleDownloadJob.start (getOrtRootFolder(), bundleId, INTERSECT_ORT_VERSION))
    {
        setUiStatusMessage ("Unable to start ONNX Runtime download", true);
        return;
    }

    if (const auto* entry = findOrtBundleCatalogEntry (bundleId))
        setUiStatusMessage ("Downloading " + entry->menuLabel + "...", false);
    else
        setUiStatusMessage ("Downloading ONNX Runtime...", false);

    uiSnapshotDirty.store (true, std::memory_order_release);
}

void IntersectProcessor::cancelOrtBundleDownload()
{
    ortBundleDownloadJob.cancel();
}

void IntersectProcessor::cancelStemSeparation()
{
    if (stemJob.getState() != StemJobState::idle)
        stemJob.cancel();
}

void IntersectProcessor::startStemSeparation (int sampleId,
                                              const StemModelId& modelId,
                                              const StemSelection& stemSelection,
                                              StemExportMode exportMode,
                                              const juce::File& outputFolder)
{
    if (stemJob.getState() != StemJobState::idle)
    {
        setUiStatusMessage ("Stem separation already in progress", true);
        return;
    }

    const auto modelFolder = getResolvedStemModelFolder();
    const auto catalogEntry = getEffectiveStemModelCatalogEntry (modelId, modelFolder);
    // An earlier version of the model (listed in its `replaces`) keeps working until the user updates.
    const auto modelFile = catalogEntry.isValid() ? findInstalledStemModelFile (catalogEntry, modelFolder) : juce::File();
    if (modelFile == juce::File() || ! modelFile.existsAsFile())
    {
        setUiStatusMessage ("Selected stem model is not installed", true);
        return;
    }

    const auto selection = normaliseStemSelection (stemSelection, catalogEntry.getNumOutputs());
    if (selection.empty())
    {
        setUiStatusMessage ("Select at least one stem", true);
        return;
    }

    if (stemComputeDevice == StemComputeDevice::gpu)
    {
        if (const auto gpuError = getStemGpuAvailabilityError(); gpuError.isNotEmpty())
        {
            setUiStatusMessage (gpuError, true);
            return;
        }
    }

    auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap == nullptr)
        return;

    // Find the session sample by ID
    const SampleData::SessionSample* targetSample = nullptr;
    for (const auto& sample : sampleSnap->sessionSamples)
    {
        if (sample.sampleId == sampleId)
        {
            targetSample = &sample;
            break;
        }
    }

    if (targetSample == nullptr)
        return;

    // Extract the audio region for this sample
    const int startFrame = targetSample->startFrame;
    const int numFrames = targetSample->numFrames;
    const int numChannels = sampleSnap->buffer.getNumChannels();

    juce::AudioBuffer<float> regionAudio (2, numFrames);
    for (int ch = 0; ch < 2; ++ch)
    {
        const int srcCh = std::min (ch, numChannels - 1);
        regionAudio.copyFrom (ch, 0, sampleSnap->buffer, srcCh, startFrame, numFrames);
    }

    juce::File outputRoot = outputFolder;
    if (outputRoot == juce::File())
    {
        const auto sourceFile = juce::File (targetSample->filePath);
        if (! juce::File::isAbsolutePath (targetSample->filePath))
        {
            setUiStatusMessage ("Choose an export folder for this sample", true);
            return;
        }

        outputRoot = sourceFile.getParentDirectory();
        if (outputRoot == juce::File())
        {
            setUiStatusMessage ("Choose an export folder for this sample", true);
            return;
        }
    }

    auto sourceName = targetSample->fileName.upToLastOccurrenceOf (".", false, false);
    if (sourceName.isEmpty())
        sourceName = targetSample->fileName;
    auto timestamp = juce::String (juce::Time::currentTimeMillis());
    auto jobDir = outputRoot.getChildFile (sourceName + "_" + catalogEntry.id + "_" + timestamp);

    stemJob.start (regionAudio, sampleSnap->decodedSampleRate, sampleId,
                   sourceName, catalogEntry, selection, exportMode,
                   stemComputeDevice, modelFile, jobDir);
    uiSnapshotDirty.store (true, std::memory_order_release);
}

void IntersectProcessor::clearPendingSliceTimelineRemap()
{
    pendingSliceTimelineRemap.active.store (false, std::memory_order_release);
    pendingSliceTimelineRemap.expectedLoadToken.store (0, std::memory_order_release);
    pendingSliceTimelineRemap.savedStateVersion.store (0, std::memory_order_release);
    pendingSliceTimelineRemap.savedDecodedNumFrames.store (0, std::memory_order_release);
    pendingSliceTimelineRemap.savedDecodedSampleRate.store (0.0, std::memory_order_release);
    pendingSliceTimelineRemap.savedSourceNumFrames.store (0, std::memory_order_release);
    pendingSliceTimelineRemap.savedSourceSampleRate.store (0.0, std::memory_order_release);
}

void IntersectProcessor::primePendingSliceTimelineRemap (int savedStateVersion,
                                                         int savedDecodedNumFrames,
                                                         double savedDecodedSampleRate,
                                                         int savedSourceNumFrames,
                                                         double savedSourceSampleRate)
{
    pendingSliceTimelineRemap.savedStateVersion.store (savedStateVersion, std::memory_order_release);
    pendingSliceTimelineRemap.savedDecodedNumFrames.store (savedDecodedNumFrames, std::memory_order_release);
    pendingSliceTimelineRemap.savedDecodedSampleRate.store (savedDecodedSampleRate, std::memory_order_release);
    pendingSliceTimelineRemap.savedSourceNumFrames.store (savedSourceNumFrames, std::memory_order_release);
    pendingSliceTimelineRemap.savedSourceSampleRate.store (savedSourceSampleRate, std::memory_order_release);
    pendingSliceTimelineRemap.active.store (true, std::memory_order_release);
}

bool IntersectProcessor::applyPendingSliceTimelineRemap()
{
    if (! pendingSliceTimelineRemap.active.load (std::memory_order_acquire))
        return false;

    const int expectedToken = pendingSliceTimelineRemap.expectedLoadToken.load (std::memory_order_acquire);
    if (expectedToken <= 0 || expectedToken != latestLoadToken.load (std::memory_order_acquire))
        return false;

    const int numSlices = sliceManager.getNumSlices();
    if (numSlices <= 0)
    {
        clearPendingSliceTimelineRemap();
        return false;
    }

    const auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap == nullptr)
        return false;

    const int targetFrames = sampleSnap->decodedNumFrames > 0
        ? sampleSnap->decodedNumFrames
        : sampleSnap->buffer.getNumSamples();
    const double targetRate = sampleSnap->decodedSampleRate;
    if (targetFrames <= 0)
        return false;

    int sourceFrames = pendingSliceTimelineRemap.savedDecodedNumFrames.load (std::memory_order_acquire);
    double sourceRate = pendingSliceTimelineRemap.savedDecodedSampleRate.load (std::memory_order_acquire);
    const int savedStateVersion = pendingSliceTimelineRemap.savedStateVersion.load (std::memory_order_acquire);

    if ((sourceFrames <= 0 || sourceRate <= 0.0) && savedStateVersion < kCurrentStateVersion)
    {
        int maxEnd = 0;
        for (int i = 0; i < numSlices; ++i)
            maxEnd = juce::jmax (maxEnd, sliceManager.getSlice (i).endSample);

        int savedSourceFrames = pendingSliceTimelineRemap.savedSourceNumFrames.load (std::memory_order_acquire);
        double savedSourceRate = pendingSliceTimelineRemap.savedSourceSampleRate.load (std::memory_order_acquire);
        if (savedSourceFrames <= 0)
            savedSourceFrames = sampleSnap->sourceNumFrames;
        if (savedSourceRate <= 0.0)
            savedSourceRate = sampleSnap->sourceSampleRate;
        if (maxEnd > 0 && savedSourceFrames > 0 && savedSourceRate > 0.0)
        {
            const double sourceDurationSec = (double) savedSourceFrames / savedSourceRate;
            if (sourceDurationSec > 0.0)
            {
                double bestRate = 0.0;
                double bestError = 1.0e12;
                for (double candidateRate : kLegacyCommonSampleRates)
                {
                    const double expectedFrames = sourceDurationSec * candidateRate;
                    const double error = std::abs (expectedFrames - (double) maxEnd);
                    if (error < bestError)
                    {
                        bestError = error;
                        bestRate = candidateRate;
                    }
                }

                if (bestRate > 0.0)
                {
                    const double expectedFrames = sourceDurationSec * bestRate;
                    const double relativeError = expectedFrames > 0.0 ? bestError / expectedFrames : 1.0;
                    if (relativeError <= 0.03)
                    {
                        sourceRate = bestRate;
                        sourceFrames = juce::jmax (1, (int) std::round (expectedFrames));
                    }
                }
            }
        }
    }

    if (sourceFrames <= 0)
    {
        clearPendingSliceTimelineRemap();
        return false;
    }

    // Prefer the sample-rate ratio when the saved rate is known: the total frame count also
    // counts silent placeholders and per-file lengths, so it only stands in for old states
    // that never recorded their rate.
    const double ratio = (sourceRate > 0.0 && targetRate > 0.0)
        ? targetRate / sourceRate
        : (double) targetFrames / (double) sourceFrames;
    const bool needsRemap = std::abs (ratio - 1.0) > 0.0001;

    if (! needsRemap)
    {
        clearPendingSliceTimelineRemap();
        return false;
    }

    for (int i = 0; i < numSlices; ++i)
    {
        auto& s = sliceManager.getSlice (i);
        s.startInSample = juce::jmax (0, (int) std::round ((double) s.startInSample * ratio));
        s.endInSample = juce::jmax (s.startInSample + 1, (int) std::round ((double) s.endInSample * ratio));
        s.startSample = juce::jmax (0, (int) std::round ((double) s.startSample * ratio));
        s.endSample = juce::jmax (s.startSample + 1, (int) std::round ((double) s.endSample * ratio));
        if (s.loopStartOffset > 0)
            s.loopStartOffset = juce::jmax (0, (int) std::round ((double) s.loopStartOffset * ratio));
        if (s.loopLength > 0)
            s.loopLength = juce::jmax (1, (int) std::round ((double) s.loopLength * ratio));
    }

    clearPendingSliceTimelineRemap();
    return true;
}

void IntersectProcessor::clearVoicesBeforeSampleSwap()
{
    // Stop lazy chop before killing voices; its preview voice and buffer pointer
    // must be torn down before the sample data is replaced.
    lazyChop.stop (voicePool, sliceManager);

    // Kill all active voices before replacing the sample buffer
    // to prevent dangling reads from stretcher pipelines.
    for (int vi = 0; vi < VoicePool::kMaxVoices; ++vi)
    {
        auto& v = voicePool.getVoice (vi);
        const auto voiceIndex = static_cast<size_t> (vi);
        v.active = false;
        voicePool.voicePositions[voiceIndex].store (0.0f,
            vi == VoicePool::kPreviewVoiceIndex
                ? std::memory_order_release
                : std::memory_order_relaxed);
    }
}

int IntersectProcessor::findSessionSampleIndexById (int sampleId) const
{
    const auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap == nullptr)
        return -1;

    for (int i = 0; i < (int) sampleSnap->sessionSamples.size(); ++i)
        if (sampleSnap->sessionSamples[(size_t) i].sampleId == sampleId)
            return i;
    return -1;
}

int IntersectProcessor::generateSessionSampleId()
{
    return nextSessionSampleId.fetch_add (1, std::memory_order_relaxed) + 1;
}

bool IntersectProcessor::isPlaceholderSample (int sampleId) const
{
    const auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap == nullptr)
        return false;

    for (const auto& sample : sampleSnap->sessionSamples)
        if (sample.sampleId == sampleId)
            return sample.isPlaceholder();
    return false;
}

void IntersectProcessor::updateSampleMissingState()
{
    const auto sampleSnap = sampleData.getSnapshot();
    const bool anyMissing = sampleSnap != nullptr && sampleSnap->hasPlaceholders();
    sampleMissing.store (anyMissing, std::memory_order_relaxed);
    sampleAvailability.store ((int) (anyMissing ? SampleStateMissingAwaitingRelink
                                     : sampleData.isLoaded() ? SampleStateLoaded
                                                             : SampleStateEmpty),
                              std::memory_order_relaxed);
}

// Parked slices can't be edited (their sample has no audio), so they never stay selected.
void IntersectProcessor::deselectParkedSlice()
{
    const int sel = sliceManager.selectedSlice.load (std::memory_order_relaxed);
    if (sel >= 0 && sel < sliceManager.getNumSlices() && sliceManager.getSlice (sel).parked)
        sliceManager.selectedSlice.store (-1, std::memory_order_relaxed);
}

void IntersectProcessor::syncSliceOwnershipFromAbsolute (Slice& slice, bool clampToSessionBounds)
{
    const auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap == nullptr || sampleSnap->sessionSamples.empty())
        return;

    // Slices of a placeholder are parked: their in-sample positions are the only record of
    // where they sit in the real file, so the silent slot must never rewrite them.
    if (isPlaceholderSample (slice.sampleId))
    {
        syncSliceAbsoluteToCurrentSession (slice);
        return;
    }

    int bestIndex = 0;
    for (int i = 0; i < (int) sampleSnap->sessionSamples.size(); ++i)
    {
        const auto& sample = sampleSnap->sessionSamples[(size_t) i];
        const int sampleEnd = sample.startFrame + sample.numFrames;
        if (slice.startSample >= sample.startFrame && slice.startSample < sampleEnd)
        {
            bestIndex = i;
            break;
        }
    }

    const auto& owner = sampleSnap->sessionSamples[(size_t) bestIndex];
    const int sampleStart = owner.startFrame;
    int startInSample = slice.startSample - sampleStart;
    int endInSample = slice.endSample - sampleStart;

    if (clampToSessionBounds)
    {
        startInSample = juce::jlimit (0, juce::jmax (0, owner.numFrames - 1), startInSample);
        endInSample = juce::jlimit (startInSample + 1, juce::jmax (startInSample + 1, owner.numFrames), endInSample);
        if (endInSample - startInSample < kMinSliceLengthSamples)
            endInSample = juce::jmin (owner.numFrames, startInSample + kMinSliceLengthSamples);
    }

    slice.sampleId = owner.sampleId;
    slice.parked = owner.isPlaceholder();
    slice.startInSample = startInSample;
    slice.endInSample = endInSample;
    slice.startSample = sampleStart + startInSample;
    slice.endSample = sampleStart + endInSample;

    const int sLen = slice.endSample - slice.startSample;
    slice.loopStartOffset = juce::jlimit (0, juce::jmax (0, sLen - 1), slice.loopStartOffset);
    if (slice.loopLength > 0)
        slice.loopLength = juce::jlimit (1, juce::jmax (1, sLen - slice.loopStartOffset), slice.loopLength);
}

void IntersectProcessor::syncSliceAbsoluteToCurrentSession (Slice& slice)
{
    const auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap == nullptr || sampleSnap->sessionSamples.empty())
        return;

    const SampleData::SessionSample* owner = nullptr;
    for (const auto& sample : sampleSnap->sessionSamples)
    {
        if (sample.sampleId == slice.sampleId)
        {
            owner = &sample;
            break;
        }
    }
    if (owner == nullptr)
        owner = &sampleSnap->sessionSamples.front();

    const bool parked = owner->isPlaceholder() && slice.endInSample > slice.startInSample;
    int startInSample = slice.startInSample;
    int endInSample = slice.endInSample;
    if (endInSample <= startInSample)
    {
        startInSample = slice.startSample - owner->startFrame;
        endInSample = slice.endSample - owner->startFrame;
    }

    startInSample = juce::jlimit (0, juce::jmax (0, owner->numFrames - 1), startInSample);
    endInSample = juce::jlimit (startInSample + 1, juce::jmax (startInSample + 1, owner->numFrames), endInSample);
    if (endInSample - startInSample < kMinSliceLengthSamples)
        endInSample = juce::jmin (owner->numFrames, startInSample + kMinSliceLengthSamples);

    slice.sampleId = owner->sampleId;
    slice.parked = owner->isPlaceholder();
    slice.startSample = owner->startFrame + startInSample;
    slice.endSample = owner->startFrame + endInSample;

    // A parked slice only gets safe absolute bounds inside the silent slot; its in-sample
    // positions and loop settings wait untouched for the relinked file.
    if (parked)
        return;

    slice.startInSample = startInSample;
    slice.endInSample = endInSample;

    const int sLen = slice.endSample - slice.startSample;
    slice.loopStartOffset = juce::jlimit (0, juce::jmax (0, sLen - 1), slice.loopStartOffset);
    if (slice.loopLength > 0)
        slice.loopLength = juce::jlimit (1, juce::jmax (1, sLen - slice.loopStartOffset), slice.loopLength);
}

void IntersectProcessor::syncAllSliceOwnershipToCurrentSession()
{
    const int numSlices = sliceManager.getNumSlices();
    for (int i = 0; i < numSlices; ++i)
        syncSliceOwnershipFromAbsolute (sliceManager.getSlice (i));
}

void IntersectProcessor::syncAllSliceAbsolutePositions()
{
    const int numSlices = sliceManager.getNumSlices();
    for (int i = 0; i < numSlices; ++i)
        syncSliceAbsoluteToCurrentSession (sliceManager.getSlice (i));
}

void IntersectProcessor::clampSlicesToSampleBounds()
{
    const int maxLen = sampleData.getNumFrames();
    if (maxLen <= 1)
        return;

    const int numSlices = sliceManager.getNumSlices();
    for (int i = 0; i < numSlices; ++i)
    {
        auto& s = sliceManager.getSlice (i);
        s.startSample = juce::jlimit (0, maxLen - 1, s.startSample);
        s.endSample = juce::jlimit (s.startSample + 1, maxLen, s.endSample);
        syncSliceOwnershipFromAbsolute (s);
    }
}

void IntersectProcessor::deleteSessionSample (int sampleId)
{
    // Take the removal prepared for this sample (older ones whose command never ran are dropped).
    SampleRemoval removal;
    bool found = false;
    while (! found && sampleRemovals.pop (removal))
    {
        found = removal.sampleId == sampleId;
        if (! found)
        {
            RetiredObjects stale;
            stale.sample = std::move (removal.source);
            retire (std::move (stale));
            stale.sample = std::move (removal.rebuilt);
            retire (std::move (stale));
        }
    }

    // Only apply it to the kit it was built from; a load that landed in between wins.
    if (found && removal.source.get() == sampleData.getActive())
    {
        if (removal.rebuilt == nullptr)
        {
            clearVoicesBeforeSampleSwap();
            RetiredObjects unloaded;
            unloaded.sample = sampleData.clear();
            retire (std::move (unloaded));
            sampleParams.clear();
            sliceManager.clearAll();
            updateSampleMissingState();
            selectedSessionSampleId.store (-1, std::memory_order_relaxed);
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
        else
        {
            const int oldSelectedSlice = sliceManager.selectedSlice.load (std::memory_order_relaxed);
            const int oldNumSlices = sliceManager.getNumSlices();
            int writeSlice = 0;
            int nextSelectedSlice = -1;
            for (int i = 0; i < oldNumSlices; ++i)
            {
                const auto& src = sliceManager.getSlice (i);
                if (src.sampleId == sampleId)
                    continue;

                sliceManager.getSlice (writeSlice) = src;
                if (i == oldSelectedSlice)
                    nextSelectedSlice = writeSlice;
                ++writeSlice;
            }

            for (int i = writeSlice; i < SliceManager::kMaxSlices; ++i)
                sliceManager.getSlice (i).active = false;
            sliceManager.setNumSlices (writeSlice);
            sliceManager.selectedSlice.store (nextSelectedSlice, std::memory_order_relaxed);

            clearVoicesBeforeSampleSwap();
            RetiredObjects replaced;
            replaced.sample = sampleData.applyDecodedSample (std::move (removal.rebuilt));
            retire (std::move (replaced));
            sampleParams.syncToSession (sampleData.getSessionSamples());
            updateSampleMissingState();
            selectedSessionSampleId.store (removal.nextSelectedSampleId, std::memory_order_relaxed);
            syncAllSliceAbsolutePositions();
            clampSlicesToSampleBounds();
            deselectParkedSlice();
            sliceManager.rebuildMidiMap();
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }

    // Whatever the removal still holds is freed on the message thread.
    RetiredObjects done;
    done.sample = std::move (removal.source);
    retire (std::move (done));
    done.sample = std::move (removal.rebuilt);
    retire (std::move (done));
}

void IntersectProcessor::publishUiSliceSnapshot()
{
    const int writeIndex = 1 - uiSliceSnapshotIndex.load (std::memory_order_relaxed);
    auto& snap = uiSliceSnapshots[(size_t) writeIndex];
    auto sampleSnap = sampleData.getSnapshot();
    const auto& status = getUiStatusMessage();
    snap.numSlices = sliceManager.getNumSlices();
    snap.numSessionSamples = sampleSnap ? juce::jmin ((int) sampleSnap->sessionSamples.size(),
                                                      SampleData::kMaxSessionSamples) : 0;
    snap.selectedSessionSampleId = selectedSessionSampleId.load (std::memory_order_relaxed);
    snap.selectedSlice = sliceManager.selectedSlice.load (std::memory_order_relaxed);
    snap.rootNote = sliceManager.rootNote.load (std::memory_order_relaxed);
    snap.sampleLoaded = (sampleSnap != nullptr);
    snap.sampleMissing = sampleMissing.load (std::memory_order_relaxed);
    snap.sampleNumFrames = sampleSnap ? sampleSnap->buffer.getNumSamples() : 0;
    snap.sampleSampleRate = sampleSnap ? sampleSnap->decodedSampleRate : 0.0;
    snap.hasStatusMessage = ! status.text.isEmpty();
    snap.statusIsWarning = status.isWarning;
    snap.statusMessage = status.text;
    snap.stemJobState = stemJob.getState();
    snap.stemJobProgress = stemJob.getProgress();
    snap.stemJobSourceSampleId = stemJob.getSourceSampleId();
    snap.stemDownloadState = stemModelDownloadJob.getState();
    snap.stemDownloadProgress = stemModelDownloadJob.getProgress();
    if (sampleSnap != nullptr)
    {
        if (snap.numSessionSamples > 1)
        {
            snap.sampleFileName.clear();   // no juce::String here: this runs on the audio thread
            snap.sampleFileName.appendUnsigned ((uint32_t) snap.numSessionSamples);
            snap.sampleFileName.appendAscii (" samples loaded");
        }
        else
            snap.sampleFileName.assign (sampleSnap->fileName);
    }
    else
        snap.sampleFileName.clear();

    snap.numMissingSessionSamples = 0;
    for (int i = 0; i < SampleData::kMaxSessionSamples; ++i)
    {
        if (sampleSnap != nullptr && i < snap.numSessionSamples)
        {
            const auto& sample = sampleSnap->sessionSamples[(size_t) i];
            auto& uiSample = snap.sessionSamples[(size_t) i];
            uiSample.sampleId = sample.sampleId;
            uiSample.startSample = sample.startFrame;
            uiSample.numFrames = sample.numFrames;
            uiSample.fileName.assign (sample.fileName);
            uiSample.params = paramsForSample (sample.sampleId);
            uiSample.availability = sample.availability;
            uiSample.parkedSlices = 0;
            if (sample.isPlaceholder())
            {
                ++snap.numMissingSessionSamples;
                for (int s = 0; s < snap.numSlices; ++s)
                    if (sliceManager.getSlice (s).sampleId == sample.sampleId)
                        ++uiSample.parkedSlices;
            }
        }
        else
        {
            snap.sessionSamples[(size_t) i] = {};
        }
    }

    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
    {
        if (i < snap.numSlices)
            snap.slices[(size_t) i] = sliceManager.getSlice (i);
        else
            snap.slices[(size_t) i].active = false;
    }

    uiSliceSnapshotIndex.store (writeIndex, std::memory_order_release);
    uiSnapshotVersion.fetch_add (1, std::memory_order_release);
    uiSnapshotDirty.store (false, std::memory_order_release);
}

void IntersectProcessor::pushCommand (Command cmd)
{
    const bool critical = isCriticalCommand (cmd.type);
    const auto scope = commandFifo.write (1);
    if (scope.blockSize1 > 0)
    {
        commandBuffer[(size_t) scope.startIndex1] = std::move (cmd);
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }
    if (scope.blockSize2 > 0)
    {
        commandBuffer[(size_t) scope.startIndex2] = std::move (cmd);
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }

    if (enqueueCoalescedCommand (cmd))
    {
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }

    if (critical && enqueueOverflowCommand (std::move (cmd)))
    {
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }

    droppedCommandCount.fetch_add (1, std::memory_order_relaxed);
    droppedCommandTotal.fetch_add (1, std::memory_order_relaxed);
    if (critical)
    {
        droppedCriticalCommandCount.fetch_add (1, std::memory_order_relaxed);
        droppedCriticalCommandTotal.fetch_add (1, std::memory_order_relaxed);
    }
}

bool IntersectProcessor::enqueueOverflowCommand (Command cmd)
{
    const int write = overflowWriteIndex.load (std::memory_order_relaxed);
    const int read = overflowReadIndex.load (std::memory_order_acquire);
    const int next = (write + 1) % kOverflowFifoSize;
    if (next == read)
        return false;

    overflowCommandBuffer[(size_t) write] = std::move (cmd);
    overflowWriteIndex.store (next, std::memory_order_release);
    return true;
}

void IntersectProcessor::drainOverflowCommands (bool& handledAny)
{
    for (;;)
    {
        const int read = overflowReadIndex.load (std::memory_order_relaxed);
        const int write = overflowWriteIndex.load (std::memory_order_acquire);
        if (read == write)
            break;

        handleCommand (overflowCommandBuffer[(size_t) read]);
        overflowReadIndex.store ((read + 1) % kOverflowFifoSize, std::memory_order_release);
        handledAny = true;
    }
}

bool IntersectProcessor::enqueueCoalescedCommand (const Command& cmd)
{
    if (! isCoalescableCommand (cmd.type))
        return false;

    if (cmd.type == CmdSetSliceParam)
    {
        pendingSetSliceParamPayload.store (packPendingSliceParamPayload (cmd.intParam1, cmd.floatParam1),
                                           std::memory_order_relaxed);
        pendingSetSliceParamIdx.store (cmd.sliceIdx, std::memory_order_relaxed);
        pendingSetSliceParam.store (true, std::memory_order_release);
        return true;
    }

    if (cmd.type == CmdSetSampleParam)
    {
        pendingSetSampleParamPayload.store (packPendingSliceParamPayload (cmd.intParam2, cmd.floatParam1),
                                            std::memory_order_relaxed);
        pendingSetSampleParamSampleId.store (cmd.intParam1, std::memory_order_relaxed);
        pendingSetSampleParam.store (true, std::memory_order_release);
        return true;
    }

    if (cmd.type == CmdSetSliceBounds)
    {
        const int end = cmd.numPositions > 0 ? cmd.positions[0] : (int) cmd.floatParam1;
        pendingSetSliceBoundsSequence.fetch_add (1u, std::memory_order_acq_rel);
        pendingSetSliceBoundsIdx.store (cmd.intParam1, std::memory_order_relaxed);
        pendingSetSliceBoundsStart.store (cmd.intParam2, std::memory_order_relaxed);
        pendingSetSliceBoundsEnd.store (end, std::memory_order_relaxed);
        pendingSetSliceBoundsSequence.fetch_add (1u, std::memory_order_release);
        pendingSetSliceBounds.store (true, std::memory_order_release);
        return true;
    }

    return false;
}

void IntersectProcessor::drainCoalescedCommands (bool& handledAny)
{
    if (pendingSetSliceBounds.exchange (false, std::memory_order_acq_rel))
    {
        Command cmd;
        cmd.type = CmdSetSliceBounds;
        for (;;)
        {
            const uint32_t startSeq = pendingSetSliceBoundsSequence.load (std::memory_order_acquire);
            if ((startSeq & 1u) != 0u)
                continue;

            const int idx = pendingSetSliceBoundsIdx.load (std::memory_order_relaxed);
            const int start = pendingSetSliceBoundsStart.load (std::memory_order_relaxed);
            const int end = pendingSetSliceBoundsEnd.load (std::memory_order_relaxed);
            const uint32_t endSeq = pendingSetSliceBoundsSequence.load (std::memory_order_acquire);

            if (startSeq == endSeq)
            {
                cmd.intParam1 = idx;
                cmd.intParam2 = start;
                cmd.positions[0] = end;
                cmd.numPositions = 1;
                handleCommand (cmd);
                handledAny = true;
                break;
            }
        }
    }

    if (pendingSetSliceParam.exchange (false, std::memory_order_acq_rel))
    {
        Command cmd;
        cmd.type = CmdSetSliceParam;
        unpackPendingSliceParamPayload (pendingSetSliceParamPayload.load (std::memory_order_relaxed),
                                        cmd.intParam1,
                                        cmd.floatParam1);
        cmd.sliceIdx = pendingSetSliceParamIdx.load (std::memory_order_relaxed);
        handleCommand (cmd);
        handledAny = true;
    }

    if (pendingSetSampleParam.exchange (false, std::memory_order_acq_rel))
    {
        Command cmd;
        cmd.type = CmdSetSampleParam;
        unpackPendingSliceParamPayload (pendingSetSampleParamPayload.load (std::memory_order_relaxed),
                                        cmd.intParam2,
                                        cmd.floatParam1);
        cmd.intParam1 = pendingSetSampleParamSampleId.load (std::memory_order_relaxed);
        handleCommand (cmd);
        handledAny = true;
    }
}

void IntersectProcessor::drainCommands()
{
    bool handledAny = false;

    drainOverflowCommands (handledAny);

    const auto scope = commandFifo.read (commandFifo.getNumReady());

    for (int i = 0; i < scope.blockSize1; ++i)
        handleCommand (commandBuffer[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)
        handleCommand (commandBuffer[(size_t) (scope.startIndex2 + i)]);

    if (scope.blockSize1 + scope.blockSize2 > 0)
        handledAny = true;

    drainCoalescedCommands (handledAny);

    if (pendingEndGesture.exchange (false, std::memory_order_acq_rel))
    {
        gestureSnapshotCaptured = false;
        blocksSinceGestureActivity = 0;
    }

    if (handledAny)
        uiSnapshotDirty.store (true, std::memory_order_release);

    const auto dropped = droppedCommandCount.exchange (0, std::memory_order_relaxed);
    const auto droppedCritical = droppedCriticalCommandCount.exchange (0, std::memory_order_relaxed);
    bool statusChanged = false;
    if (dropped > 0)
    {
        setDroppedCommandWarning (dropped, droppedCritical);
        statusChanged = true;
    }
    else if (handledAny)
    {
        statusChanged = clearDroppedCommandWarning();
    }

    if (statusChanged)
        uiSnapshotDirty.store (true, std::memory_order_release);

    if (handledAny || dropped > 0)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void IntersectProcessor::applyLiveDragBoundsToSlice()
{
    const int liveIdx = liveDragSliceIdx.load (std::memory_order_acquire);
    if (liveIdx >= 0 && liveIdx < sliceManager.getNumSlices())
    {
        const int maxLen = sampleData.getNumFrames();
        if (maxLen > 1)
        {
            auto& s = sliceManager.getSlice (liveIdx);
            int start = liveDragBoundsStart.load (std::memory_order_relaxed);
            int end   = liveDragBoundsEnd.load   (std::memory_order_relaxed);
            start = juce::jlimit (0, juce::jmax (0, maxLen - 1), start);
            end   = juce::jlimit (start + 1, juce::jmax (start + 1, maxLen), end);
            if (end - start < kMidiEditMinSliceLength)
                end = juce::jmin (maxLen, start + kMidiEditMinSliceLength);
            s.startSample = start;
            s.endSample   = end;
            syncSliceOwnershipFromAbsolute (s);
            const int sLen = end - start;
            s.loopStartOffset = juce::jlimit (0, juce::jmax (0, sLen - 1), s.loopStartOffset);
            if (s.loopLength > 0)
                s.loopLength = juce::jlimit (1, juce::jmax (1, sLen - s.loopStartOffset), s.loopLength);
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }
}

UndoManager::Snapshot IntersectProcessor::makeSnapshot()
{
    UndoManager::Snapshot snap;
    const auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap != nullptr)
    {
        snap.numSessionSamples = juce::jmin ((int) sampleSnap->sessionSamples.size(),
                                             SampleData::kMaxSessionSamples);
        for (int i = 0; i < snap.numSessionSamples; ++i)
            snap.sessionSamples[(size_t) i] = sampleSnap->sessionSamples[(size_t) i];
    }
    snap.selectedSessionSampleId = selectedSessionSampleId.load (std::memory_order_relaxed);
    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
        snap.slices[(size_t) i] = sliceManager.getSlice (i);
    snap.numSlices = sliceManager.getNumSlices();
    snap.selectedSlice = sliceManager.selectedSlice;
    snap.rootNote = sliceManager.rootNote.load();

    snap.params = captureParamUndoState();
    snap.sampleParams = sampleParams;
    snap.midiSelectsSlice = midiSelectsSlice.load();
    snap.snapToZeroCrossing = snapToZeroCrossing.load();
    return snap;
}

void IntersectProcessor::captureSnapshot()
{
    undoMgr.push (makeSnapshot());
}

void IntersectProcessor::restoreSnapshot (const UndoManager::Snapshot& snap)
{
    // Audio thread: the reload is started on the message thread (LoadRequest, timerCallback).
    LoadRequest reload;
    reload.updatesPendingStateFile = true;
    for (int i = 0; i < juce::jmin (snap.numSessionSamples, SampleData::kMaxSessionSamples); ++i)
    {
        const auto& sample = snap.sessionSamples[(size_t) i];
        if (sample.filePath.isNotEmpty())
        {
            reload.paths[(size_t) reload.numSamples] = sample.filePath;
            reload.sampleIds[(size_t) reload.numSamples] = sample.sampleId;
            ++reload.numSamples;
        }
    }

    // Apply slice state immediately (safe — no allocation).
    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
        sliceManager.getSlice (i) = snap.slices[(size_t) i];
    sliceManager.setNumSlices (snap.numSlices);
    sliceManager.selectedSlice = snap.selectedSlice;
    sliceManager.rootNote.store (snap.rootNote);
    sampleParams = snap.sampleParams;
    selectedSessionSampleId.store (snap.selectedSessionSampleId, std::memory_order_relaxed);
    midiSelectsSlice.store (snap.midiSelectsSlice);
    snapToZeroCrossing.store (snap.snapToZeroCrossing);

    if (reload.numSamples > 0)
    {
        syncAllSliceAbsolutePositions();
        sliceManager.rebuildMidiMap();
        pendingStateRestoreToken.store (0, std::memory_order_release);
        reload.kind = LoadKindPreserveSlices;
        reload.token = beginSampleLoad (reload.kind);
    }
    else
    {
        clearVoicesBeforeSampleSwap();
        RetiredObjects unloaded;
        unloaded.sample = sampleData.clear();
        retire (std::move (unloaded));
        updateSampleMissingState();
        sliceManager.rebuildMidiMap();
    }
    loadRequests.push (std::move (reload));   // also clears the pending state file when there is no sample

    uiSnapshotDirty.store (true, std::memory_order_release);

    const int writeIndex = pendingParamRestoreIndex.load (std::memory_order_relaxed) == 0 ? 1 : 0;
    pendingParamRestoreStates[(size_t) writeIndex] = snap.params;
    pendingParamRestoreIndex.store (writeIndex, std::memory_order_release);
}

bool IntersectProcessor::applyDeferredParamRestore()
{
    const int pendingIndex = pendingParamRestoreIndex.exchange (-1, std::memory_order_acq_rel);
    if (pendingIndex < 0)
        return false;

    applyParamUndoState (pendingParamRestoreStates[(size_t) pendingIndex]);
    return true;
}

void IntersectProcessor::handleCommand (const Command& cmd)
{
    switch (cmd.type)
    {
        case CmdNone:
        case CmdLazyChopStart:
        case CmdLazyChopStop:
        case CmdFileLoadCompleted:
        case CmdFileLoadFailed:
        case CmdUndo:
        case CmdRedo:
        case CmdPanic:
        case CmdSelectSlice:
        case CmdStemSeparate:
            gestureSnapshotCaptured = false;
            break;

        case CmdBeginGesture:
            captureSnapshot();
            gestureSnapshotCaptured = true;
            blocksSinceGestureActivity = 0;
            break;

        case CmdSetSliceParam:
        case CmdSetSampleParam:
        case CmdSetRootNote:
            if (! gestureSnapshotCaptured)
                captureSnapshot();
            gestureSnapshotCaptured = true;
            blocksSinceGestureActivity = 0;
            break;

        case CmdSetSliceBounds:
        case CmdCreateSlice:
        case CmdDeleteSlice:
        case CmdDeleteSessionSample:
        case CmdStretch:
        case CmdToggleLock:
        case CmdDuplicateSlice:
        case CmdSplitSlice:
        case CmdTransientChop:
        case CmdRepackMidi:
            if (getUiStatusMessage().source == UiStatusMessage::Source::midiLimit)
                clearUiStatusMessage();
            if (! gestureSnapshotCaptured)
                captureSnapshot();
            gestureSnapshotCaptured = false;
            blocksSinceGestureActivity = 0;
            break;
    }

    switch (cmd.type)
    {
        case CmdCreateSlice:
        {
            bool wasAtLimit = sliceManager.nextMidiNote() == kMaxMidiNote
                              && ! sliceManager.midiNoteToSlices (kMaxMidiNote).empty();
            int idx = sliceManager.createSlice (cmd.intParam1, cmd.intParam2);
            if (idx >= 0)
            {
                syncSliceOwnershipFromAbsolute (sliceManager.getSlice (idx));
                if (wasAtLimit)
                    setUiStatusMessage ("MIDI note limit - slice " + juce::String (idx + 1)
                        + " has no unique MIDI note",
                        true, UiStatusMessage::Source::midiLimit);
            }
            break;
        }

        case CmdDeleteSlice:
            sliceManager.deleteSlice (cmd.intParam1);
            break;

        case CmdDeleteSessionSample:
            deleteSessionSample (cmd.intParam1);
            break;

        case CmdLazyChopStart:
            if (sampleData.isLoaded())
            {
                const auto psp = makePreviewStretchParams (selectedSampleParams(), dawBpm.load(), currentSampleRate, &sampleData);
                lazyChop.start (sampleData.getNumFrames(), sliceManager, psp,
                                snapToZeroCrossing.load(), &sampleData.getBuffer());
            }
            break;

        case CmdLazyChopStop:
            lazyChop.stop (voicePool, sliceManager);
            break;

        case CmdStretch:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (sel);
                const double timelineRate = sampleData.getDecodedSampleRate() > 0.0
                    ? sampleData.getDecodedSampleRate()
                    : currentSampleRate;
                float newBpm = GrainEngine::calcStretchBpm (
                    s.startSample, s.endSample, cmd.floatParam1, timelineRate);
                s.bpm = newBpm;
                s.lockMask |= kLockBpm;
            }
            break;
        }

        case CmdSetSampleParam:
            if (auto* params = sampleParams.find (cmd.intParam1))
                params->setField (cmd.intParam2, cmd.floatParam1);
            break;

        case CmdToggleLock:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (sel);
                const auto& sampleLevel = paramsForSample (s.sampleId);
                uint64_t bit = cmd.lockBitParam;
                bool turningOn = !(s.lockMask & bit);

                if (turningOn)
                    SliceEdits::copySampleParam (s, sampleLevel, bit);

                s.lockMask ^= bit;
            }
            break;
        }

        case CmdSetSliceParam:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (sel);
                if (SliceEdits::setField (s, paramsForSample (s.sampleId), cmd.intParam1, cmd.floatParam1))
                    sliceManager.rebuildMidiMap();
            }
            break;
        }

        case CmdSetSliceBounds:
        {
            int idx = cmd.intParam1;
            if (idx >= 0 && idx < sliceManager.getNumSlices())
            {
                const int maxLen = sampleData.getNumFrames();
                if (maxLen <= 1)
                    break;

                auto& s = sliceManager.getSlice (idx);
                int requestedEnd = (cmd.numPositions > 0) ? cmd.positions[0] : (int) cmd.floatParam1;
                int start = juce::jmin (cmd.intParam2, requestedEnd);
                int end = juce::jmax (cmd.intParam2, requestedEnd);
                start = juce::jlimit (0, juce::jmax (0, maxLen - 1), start);
                end = juce::jlimit (start + 1, juce::jmax (start + 1, maxLen), end);
                if (end - start < kMinSliceLengthSamples)
                    end = juce::jmin (maxLen, start + kMinSliceLengthSamples);
                s.startSample = start;
                s.endSample = end;
                syncSliceOwnershipFromAbsolute (s);
                // Clamp loop fields to new slice length
                const int newLen = end - start;
                s.loopStartOffset = juce::jlimit (0, juce::jmax (0, newLen - 1), s.loopStartOffset);
                if (s.loopLength > 0)
                    s.loopLength = juce::jlimit (1, juce::jmax (1, newLen - s.loopStartOffset), s.loopLength);
                sliceManager.rebuildMidiMap();
            }
            break;
        }

        case CmdDuplicateSlice:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                bool wasAtLimit = sliceManager.nextMidiNote() == kMaxMidiNote
                                  && ! sliceManager.midiNoteToSlices (kMaxMidiNote).empty();
                const auto& src = sliceManager.getSlice (sel);
                int newIdx = sliceManager.createSlice (src.startSample, src.endSample);
                if (newIdx >= 0)
                {
                    auto& dst = sliceManager.getSlice (newIdx);
                    int savedNote = dst.midiNote;  // assigned by createSlice
                    dst = src;                     // copy all params, lockMask, colour
                    dst.midiNote      = savedNote; // restore unique MIDI note
                    dst.highNote      = savedNote; // reset to single-note
                    dst.sliceRootNote = savedNote;
                    if (cmd.intParam1 >= 0)        // ctrl-drag: use explicit position
                    {
                        dst.startSample = cmd.intParam1;
                        dst.endSample   = cmd.intParam2;
                        syncSliceOwnershipFromAbsolute (dst);
                        // Clamp loop fields to new slice length
                        const int dLen = dst.endSample - dst.startSample;
                        dst.loopStartOffset = juce::jlimit (0, juce::jmax (0, dLen - 1), dst.loopStartOffset);
                        if (dst.loopLength > 0)
                            dst.loopLength = juce::jlimit (1, juce::jmax (1, dLen - dst.loopStartOffset), dst.loopLength);
                    }
                    else
                    {
                        dst.sampleId = src.sampleId;
                        dst.startInSample = src.startInSample;
                        dst.endInSample = src.endInSample;
                    }
                    // else (intParam1 == -1): inherit src.startSample/endSample as-is
                    sliceManager.selectedSlice = newIdx;
                    selectedSessionSampleId.store (dst.sampleId, std::memory_order_relaxed);
                    if (wasAtLimit)
                        setUiStatusMessage ("MIDI note limit - slice " + juce::String (newIdx + 1)
                            + " has no unique MIDI note",
                            true, UiStatusMessage::Source::midiLimit);
                }
            }
            break;
        }

        case CmdSplitSlice:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                Slice srcCopy = sliceManager.getSlice (sel);
                int startS = srcCopy.startSample;
                int endS = srcCopy.endSample;
                int count = juce::jlimit (2, 128, cmd.intParam1);
                int len = endS - startS;
                // Notes for sub-slices start one past the highest note any existing
                // slice has, so no existing note is disturbed.
                int baseNote = sliceManager.nextMidiNote();

                sliceManager.deleteSlice (sel);

                bool doSnap = snapToZeroCrossing.load() && sampleData.isLoaded();
                int firstNew = -1;
                for (int i = 0; i < count; ++i)
                {
                    int s = startS + i * len / count;
                    int e = startS + (i + 1) * len / count;
                    if (doSnap)
                    {
                        if (i > 0)
                            s = AudioAnalysis::findNearestZeroCrossing (sampleData.getBuffer(), s);
                        if (i < count - 1)
                            e = AudioAnalysis::findNearestZeroCrossing (sampleData.getBuffer(), e);
                    }
                    if (e - s < kMinSliceLengthSamples) e = s + kMinSliceLengthSamples;
                    int idx = sliceManager.createSlice (s, e);
                    if (idx >= 0)
                    {
                        auto& dst = sliceManager.getSlice (idx);
                        juce::Colour savedColour = dst.colour;
                        dst = srcCopy;
                        dst.startSample = s;
                        dst.endSample   = e;
                        syncSliceOwnershipFromAbsolute (dst);
                        dst.midiNote      = juce::jlimit (0, kMaxMidiNote, baseNote + i);
                        dst.highNote      = dst.midiNote;
                        dst.sliceRootNote = dst.midiNote;
                        dst.colour      = savedColour;
                        dst.active      = true;
                        dst.loopStartOffset = 0;
                        dst.loopLength      = 0;
                    }
                    if (i == 0) firstNew = idx;
                }

                sliceManager.rebuildMidiMap();
                if (firstNew >= 0)
                {
                    sliceManager.selectedSlice = firstNew;
                    selectedSessionSampleId.store (sliceManager.getSlice (firstNew).sampleId, std::memory_order_relaxed);
                }
                if (baseNote + count - 1 > kMaxMidiNote)
                {
                    int firstOverflow = firstNew + (kMaxMidiNote - baseNote + 1);
                    setUiStatusMessage ("MIDI note limit - slices " + juce::String (firstOverflow + 1)
                        + "+ have no unique MIDI note",
                        true, UiStatusMessage::Source::midiLimit);
                }
            }
            break;
        }

        case CmdTransientChop:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices() && cmd.numPositions > 0)
            {
                Slice srcCopy = sliceManager.getSlice (sel);
                int startS = srcCopy.startSample;
                int endS = srcCopy.endSample;
                int baseNote = sliceManager.nextMidiNote();

                // Build fixed-size boundary list: [startS, ...positions..., endS]
                int bounds[SliceManager::kMaxSlices + 2];
                int numBounds = 0;
                bounds[numBounds++] = startS;
                for (int bi = 0; bi < cmd.numPositions; ++bi)
                    bounds[numBounds++] = cmd.positions[(size_t) bi];
                bounds[numBounds++] = endS;

                sliceManager.deleteSlice (sel);

                int firstNew = -1;
                int subIdx = 0;
                for (int i = 0; i + 1 < numBounds; ++i)
                {
                    int s = bounds[i];
                    int e = bounds[i + 1];
                    if (e - s < kMinSliceLengthSamples) continue;
                    int idx = sliceManager.createSlice (s, e);
                    if (idx >= 0)
                    {
                        auto& dst = sliceManager.getSlice (idx);
                        juce::Colour savedColour = dst.colour;
                        dst = srcCopy;
                        dst.startSample = s;
                        dst.endSample   = e;
                        syncSliceOwnershipFromAbsolute (dst);
                        dst.midiNote      = juce::jlimit (0, kMaxMidiNote, baseNote + subIdx);
                        dst.highNote      = dst.midiNote;
                        dst.sliceRootNote = dst.midiNote;
                        dst.colour      = savedColour;
                        dst.active      = true;
                        dst.loopStartOffset = 0;
                        dst.loopLength      = 0;
                    }
                    if (firstNew < 0) firstNew = idx;
                    ++subIdx;
                }

                sliceManager.rebuildMidiMap();
                if (firstNew >= 0)
                {
                    sliceManager.selectedSlice = firstNew;
                    selectedSessionSampleId.store (sliceManager.getSlice (firstNew).sampleId, std::memory_order_relaxed);
                }
                if (baseNote + subIdx - 1 > kMaxMidiNote)
                {
                    int firstOverflow = firstNew + (kMaxMidiNote - baseNote + 1);
                    setUiStatusMessage ("MIDI note limit - slices " + juce::String (firstOverflow + 1)
                        + "+ have no unique MIDI note",
                        true, UiStatusMessage::Source::midiLimit);
                }
            }
            break;
        }

        case CmdRepackMidi:
        {
            int overflowAt = sliceManager.repackMidiNotes (cmd.intParam1 != 0);
            if (overflowAt >= 0)
                setUiStatusMessage ("MIDI note limit - slices " + juce::String (overflowAt + 1)
                    + "+ were not resequenced",
                    true, UiStatusMessage::Source::midiLimit);
            break;
        }

        case CmdFileLoadCompleted:
            jassertfalse; // Legacy path no longer used; completions arrive via completedLoadSuccess.
            break;

        case CmdFileLoadFailed:
            jassertfalse; // Legacy path no longer used; failures arrive via completedLoadFailure.
            break;

        case CmdUndo:
            if (undoMgr.canUndo())
                restoreSnapshot (undoMgr.undo (makeSnapshot()));
            break;

        case CmdRedo:
            if (undoMgr.canRedo())
                restoreSnapshot (undoMgr.redo());
            break;

        case CmdBeginGesture:
            break;

        case CmdPanic:
            voicePool.killAll();
            auditionPlayer.stopFromAudioThread();
            lazyChop.stop (voicePool, sliceManager);
            std::fill (std::begin (heldNotes), std::end (heldNotes), false);
            break;

        case CmdSelectSlice:
        {
            const int selected = juce::jlimit (-1, juce::jmax (-1, sliceManager.getNumSlices() - 1), cmd.intParam1);
            if (selected >= 0 && sliceManager.getSlice (selected).parked)
                break;
            sliceManager.selectedSlice.store (selected, std::memory_order_relaxed);
            if (selected >= 0 && selected < sliceManager.getNumSlices())
                selectedSessionSampleId.store (sliceManager.getSlice (selected).sampleId, std::memory_order_relaxed);
            break;
        }

        case CmdSetRootNote:
            sliceManager.rootNote.store (juce::jlimit (0, kMaxMidiNote, cmd.intParam1),
                                         std::memory_order_relaxed);
            break;

        case CmdStemSeparate:
            // Launched from message thread via startStemSeparation(); nothing to do here.
            break;

        case CmdNone:
            break;
    }
}

std::optional<IntersectProcessor::MidiEditEvent> IntersectProcessor::tryParseMidiEditMessage (
    const juce::MidiMessage& msg)
{
    if (! msg.isController())
        return std::nullopt;

    const int channelIndex = msg.getChannel() - 1;
    if (channelIndex < 0 || channelIndex >= 16)
        return std::nullopt;

    const int cc  = msg.getControllerNumber();
    const int val = msg.getControllerValue();

    if (cc == kNrpnCcMsb)
    {
        midiEditParser.nrpnMsb[(size_t) channelIndex] = (uint8_t) val;
        return std::nullopt;
    }

    if (cc == kNrpnCcLsb)
    {
        midiEditParser.nrpnLsb[(size_t) channelIndex] = (uint8_t) val;
        return std::nullopt;
    }

    if (cc != kNrpnCcIncr && cc != kNrpnCcDecr)
        return std::nullopt;

    const int nrpnNum = midiEditParser.nrpnMsb[(size_t) channelIndex] * 128
                      + midiEditParser.nrpnLsb[(size_t) channelIndex];

    MidiEditAction action = MidiEditAction::none;
    if (nrpnNum == kMidiEditNrpnZoom)        action = MidiEditAction::zoom;
    else if (nrpnNum == kMidiEditNrpnSliceStart) action = MidiEditAction::sliceStart;
    else if (nrpnNum == kMidiEditNrpnSliceEnd)   action = MidiEditAction::sliceEnd;

    if (action == MidiEditAction::none)
        return std::nullopt;

    MidiEditEvent event;
    event.action = action;
    event.steps  = (cc == kNrpnCcIncr) ? 1 : -1;
    return event;
}

void IntersectProcessor::applyMidiEditZoomSteps (int steps)
{
    if (steps == 0)
        return;

    const int numFrames = sampleData.getNumFrames();
    if (numFrames <= 0)
        return;

    auto getViewLen = [&] (float currentZoom) -> int
    {
        return juce::jlimit (1, numFrames, (int) ((float) numFrames / std::max (1.0f, currentZoom)));
    };

    const float currentZoom = zoom.load (std::memory_order_relaxed);
    const int currentViewLen = getViewLen (currentZoom);
    const int currentMaxStart = std::max (0, numFrames - currentViewLen);
    const float currentScroll = scroll.load (std::memory_order_relaxed);
    const int currentViewStart = (currentMaxStart > 0)
                               ? (int) (currentScroll * (float) currentMaxStart)
                               : 0;

    int anchorSample = currentViewStart + currentViewLen / 2;
    if (midiEditState.previewActive && midiEditState.activeGestureSlice >= 0)
    {
        anchorSample = midiEditState.activeBoundaryIsStart
                     ? liveDragBoundsStart.load (std::memory_order_relaxed)
                     : liveDragBoundsEnd.load   (std::memory_order_relaxed);
    }

    const float newZoom = juce::jlimit (1.0f, kMidiEditZoomClampMax,
        currentZoom * std::pow (2.0f, (float) steps / (float) kMidiEditZoomStepsPerOctave));
    const int newViewLen = getViewLen (newZoom);
    const int newMaxStart = std::max (0, numFrames - newViewLen);
    const int desiredStart = juce::jlimit (0, newMaxStart, anchorSample - newViewLen / 2);
    const float newScroll = (newMaxStart > 0) ? (float) desiredStart / (float) newMaxStart : 0.0f;

    zoom.store (newZoom, std::memory_order_relaxed);
    scroll.store (newScroll, std::memory_order_relaxed);
}

void IntersectProcessor::setMidiBoundaryPreviewState (int sliceIdx, int startSample, int endSample,
                                                      bool isStart)
{
    midiBoundaryPreviewSliceIdx.store (sliceIdx, std::memory_order_relaxed);
    midiBoundaryPreviewStart.store (startSample, std::memory_order_relaxed);
    midiBoundaryPreviewEnd.store (endSample, std::memory_order_relaxed);
    midiBoundaryPreviewEditedEdge.store (isStart ? 1 : 2, std::memory_order_relaxed);
    midiBoundaryPreviewActive.store (true, std::memory_order_release);
}

bool IntersectProcessor::beginMidiSliceBoundaryGestureIfNeeded (int sliceIdx, bool isStart)
{
    if (sliceIdx < 0 || sliceIdx >= sliceManager.getNumSlices())
        return false;

    if (midiEditState.gestureOpen)
    {
        midiEditState.activeBoundaryIsStart = isStart;
        return midiEditState.activeGestureSlice == sliceIdx;
    }

    int expectedOwner = 0;
    if (! liveDragOwner.compare_exchange_strong (expectedOwner, 2,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire)
        && expectedOwner != 2)
    {
        return false;
    }

    const auto& slice = sliceManager.getSlice (sliceIdx);
    liveDragBoundsStart.store (slice.startSample, std::memory_order_relaxed);
    liveDragBoundsEnd.store   (slice.endSample,   std::memory_order_relaxed);
    liveDragSliceIdx.store    (sliceIdx,          std::memory_order_release);

    Command gestureCmd;
    gestureCmd.type = CmdBeginGesture;
    handleCommand (gestureCmd);

    midiEditState.gestureOpen = true;
    midiEditState.previewActive = true;
    midiEditState.activeGestureSlice = sliceIdx;
    midiEditState.gestureIdleSamples = 0;
    midiEditState.activeBoundaryIsStart = isStart;
    setMidiBoundaryPreviewState (sliceIdx, slice.startSample, slice.endSample, isStart);
    uiSnapshotDirty.store (true, std::memory_order_release);
    return true;
}

void IntersectProcessor::applyMidiSliceBoundarySteps (int sliceIdx, bool isStart, int steps)
{
    if (steps == 0 || ! beginMidiSliceBoundaryGestureIfNeeded (sliceIdx, isStart))
        return;

    const int numFrames = sampleData.getNumFrames();
    if (numFrames <= 0)
        return;

    auto getViewLen = [&]() -> int
    {
        const float currentZoom = zoom.load (std::memory_order_relaxed);
        return juce::jlimit (1, numFrames, (int) ((float) numFrames / std::max (1.0f, currentZoom)));
    };

    auto centreBoundaryInView = [&] (int samplePos)
    {
        const int viewLen = getViewLen();
        const int maxStart = std::max (0, numFrames - viewLen);
        const int desiredStart = juce::jlimit (0, maxStart, samplePos - viewLen / 2);
        const float newScroll = (maxStart > 0) ? (float) desiredStart / (float) maxStart : 0.0f;
        scroll.store (newScroll, std::memory_order_relaxed);
    };

    const int samplesPerStep = std::max (1, getViewLen() / kMidiEditStepsPerView);
    const int sampleDelta = steps * samplesPerStep;
    const int currentStart = liveDragBoundsStart.load (std::memory_order_relaxed);
    const int currentEnd   = liveDragBoundsEnd.load   (std::memory_order_relaxed);

    int boundaryPos = isStart ? currentStart : currentEnd;
    if (isStart)
    {
        boundaryPos = juce::jlimit (0, std::max (0, currentEnd - kMidiEditMinSliceLength),
                                    currentStart + sampleDelta);
    }
    else
    {
        boundaryPos = juce::jlimit (std::min (numFrames, currentStart + kMidiEditMinSliceLength),
                                    numFrames, currentEnd + sampleDelta);
    }

    if (snapToZeroCrossing.load() && sampleData.isLoaded())
        boundaryPos = AudioAnalysis::findNearestZeroCrossing (sampleData.getBuffer(), boundaryPos);

    if (isStart)
        liveDragBoundsStart.store (boundaryPos, std::memory_order_relaxed);
    else
        liveDragBoundsEnd.store (boundaryPos, std::memory_order_relaxed);

    midiEditState.activeBoundaryIsStart = isStart;
    midiEditState.gestureIdleSamples = 0;
    setMidiBoundaryPreviewState (sliceIdx,
                                 liveDragBoundsStart.load (std::memory_order_relaxed),
                                 liveDragBoundsEnd.load (std::memory_order_relaxed),
                                 isStart);
    applyLiveDragBoundsToSlice();
    uiSnapshotDirty.store (true, std::memory_order_release);
    centreBoundaryInView (boundaryPos);
}

void IntersectProcessor::handleMidiEditEvent (const MidiEditEvent& event)
{
    switch (event.action)
    {
        case MidiEditAction::zoom:
            applyMidiEditZoomSteps (event.steps);
            break;

        case MidiEditAction::sliceStart:
        case MidiEditAction::sliceEnd:
        {
            const int sliceIdx = midiEditState.gestureOpen
                               ? midiEditState.activeGestureSlice
                               : sliceManager.selectedSlice.load (std::memory_order_relaxed);
            applyMidiSliceBoundarySteps (sliceIdx, event.action == MidiEditAction::sliceStart, event.steps);
            break;
        }

        case MidiEditAction::none:
            break;
    }
}

void IntersectProcessor::commitMidiSliceBoundaryGestureIfIdle (int blockSamples)
{
    if (! midiEditState.gestureOpen || ! midiEditState.previewActive)
        return;

    midiEditState.gestureIdleSamples += blockSamples;
    const int idleThreshold = (int) (currentSampleRate * kMidiEditGestureIdleSeconds);
    if (midiEditState.gestureIdleSamples <= idleThreshold)
        return;

    const int sliceIdx = midiEditState.activeGestureSlice;
    if (sliceIdx >= 0 && sliceIdx < sliceManager.getNumSlices())
    {
        Command cmd;
        cmd.type         = CmdSetSliceBounds;
        cmd.intParam1    = sliceIdx;
        cmd.intParam2    = liveDragBoundsStart.load (std::memory_order_relaxed);
        cmd.positions[0] = liveDragBoundsEnd.load   (std::memory_order_relaxed);
        cmd.numPositions = 1;
        handleCommand (cmd);
    }
    else
    {
        gestureSnapshotCaptured = false;
    }

    clearMidiEditGestureState();
}

void IntersectProcessor::clearMidiEditGestureState()
{
    liveDragSliceIdx.store (-1, std::memory_order_release);
    liveDragOwner.store    (0,  std::memory_order_release);
    midiBoundaryPreviewSliceIdx.store (-1, std::memory_order_relaxed);
    midiBoundaryPreviewStart.store (0, std::memory_order_relaxed);
    midiBoundaryPreviewEnd.store (0, std::memory_order_relaxed);
    midiBoundaryPreviewEditedEdge.store (0, std::memory_order_relaxed);
    midiBoundaryPreviewActive.store (false, std::memory_order_release);
    midiEditState.activeGestureSlice = -1;
    midiEditState.gestureIdleSamples = 0;
    midiEditState.previewActive = false;
    midiEditState.gestureOpen = false;
    midiEditState.activeBoundaryIsStart = true;
}

void IntersectProcessor::processMidiEvent (const juce::MidiMessage& msg,
                                           const MidiEditBlockSettings& edit)
{
    if (edit.enabled && msg.isController()
        && (edit.channel == 0 || msg.getChannel() == edit.channel))
    {
        if (auto midiEditEvent = tryParseMidiEditMessage (msg))
            handleMidiEditEvent (*midiEditEvent);
    }

    if (msg.isNoteOn())
    {
        int note = msg.getNoteNumber();
        float velocity = (float) msg.getVelocity();

        if (lazyChop.isActive())
        {
            // Any MIDI note places a chop boundary at the playhead
            const int slicesBefore = sliceManager.getNumSlices();
            int newSliceIdx = lazyChop.onNote (note, voicePool, sliceManager);

            // One note can close a wrapped tail and a new head; bind every new
            // slice to the session sample it was cut from.
            for (int i = slicesBefore; i < sliceManager.getNumSlices(); ++i)
                syncSliceOwnershipFromAbsolute (sliceManager.getSlice (i));
            if (newSliceIdx >= 0)
            {
                sliceManager.selectedSlice.store (newSliceIdx, std::memory_order_relaxed);
                uiSnapshotDirty.store (true, std::memory_order_release);
            }
        }
        else
        {
            const auto noteIndex = static_cast<size_t> (note);
            heldNotes[noteIndex] = true;

            const int rootNote = sliceManager.rootNote.load();
            const float hostBpm = dawBpm.load();

            const auto& sliceIndices = sliceManager.midiNoteToSlices (note);
            for (int sliceIdx : sliceIndices)
            {
                if (! juce::isPositiveAndBelow (sliceIdx, sliceManager.getNumSlices()))
                    continue;

                if (midiSelectsSlice.load (std::memory_order_relaxed))
                {
                    const int previous = sliceManager.selectedSlice.load (std::memory_order_relaxed);
                    sliceManager.selectedSlice.store (sliceIdx, std::memory_order_relaxed);
                    selectedSessionSampleId.store (sliceManager.getSlice (sliceIdx).sampleId, std::memory_order_relaxed);
                    if (previous != sliceIdx)
                        uiSnapshotDirty.store (true, std::memory_order_release);
                }

                int voiceIdx = voicePool.allocate();

                // Each slice inherits from its own session sample's parameters.
                const auto& s = sliceManager.getSlice (sliceIdx);
                auto p = makeVoiceStartParams (paramsForSample (s.sampleId), rootNote, note, velocity, hostBpm);

                // Handle mute groups
                int mg = (int) sliceManager.resolveParam (sliceIdx, kLockMuteGroup,
                                                          (float) s.muteGroup, (float) p.sampleMuteGroup);
                voicePool.muteGroup (mg, voiceIdx);

                p.sliceIdx = sliceIdx;
                p.sliceRootNote = s.sliceRootNote;
                voicePool.startVoice (voiceIdx, p, sliceManager, sampleData);
            }
        }
    }
    else if (msg.isNoteOff())
    {
        int note = msg.getNoteNumber();
        const auto noteIndex = static_cast<size_t> (note);
        if (heldNotes[noteIndex])
        {
            heldNotes[noteIndex] = false;
            voicePool.releaseNote (note);           // normal: respects oneShot
        }
        else
        {
            voicePool.releaseNoteForced (note);     // host sweep: kills even oneShot voices
        }
    }
    else if (msg.isAllNotesOff())
    {
        voicePool.releaseAll();  // 50ms fade on all active voices
        lazyChop.stop (voicePool, sliceManager);
        std::fill (std::begin (heldNotes), std::end (heldNotes), false);
    }
    else if (msg.isAllSoundOff())
    {
        voicePool.killAll();     // 5ms hard kill on all active voices
        lazyChop.stop (voicePool, sliceManager);
        std::fill (std::begin (heldNotes), std::end (heldNotes), false);
    }
}

void IntersectProcessor::filterConsumedMidiEditEvents (juce::MidiBuffer& midi,
                                                       const MidiEditBlockSettings& edit)
{
    // Strip MIDI edit CCs from the buffer so they don't pass downstream
    if (! (edit.enabled && edit.consumeCc))
        return;

    juce::MidiBuffer filtered;
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int cc = msg.getControllerNumber();
        const bool strip = msg.isController()
            && (edit.channel == 0 || msg.getChannel() == edit.channel)
            && (cc == kNrpnCcMsb || cc == kNrpnCcLsb || cc == kNrpnCcIncr || cc == kNrpnCcDecr);
        if (! strip)
            filtered.addEvent (msg, metadata.samplePosition);
    }
    midi = std::move (filtered);
}

static inline float sanitiseSample (float x)
{
#if JUCE_DEBUG
    if (std::isfinite (x))
        jassert (std::abs (x) < 10.0f);
#endif

    if (! std::isfinite (x)) return 0.0f;
    return juce::jlimit (-1.0f, 1.0f, x);
}

void IntersectProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi) INTERSECT_NONBLOCKING
{
    juce::ScopedNoDenormals noDenormals;
    lastProcessBlockMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    buffer.clear();

    // Standalone has no host transport, so its shell provides the tempo directly.
    if (wrapperType == wrapperType_Standalone)
    {
        dawBpm.store (standaloneTransportBpm.load (std::memory_order_relaxed), std::memory_order_relaxed);
    }
    else if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
            if (auto bpmOpt = pos->getBpm())
                dawBpm.store ((float) *bpmOpt, std::memory_order_relaxed);
    }

    // Poll shift preview request (atomic, avoids FIFO latency)
    {
        int req = shiftPreviewRequest.exchange (-2, std::memory_order_relaxed);
        if (req == -1)
            voicePool.stopShiftPreview();
        else if (req >= 0 && ! lazyChop.isActive() && sampleData.isLoaded())
        {
            const auto psp = makePreviewStretchParams (selectedSampleParams(), dawBpm.load(), currentSampleRate, &sampleData);
            voicePool.startShiftPreview (req, sampleData.getNumFrames(), psp);
        }
    }

    bool loadStateChanged = false;
    {
        auto* rawLoad = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
        if (rawLoad != nullptr)
        {
            std::unique_ptr<CompletedLoad> load (rawLoad);
            const auto& decoded = load->sample;
            const int currentToken = latestLoadToken.load (std::memory_order_acquire);
            const auto currentLoadKind = load->kind;
            const bool isStateRestoreLoad = pendingStateRestoreToken.load (std::memory_order_acquire) == currentToken;

            const bool needsSampleRateRetry = decoded->filePath.isNotEmpty()
                && currentSampleRate > 0.0
                && decoded->decodedSampleRate > 0.0
                && std::abs (decoded->decodedSampleRate - currentSampleRate) > 0.01;

            if (load->token != currentToken)
            {
                // A newer load started after this one finished decoding: drop it.
            }
            else if (needsSampleRateRetry)
            {
                // Decode again at the current rate; the message thread starts it (timerCallback).
                LoadRequest retry;
                retry.kind = currentLoadKind;
                for (const auto& sample : decoded->sessionSamples)
                {
                    if (retry.numSamples == SampleData::kMaxSessionSamples)
                        break;
                    retry.paths[(size_t) retry.numSamples] = sample.filePath;
                    retry.sampleIds[(size_t) retry.numSamples] = sample.sampleId;
                    ++retry.numSamples;
                }
                for (const int id : decoded->importSliceSampleIds)
                    if (retry.numImportIds < SampleData::kMaxSessionSamples)
                        retry.importIds[(size_t) retry.numImportIds++] = id;
                retry.token = beginSampleLoad (currentLoadKind);
                if (isStateRestoreLoad)
                    pendingStateRestoreToken.store (retry.token, std::memory_order_release);
                loadRequests.push (std::move (retry));
            }
            else
            {
                clearVoicesBeforeSampleSwap();
                RetiredObjects replaced;
                replaced.sample = sampleData.applyDecodedSample (std::move (load->sample));
                retire (std::move (replaced));
                // New session samples start at factory defaults; removed ones lose their entry.
                sampleParams.syncToSession (sampleData.getSessionSamples());
                updateSampleMissingState();
                if (sampleData.getNumSessionSamples() > 0)
                {
                    const auto& sessionSamples = sampleData.getSessionSamples();
                    int maxSampleId = 0;
                    for (const auto& sample : sessionSamples)
                        maxSampleId = juce::jmax (maxSampleId, sample.sampleId);
                    nextSessionSampleId.store (juce::jmax (nextSessionSampleId.load (std::memory_order_relaxed), maxSampleId),
                                               std::memory_order_relaxed);
                    const int currentSelectedSampleId = selectedSessionSampleId.load (std::memory_order_relaxed);
                    const bool stillPresent = std::any_of (sessionSamples.begin(), sessionSamples.end(),
                                                           [currentSelectedSampleId] (const auto& sample)
                                                           {
                                                               return sample.sampleId == currentSelectedSampleId;
                                                           });
                    if (! stillPresent)
                        selectedSessionSampleId.store (sessionSamples.front().sampleId, std::memory_order_relaxed);
                }
                else
                {
                    selectedSessionSampleId.store (-1, std::memory_order_relaxed);
                }

                // Apply pending stem metadata to newly imported session samples
                {
                    std::unique_ptr<PendingStemImport> pending (pendingStemImport.exchange (nullptr, std::memory_order_acq_rel));
                    if (pending != nullptr)
                    {
                        const auto& sessionSamples = sampleData.getSessionSamples();
                        const auto numNewStems = (int) pending->metas.size();
                        const int totalSamples = (int) sessionSamples.size();
                        const int firstStemIdx = totalSamples - numNewStems;
                        for (int i = 0; i < numNewStems && firstStemIdx + i < totalSamples; ++i)
                        {
                            const auto& meta = pending->metas[(size_t) i];
                            const int stemId = sessionSamples[(size_t) (firstStemIdx + i)].sampleId;
                            setStemMeta (stemId, meta);
                            // Stems play the same material as their source, so they start with its
                            // sample-level settings (BPM, pitch, …) instead of factory defaults.
                            const auto sourceParams = paramsForSample (pending->parentSourceSampleId);
                            sampleParams.set (stemId, sourceParams);
                        }
                        RetiredObjects applied;
                        applied.stemImport = std::move (pending);
                        retire (std::move (applied));
                    }
                }

                if (! isStateRestoreLoad
                    && currentLoadKind == LoadKindReplace)
                    sliceManager.clearAll();
                else
                {
                    applyPendingSliceTimelineRemap();
                    syncAllSliceAbsolutePositions();
                    clampSlicesToSampleBounds();
                    deselectParkedSlice();
                    sliceManager.rebuildMidiMap();
                }

                // REX2 import: create the slices embedded in freshly loaded loops. The decoder
                // only fills this list for files the load request marked for import, so
                // reloads (append, reorder, undo, state restore) never duplicate slices.
                // Each freshly imported loop's tempo becomes that sample's own BPM (stretch untouched).
                {
                    const auto& importedSlices = sampleData.getImportedSlices();
                    for (const auto& sample : sampleData.getSessionSamples())
                    {
                        if (sample.anchorTempoBpm <= 0.0f)
                            continue;
                        const bool imported = std::any_of (importedSlices.begin(), importedSlices.end(),
                                                           [id = sample.sampleId] (const auto& imp) { return imp.sampleId == id; });
                        if (auto* params = imported ? sampleParams.find (sample.sampleId) : nullptr)
                            params->setField (FieldBpm, sample.anchorTempoBpm);
                    }

                    int created = 0;
                    for (const auto& imp : importedSlices)
                    {
                        const int idx = sliceManager.createSlice (imp.startSample, imp.endSample);
                        if (idx < 0)
                            break;   // kMaxSlices reached
                        syncSliceOwnershipFromAbsolute (sliceManager.getSlice (idx));
                        ++created;
                    }

                    if (created > 0)
                        sliceManager.rebuildMidiMap();
                    if (created < (int) importedSlices.size())
                        setUiStatusMessage ("REX file has " + juce::String ((int) importedSlices.size())
                                                + " slices - only " + juce::String (created) + " imported",
                                            true);
                }

                if (isStateRestoreLoad)
                    pendingStateRestoreToken.store (0, std::memory_order_release);

                loadStateChanged = true;
                uiSnapshotDirty.store (true, std::memory_order_release);
            }

            // The hand-off (and the decoded sample, after a sample-rate retry) is freed off the audio thread.
            RetiredObjects done;
            done.load = std::move (load);
            retire (std::move (done));
        }
    }

    {
        auto* rawFailure = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
        if (rawFailure != nullptr)
        {
            std::unique_ptr<FailedLoadResult> failed (rawFailure);
            const bool isStateRestoreLoad = pendingStateRestoreToken.load (std::memory_order_acquire) == failed->token;
            // Unreadable files become placeholders, so a whole load only fails when the
            // decoder had nothing to work with; leave the current kit as it is.
            if (failed->token == latestLoadToken.load (std::memory_order_acquire))
            {
                if (isStateRestoreLoad)
                    pendingStateRestoreToken.store (0, std::memory_order_release);

                loadStateChanged = true;
                uiSnapshotDirty.store (true, std::memory_order_release);
            }

            RetiredObjects done;
            done.failure = std::move (failed);
            retire (std::move (done));
        }
    }

    {
        const auto& status = getUiStatusMessage();
        if ((status.isWarning || status.source == UiStatusMessage::Source::transientInfo)
            && status.shownAtMs > 0
            && juce::Time::currentTimeMillis() - status.shownAtMs >= 5000)
        {
            clearUiStatusMessage();
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }

    if (loadStateChanged)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));

    // Drain UI-thread undo snapshots before command handling
    {
        const auto uiScope = uiUndoFifo.read (uiUndoFifo.getNumReady());
        for (int i = 0; i < uiScope.blockSize1; ++i)
            undoMgr.push (uiUndoBuffer[(size_t) (uiScope.startIndex1 + i)]);
        for (int i = 0; i < uiScope.blockSize2; ++i)
            undoMgr.push (uiUndoBuffer[(size_t) (uiScope.startIndex2 + i)]);
    }

    drainCommands();
    applyLiveDragBoundsToSlice();

    // Update max active voices from param
    voicePool.setMaxActiveVoices ((int) maxVoicesParam->load());

    // Collect write pointers for all enabled output buses
    std::array<float*, kMaxOutputBuses> busL {};
    std::array<float*, kMaxOutputBuses> busR {};
    int numActiveBuses = 0;

    for (int b = 0; b < std::min (getBusCount (false), kMaxOutputBuses); ++b)
    {
        auto* bus = getBus (false, b);
        if (bus != nullptr && bus->isEnabled())
        {
            const auto busIndex = static_cast<size_t> (b);
            int chOff = getChannelIndexInProcessBlockBuffer (false, b, 0);
            if (chOff < buffer.getNumChannels())
            {
                busL[busIndex] = buffer.getWritePointer (chOff);
                busR[busIndex] = (chOff + 1 < buffer.getNumChannels())
                              ? buffer.getWritePointer (chOff + 1) : nullptr;
                if (b + 1 > numActiveBuses) numActiveBuses = b + 1;
            }
        }
    }

    const int numSamples = buffer.getNumSamples();

    const bool canRender = sampleData.isLoaded();
    if (! canRender && ! sampleMissing.load (std::memory_order_relaxed))
        sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);

    // Event-boundary timeline (issue #40): render audio up to each MIDI event's sample
    // position, apply the event, then continue, so note-ons/offs and mute-group cuts land
    // sample-accurately instead of snapping to the block start. MIDI bookkeeping still runs
    // with no sample loaded; only the audio ranges are skipped.
    auto renderAudioRange = [&] (int rangeStart, int rangeLength)
    {
        if (! canRender || rangeLength <= 0)
            return;
        if (numActiveBuses <= 1)
            voicePool.renderMainBusRange (sampleData, busL[0], busR[0], rangeStart, rangeLength);
        else
            voicePool.renderRoutedRange (sampleData, busL.data(), busR.data(), numActiveBuses,
                                         rangeStart, rangeLength);
    };

    const MidiEditBlockSettings midiEdit {
        midiEditState.enabled.load (std::memory_order_acquire),
        midiEditState.channel.load (std::memory_order_relaxed),
        midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed) };

    int renderCursor = 0;
    for (const auto metadata : midi)
    {
        // Hosts must supply ordered, in-range event positions; clamp defensively in release
        jassert (metadata.samplePosition >= renderCursor && metadata.samplePosition <= numSamples);
        const int eventPos = juce::jlimit (renderCursor, numSamples, metadata.samplePosition);
        renderAudioRange (renderCursor, eventPos - renderCursor);
        processMidiEvent (metadata.getMessage(), midiEdit);
        renderCursor = eventPos;
    }
    renderAudioRange (renderCursor, numSamples - renderCursor);

    filterConsumedMidiEditEvents (midi, midiEdit);

    if (midiEditState.gestureOpen && midiEditState.previewActive)
    {
        blocksSinceGestureActivity = 0;
        commitMidiSliceBoundaryGestureIfIdle (buffer.getNumSamples());
    }
    else if (gestureSnapshotCaptured)
    {
        ++blocksSinceGestureActivity;
        const int gestureTimeoutBlocks =
            juce::jmax (3, (int) std::ceil (0.5 * currentSampleRate
                                            / juce::jmax (1, buffer.getNumSamples())));
        if (blocksSinceGestureActivity > gestureTimeoutBlocks)
            gestureSnapshotCaptured = false;
    }

    // A finished stem separation is picked up by timerCallback, not here.
    const auto stemState = stemJob.getState();
    if (stemState != StemJobState::idle
        || stemModelDownloadJob.getState() == StemModelDownloadState::downloading)
        uiSnapshotDirty.store (true, std::memory_order_release);

    if (uiSnapshotDirty.exchange (false, std::memory_order_acq_rel))
        publishUiSliceSnapshot();

    // Browser audition plays on the main bus, even when no sample is loaded.
    const bool auditioned = auditionPlayer.renderAdd (busL[0], busR[0], numSamples);

    if (! canRender && ! auditioned)
        return;

    // Sanitise once after all ranges have been mixed: clamp / NaN-guard every active bus
    // (bus 0 is the only populated slot on the single-stereo fast path)
    const int busesToSanitise = juce::jmax (1, numActiveBuses);
    for (int b = 0; b < busesToSanitise; ++b)
    {
        const auto busIndex = static_cast<size_t> (b);
        if (busL[busIndex])
            for (int i = 0; i < numSamples; ++i)
                busL[busIndex][i] = sanitiseSample (busL[busIndex][i]);
        if (busR[busIndex])
            for (int i = 0; i < numSamples; ++i)
                busR[busIndex][i] = sanitiseSample (busR[busIndex][i]);
    }

    // Pass through MIDI
    // (already in the buffer, no action needed)
}

juce::AudioProcessorEditor* IntersectProcessor::createEditor()
{
    return new IntersectEditor (*this);
}

SessionState IntersectProcessor::captureSessionState()
{
    SessionState state;
    state.version = kCurrentStateVersion;

    std::unique_ptr<juce::XmlElement> xml (apvts.copyState().createXml());
    state.apvtsXml = xml->toString();

    state.zoom = zoom.load();
    state.scroll = scroll.load();
    state.selectedSlice = sliceManager.selectedSlice;
    state.midiSelectsSlice = midiSelectsSlice.load();
    state.rootNote = sliceManager.rootNote.load();

    const int numSlices = sliceManager.getNumSlices();
    state.slices.reserve ((size_t) numSlices);
    for (int i = 0; i < numSlices; ++i)
        state.slices.push_back (sliceManager.getSlice (i));

    // The first sample's file, kept for single-file states and older builds.
    const auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap != nullptr)
    {
        if (! sampleSnap->sessionSamples.empty())
        {
            state.filePath = sampleSnap->sessionSamples.front().filePath;
            state.fileName = sampleSnap->sessionSamples.front().fileName;
        }
        else
        {
            state.filePath = sampleSnap->filePath;
            state.fileName = sampleSnap->fileName;
        }
    }
    else if (const auto pendingFile = getPendingStateFile(); pendingFile != juce::File())
    {
        state.filePath = pendingFile.getFullPathName();
        state.fileName = pendingFile.getFileName();
    }

    state.decodedNumFrames = sampleData.getNumFrames();
    state.decodedSampleRate = sampleData.getDecodedSampleRate();
    state.sourceNumFrames = sampleData.getSourceNumFrames();
    state.sourceSampleRate = sampleData.getSourceSampleRate();
    state.snapToZeroCrossing = snapToZeroCrossing.load();
    state.midiEditEnabled = midiEditState.enabled.load (std::memory_order_relaxed);
    state.midiEditChannel = midiEditState.channel.load (std::memory_order_relaxed);
    state.consumeMidiEditCc = midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed);

    state.selectedSessionSampleId = selectedSessionSampleId.load (std::memory_order_relaxed);
    state.stemComputeDevice = stemComputeDevice;
    SampleParamTable params;
    if (sampleSnap != nullptr)
    {
        const int numSessionSamples = juce::jmin ((int) sampleSnap->sessionSamples.size(),
                                                  SampleData::kMaxSessionSamples);
        state.sessionSamples.reserve ((size_t) numSessionSamples);
        for (int i = 0; i < numSessionSamples; ++i)
        {
            auto sample = sampleSnap->sessionSamples[(size_t) i];
            sample.stemMeta = getStemMeta (sample.sampleId);
            params.set (sample.sampleId, paramsForSample (sample.sampleId));
            state.sessionSamples.push_back (std::move (sample));
        }
    }
    state.sampleParams = params;
    return state;
}

void IntersectProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream (destData, false);
    StateFormat::write (captureSessionState(), stream);
}

void IntersectProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    presetLoadGeneration.fetch_add (1, std::memory_order_acq_rel);   // host restore wins over a pending preset
    restoreState (data, sizeInBytes, {});
}

namespace
{
// Keeps the host from calling processBlock while the message thread replaces engine state.
// suspendProcessing() takes the callback lock, so a block in progress finishes first; JUCE's
// wrappers then output silence until the scope ends. A processor that was already suspended
// (by the host or an outer scope) stays suspended.
class ScopedProcessingSuspend
{
public:
    explicit ScopedProcessingSuspend (juce::AudioProcessor& p)
        : processor (p), wasSuspended (p.isSuspended())
    {
        if (! wasSuspended)
            processor.suspendProcessing (true);
    }

    ~ScopedProcessingSuspend()
    {
        if (! wasSuspended)
            processor.suspendProcessing (false);
    }

    ScopedProcessingSuspend (const ScopedProcessingSuspend&) = delete;
    ScopedProcessingSuspend& operator= (const ScopedProcessingSuspend&) = delete;

private:
    juce::AudioProcessor& processor;
    const bool wasSuspended;
};
}

bool IntersectProcessor::restoreState (const void* data, int sizeInBytes, const RestoreOptions& options)
{
    // Read the whole state first: an unsupported or damaged one changes nothing.
    const auto read = StateFormat::read (data, (size_t) juce::jmax (0, sizeInBytes));

    // Restoring rewrites slices, sample params, stem metadata, the sample and the voices, all of
    // which processBlock reads; audio stays silent for the few milliseconds this takes.
    const ScopedProcessingSuspend suspendAudio (*this);

    if (! read.state.has_value())
    {
        setUiStatusMessage (read.error, true);
        uiSnapshotDirty.store (true, std::memory_order_release);
        publishUiSliceSnapshot();
        return false;
    }

    applySessionState (*read.state, options);
    return true;
}

void IntersectProcessor::applySessionState (const SessionState& state, const RestoreOptions& options)
{
    pendingStateRestoreToken.store (0, std::memory_order_release);
    clearUiStatusMessage();

    // APVTS state
    if (auto xml = juce::parseXML (state.apvtsXml))
    {
        auto newState = juce::ValueTree::fromXml (*xml);
        if (options.preserveUserPrefs && uiScaleParam != nullptr)
        {
            auto scaleParam = newState.getChildWithProperty ("id", ParamIds::uiScale);
            if (scaleParam.isValid())
                scaleParam.setProperty ("value", uiScaleParam->load(), nullptr);
        }
        apvts.replaceState (newState);
    }

    if (state.version == 20)
        if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (ParamIds::defaultFilterEnvAmount)))
            param->setValueNotifyingHost (param->convertTo0to1 (0.0f));

    // UI state
    zoom.store (state.zoom);
    scroll.store (state.scroll);
    midiSelectsSlice.store (state.midiSelectsSlice);
    sliceManager.rootNote.store (state.rootNote);

    const int numSlices = (int) state.slices.size();
    sliceManager.setNumSlices (numSlices);
    sliceManager.selectedSlice = state.selectedSlice;

    if (state.stemComputeDevice.has_value() && ! options.preserveUserPrefs)
        stemComputeDevice = *state.stemComputeDevice;

    for (int i = 0; i < numSlices; ++i)
        sliceManager.getSlice (i) = state.slices[(size_t) i];

    for (int i = numSlices; i < SliceManager::kMaxSlices; ++i)
        sliceManager.getSlice (i).active = false;

    // Path-based sample restore
    clearVoicesBeforeSampleSwap();
    sampleData.clear().reset();   // message thread, audio suspended: freeing it here is fine

    // Restore stem metadata from parsed session samples
    stemMetaEntryCount = 0;
    if (! state.sessionSamples.empty())
    {
        for (const auto& sample : state.sessionSamples)
        {
            if (sample.stemMeta.isGenerated)
                setStemMeta (sample.sampleId, sample.stemMeta);
        }
    }

    // A path that can't be resolved (e.g. saved on another OS) is kept verbatim: the sample
    // loads as a placeholder the user can relink instead of silently leaving the kit.
    auto resolveSamplePath = [&options] (const juce::String& savedPath)
    {
        auto file = options.resolveSamplePath != nullptr
            ? options.resolveSamplePath (savedPath)
            : (juce::File::isAbsolutePath (savedPath) ? juce::File (savedPath) : juce::File());
        return file != juce::File() ? file : juce::File::createFileWithoutCheckingPath (savedPath);
    };

    std::vector<juce::File> restoreFiles;
    std::vector<int> restoreSampleIds;
    if (! state.sessionSamples.empty())
    {
        restoreFiles.reserve (state.sessionSamples.size());
        restoreSampleIds.reserve (state.sessionSamples.size());
        for (const auto& sample : state.sessionSamples)
        {
            if (sample.filePath.isEmpty())
                continue;

            const auto file = resolveSamplePath (sample.filePath);
            if (file != juce::File())
            {
                restoreFiles.push_back (file);
                restoreSampleIds.push_back (sample.sampleId);
            }
        }
    }
    else if (state.filePath.isNotEmpty())
    {
        const auto file = resolveSamplePath (state.filePath);
        if (file != juce::File())
        {
            restoreFiles.push_back (file);
            restoreSampleIds.push_back (generateSessionSampleId());
        }
    }

    // Sample-level parameters: saved per sample since extension v7. Older projects stored one
    // shared set in the legacy APVTS params, so every sample starts from those values and the
    // project sounds exactly as it did.
    {
        const bool hasPerSample = state.sampleParams.has_value();
        const auto legacy = SampleParams::fromLegacyApvts (apvts);
        sampleParams.clear();
        for (int sampleId : restoreSampleIds)
        {
            const auto* saved = hasPerSample ? state.sampleParams->find (sampleId) : nullptr;
            sampleParams.set (sampleId, saved != nullptr ? *saved
                                                         : (hasPerSample ? SampleParams::factoryDefaults() : legacy));
        }
    }

    if (! restoreFiles.empty())
    {
        updateSampleMissingState();
        setPendingStateFile (restoreFiles.front());
        primePendingSliceTimelineRemap (state.version,
                                        state.decodedNumFrames,
                                        state.decodedSampleRate,
                                        state.sourceNumFrames,
                                        state.sourceSampleRate);
        // Preserve restored slices while loading; missing files come back as placeholders.
        selectedSessionSampleId.store (state.selectedSessionSampleId, std::memory_order_relaxed);
        pendingStateRestoreToken.store (requestSampleLoad (restoreFiles, LoadKindPreserveSlices, &restoreSampleIds),
                                        std::memory_order_release);
    }
    else
    {
        updateSampleMissingState();
        clearPendingStateFile();
        clearPendingSliceTimelineRemap();
        selectedSessionSampleId.store (-1, std::memory_order_relaxed);
    }

    snapToZeroCrossing.store (state.snapToZeroCrossing);

    // v19: MIDI edit settings
    if (! options.preserveUserPrefs)
    {
        midiEditState.enabled.store (state.midiEditEnabled, std::memory_order_relaxed);
        midiEditState.channel.store (juce::jlimit (0, 16, state.midiEditChannel), std::memory_order_relaxed);
        midiEditState.consumeMidiEditCc.store (state.consumeMidiEditCc, std::memory_order_relaxed);
    }

    sliceManager.rebuildMidiMap();
    publishUiSliceSnapshot();
}


namespace
{
// True when the pool job running on this thread has been asked to stop (e.g. plugin teardown).
bool currentPoolJobShouldExit()
{
    auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
    return job != nullptr && job->shouldExit();
}
}

// Mirrors the sample-path choice in getStateInformation(): every session sample, or the single
// legacy/missing/pending path when no session is loaded.
juce::StringArray IntersectProcessor::getStateSampleFilePaths() const
{
    juce::StringArray paths;
    if (auto sampleSnap = sampleData.getSnapshot())
    {
        const int numSessionSamples = juce::jmin ((int) sampleSnap->sessionSamples.size(),
                                                  SampleData::kMaxSessionSamples);
        for (int i = 0; i < numSessionSamples; ++i)
            paths.addIfNotAlreadyThere (sampleSnap->sessionSamples[(size_t) i].filePath);

        if (numSessionSamples == 0)
            paths.add (sampleSnap->filePath);
    }
    else if (const auto pendingFile = getPendingStateFile(); pendingFile != juce::File())
    {
        paths.add (pendingFile.getFullPathName());
    }

    paths.removeEmptyStrings();
    return paths;
}

void IntersectProcessor::savePresetAsync (const juce::File& destination, bool embedSamples)
{
    juce::MemoryBlock state;
    getStateInformation (state);
    const auto samplePaths = getStateSampleFilePaths();

    if (embedSamples && ! samplePaths.isEmpty())
        setUiStatusMessage ("Saving preset with samples...", false, UiStatusMessage::Source::transientInfo);

    fileLoadPool.addJob ([this, destination, embedSamples, state, samplePaths]
    {
        PresetJobResult done;
        done.kind = PresetJobResult::Kind::save;
        done.presetFile = destination;
        done.result = PresetFile::write (destination, state, samplePaths, embedSamples,
                                         [] { return currentPoolJobShouldExit(); });
        if (! currentPoolJobShouldExit())
            postPresetJobResult (std::move (done));
    });
}

void IntersectProcessor::loadPresetAsync (const juce::File& presetFile)
{
    if (stemJob.getState() != StemJobState::idle)
    {
        setUiStatusMessage ("Finish or cancel stem separation before loading a preset", true);
        return;
    }

    warnIfAudioNotRunning();

    const int generation = presetLoadGeneration.fetch_add (1, std::memory_order_acq_rel) + 1;
    const auto cacheDir = AppFiles::getPresetSampleCacheDir();

    fileLoadPool.addJob ([this, presetFile, generation, cacheDir]
    {
        auto superseded = [this, generation]
        {
            return currentPoolJobShouldExit()
                || presetLoadGeneration.load (std::memory_order_acquire) != generation;
        };

        PresetJobResult done;
        done.kind = PresetJobResult::Kind::load;
        done.generation = generation;
        done.presetFile = presetFile;

        PresetFile::Contents contents;
        done.result = PresetFile::read (presetFile, contents);
        if (done.result.wasOk())
        {
            const auto resolved = PresetFile::resolveSamples (presetFile, contents, cacheDir, superseded);
            for (size_t i = 0; i < resolved.size(); ++i)
            {
                if (resolved[i] != juce::File())
                    done.resolvedSamples[contents.samples[i].savedPath] = resolved[i];
                else
                    ++done.missingSamples;
            }
            done.state = std::move (contents.state);
        }

        if (! superseded())
            postPresetJobResult (std::move (done));
    });
}

void IntersectProcessor::exportPresetAsync (const juce::File& source, const juce::File& destination, bool embedSamples)
{
    if (embedSamples)
        setUiStatusMessage ("Exporting preset with samples...", false, UiStatusMessage::Source::transientInfo);

    const auto cacheDir = AppFiles::getPresetSampleCacheDir();
    fileLoadPool.addJob ([this, source, destination, embedSamples, cacheDir]
    {
        PresetJobResult done;
        done.kind = PresetJobResult::Kind::exportCopy;
        done.presetFile = destination;

        if (embedSamples)
        {
            done.result = PresetFile::exportWithSamples (source, destination, cacheDir, done.missingSamples,
                                                         [] { return currentPoolJobShouldExit(); });
        }
        else
        {
            // Copy through a temp file so a failed export never leaves a half-written preset.
            juce::TemporaryFile temp (destination);
            const bool copied = source.copyFileTo (temp.getFile()) && temp.overwriteTargetFileWithTemporary();
            done.result = copied ? juce::Result::ok()
                                 : juce::Result::fail ("Couldn't export " + source.getFileNameWithoutExtension());
        }

        if (! currentPoolJobShouldExit())
            postPresetJobResult (std::move (done));
    });
}

void IntersectProcessor::postPresetJobResult (PresetJobResult result)
{
    {
        const juce::ScopedLock sl (presetJobLock);
        completedPresetJobs.push_back (std::move (result));
    }
    triggerAsyncUpdate();
}

void IntersectProcessor::handlePresetJobCompletionOnMessageThread()
{
    std::vector<PresetJobResult> finished;
    {
        const juce::ScopedLock sl (presetJobLock);
        finished.swap (completedPresetJobs);
    }

    for (const auto& job : finished)
    {
        if (job.kind == PresetJobResult::Kind::load)
        {
            applyLoadedPreset (job);
            continue;
        }

        if (job.kind == PresetJobResult::Kind::exportCopy)
        {
            const auto name = job.presetFile.getFileNameWithoutExtension();
            if (job.result.failed())
                setUiStatusMessage (job.result.getErrorMessage(), true);
            else if (job.missingSamples > 0)
                setUiStatusMessage ("Exported " + name + " - " + juce::String (job.missingSamples)
                                        + (job.missingSamples == 1 ? " sample" : " samples") + " not found, not embedded",
                                    true);
            else
                setUiStatusMessage ("Exported " + name, false, UiStatusMessage::Source::transientInfo);
            continue;
        }

        if (job.result.failed())
        {
            setUiStatusMessage (job.result.getErrorMessage(), true);
            continue;
        }

        lastSavedPresetFile = job.presetFile;
        ++presetSaveVersion;
        setUiStatusMessage ("Saved preset " + job.presetFile.getFileNameWithoutExtension(), false,
                            UiStatusMessage::Source::transientInfo);
    }
}

void IntersectProcessor::applyLoadedPreset (const PresetJobResult& job)
{
    // A later preset load, sample load or host restore happened while this one was in flight.
    if (job.generation != presetLoadGeneration.load (std::memory_order_acquire))
        return;

    if (job.result.failed())
    {
        setUiStatusMessage (job.result.getErrorMessage(), true);
        return;
    }

    if (stemJob.getState() != StemJobState::idle)
    {
        setUiStatusMessage ("Finish or cancel stem separation before loading a preset", true);
        return;
    }

    // Queue the current kit as an undo step first, so UNDO brings it back.
    enqueueUiUndoSnapshot();

    RestoreOptions options;
    options.preserveUserPrefs = true;
    options.resolveSamplePath = [&job] (const juce::String& savedPath)
    {
        if (const auto it = job.resolvedSamples.find (savedPath); it != job.resolvedSamples.end())
            return it->second;

        // Unresolved: keep the saved path so the relink flow can name the missing file.
        return juce::File::isAbsolutePath (savedPath) ? juce::File (savedPath) : juce::File();
    };

    if (! restoreState (job.state.getData(), (int) job.state.getSize(), options))
        return;   // restoreState reported the problem

    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));

    const auto name = job.presetFile.getFileNameWithoutExtension();
    if (job.missingSamples > 0)
        setUiStatusMessage ("Loaded preset " + name + " - " + juce::String (job.missingSamples)
                                + (job.missingSamples == 1 ? " sample" : " samples") + " not found",
                            true);
    else
        setUiStatusMessage ("Loaded preset " + name, false, UiStatusMessage::Source::transientInfo);
}

void IntersectProcessor::auditionFileAsync (const juce::File& file)
{
    const int generation = auditionGeneration.fetch_add (1) + 1;
    auditionFile = file;

    // Already decoded (e.g. PLAY again after STOP): start straight away.
    if (const auto clip = auditionPlayer.getClip(); clip != nullptr && clip->file == file)
    {
        auditionPendingState = AuditionStatus::State::idle;
        auditionPlayer.play (clip);
        return;
    }

    auditionPlayer.stop();
    auditionPendingState = AuditionStatus::State::decoding;
    const double rate = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;

    auditionPool.addJob ([this, file, generation, rate]
    {
        auto superseded = [this, generation]
        {
            return currentPoolJobShouldExit() || auditionGeneration.load() != generation;
        };

        auto clip = AuditionClip::decode (file, rate, kMaxAuditionSeconds, superseded);
        if (superseded())
            return;

        {
            const juce::ScopedLock sl (auditionLock);
            completedAudition = AuditionJobResult { generation, std::move (clip) };
        }
        triggerAsyncUpdate();
    });
}

void IntersectProcessor::stopAudition()
{
    ++auditionGeneration;   // drops a decode that is still running
    auditionPendingState = AuditionStatus::State::idle;
    auditionPlayer.stop();
}

void IntersectProcessor::handleAuditionCompletionOnMessageThread()
{
    std::optional<AuditionJobResult> done;
    {
        const juce::ScopedLock sl (auditionLock);
        done.swap (completedAudition);
    }

    if (! done.has_value() || done->generation != auditionGeneration.load())
        return;

    if (done->clip != nullptr)
    {
        auditionPendingState = AuditionStatus::State::idle;
        auditionPlayer.play (done->clip);
    }
    else
    {
        auditionPendingState = AuditionStatus::State::failed;
    }
}

AuditionStatus IntersectProcessor::pollAuditionStatus()
{
    auditionPlayer.releaseRetiredClips();

    AuditionStatus status;
    status.file = auditionFile;
    status.clip = auditionPlayer.getClip();
    status.position = auditionPlayer.getPosition();
    if (auditionPendingState != AuditionStatus::State::idle)
        status.state = auditionPendingState;
    else if (auditionPlayer.isPlaying())
        status.state = AuditionStatus::State::playing;
    else if (status.clip == nullptr)
        status.state = AuditionStatus::State::idle;
    else if (status.position > 0.0f && status.position < 1.0f)
        status.state = AuditionStatus::State::paused;   // part-way through, not playing
    else
        status.state = AuditionStatus::State::stopped;
    return status;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return static_cast<juce::AudioProcessor*> (new IntersectProcessor());
}
