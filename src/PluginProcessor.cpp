#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "audio/GrainEngine.h"
#include "audio/AudioAnalysis.h"
#include <cmath>
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

    SampleDecodeJob (juce::File sourceFile, double targetRate, int loadToken,
                     IntersectProcessor::LoadKind kind,
                     SuccessFn onSuccessIn, FailureFn onFailureIn)
        : juce::ThreadPoolJob ("SampleDecodeJob"),
          file (std::move (sourceFile)),
          sampleRate (targetRate),
          token (loadToken),
          loadKind (kind),
          onSuccess (std::move (onSuccessIn)),
          onFailure (std::move (onFailureIn))
    {
    }

    JobStatus runJob() override
    {
        auto decoded = SampleData::decodeFromFile (file, sampleRate);
        if (shouldExit())
            return jobHasFinished;

        if (decoded != nullptr)
            onSuccess (token, loadKind, std::move (decoded));
        else
            onFailure (token, loadKind, file);
        return jobHasFinished;
    }

private:
    juce::File file;
    double sampleRate = 44100.0;
    int token = 0;
    IntersectProcessor::LoadKind loadKind = IntersectProcessor::LoadKindReplace;
    SuccessFn onSuccess;
    FailureFn onFailure;
};

static constexpr uint32_t kValidLockMask =
    kLockBpm | kLockPitch | kLockAlgorithm | kLockAttack | kLockDecay | kLockSustain
    | kLockRelease | kLockMuteGroup | kLockStretch | kLockTonality | kLockFormant
    | kLockFormantComp | kLockGrainMode | kLockVolume | kLockReleaseTail | kLockReverse
    | kLockOutputBus | kLockLoop | kLockOneShot | kLockCentsDetune | kLockFilterEnabled
    | kLockFilterType | kLockFilterSlope | kLockFilterCutoff | kLockFilterReso
    | kLockFilterDrive | kLockFilterKeyTrack | kLockFilterEnvAttack | kLockFilterEnvDecay
    | kLockFilterEnvSustain | kLockFilterEnvRelease | kLockFilterEnvAmount;

static Slice sanitiseRestoredSlice (Slice s)
{
    s.startSample = juce::jmax (0, s.startSample);
    s.endSample = juce::jmax (s.startSample + 1, s.endSample);
    if (s.endSample - s.startSample < 64)
        s.endSample = s.startSample + 64;

    s.midiNote = juce::jlimit (0, 127, s.midiNote);
    s.bpm = juce::jlimit (20.0f, 999.0f, s.bpm);
    s.pitchSemitones = juce::jlimit (-48.0f, 48.0f, s.pitchSemitones);
    s.algorithm = juce::jlimit (0, 2, s.algorithm);
    s.attackSec = juce::jlimit (0.0f, 1.0f, s.attackSec);
    s.decaySec = juce::jlimit (0.0f, 5.0f, s.decaySec);
    s.sustainLevel = juce::jlimit (0.0f, 1.0f, s.sustainLevel);
    s.releaseSec = juce::jlimit (0.0f, 5.0f, s.releaseSec);
    s.muteGroup = juce::jlimit (0, 32, s.muteGroup);
    s.loopMode = juce::jlimit (0, 2, s.loopMode);
    s.tonalityHz = juce::jlimit (0.0f, 8000.0f, s.tonalityHz);
    s.formantSemitones = juce::jlimit (-24.0f, 24.0f, s.formantSemitones);
    s.grainMode = juce::jlimit (0, 2, s.grainMode);
    s.volume = juce::jlimit (-100.0f, 24.0f, s.volume);
    s.outputBus = juce::jlimit (0, 15, s.outputBus);
    s.centsDetune = juce::jlimit (-100.0f, 100.0f, s.centsDetune);
    s.filterType = juce::jlimit (0, 3, s.filterType);
    s.filterSlope = juce::jlimit (0, 1, s.filterSlope);
    s.filterCutoff = juce::jlimit (20.0f, 20000.0f, s.filterCutoff);
    s.filterReso = juce::jlimit (0.0f, 100.0f, s.filterReso);
    s.filterDrive = juce::jlimit (0.0f, 100.0f, s.filterDrive);
    s.filterKeyTrack = juce::jlimit (0.0f, 100.0f, s.filterKeyTrack);
    s.filterEnvAttackSec = juce::jlimit (0.0f, 10.0f, s.filterEnvAttackSec);
    s.filterEnvDecaySec = juce::jlimit (0.0f, 10.0f, s.filterEnvDecaySec);
    s.filterEnvSustain = juce::jlimit (0.0f, 1.0f, s.filterEnvSustain);
    s.filterEnvReleaseSec = juce::jlimit (0.0f, 10.0f, s.filterEnvReleaseSec);
    s.filterEnvAmount = juce::jlimit (-96.0f, 96.0f, s.filterEnvAmount);
    s.lockMask &= kValidLockMask;
    return s;
}

static bool isCoalescableCommand (IntersectProcessor::CommandType type)
{
    return type == IntersectProcessor::CmdSetSliceParam
        || type == IntersectProcessor::CmdSetSliceBounds;
}

static bool isCriticalCommand (IntersectProcessor::CommandType type)
{
    switch (type)
    {
        case IntersectProcessor::CmdLoadFile:
        case IntersectProcessor::CmdCreateSlice:
        case IntersectProcessor::CmdDeleteSlice:
        case IntersectProcessor::CmdDuplicateSlice:
        case IntersectProcessor::CmdSplitSlice:
        case IntersectProcessor::CmdTransientChop:
        case IntersectProcessor::CmdRelinkFile:
        case IntersectProcessor::CmdUndo:
        case IntersectProcessor::CmdRedo:
        case IntersectProcessor::CmdPanic:
        case IntersectProcessor::CmdSelectSlice:
        case IntersectProcessor::CmdSetRootNote:
            return true;
        default:
            return false;
    }
}

constexpr int kNrpnCcMsb  = 99;   // NRPN MSB address byte
constexpr int kNrpnCcLsb  = 98;   // NRPN LSB address byte
constexpr int kNrpnCcIncr = 96;   // Data Increment (CC 96 = value up)
constexpr int kNrpnCcDecr = 97;   // Data Decrement (CC 97 = value down)
constexpr int kMidiEditNrpnZoom       = 8193;
constexpr int kMidiEditNrpnSliceStart = 8194;
constexpr int kMidiEditNrpnSliceEnd   = 8195;
constexpr int kMidiEditMinSliceLength = 64;
constexpr int kMidiEditStepsPerView   = 192;
constexpr float kMidiEditZoomClampMax = 16384.0f;
constexpr double kMidiEditZoomStepsPerOctave = 6.0;
constexpr double kMidiEditGestureIdleSeconds = 0.3;
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
    masterVolParam = apvts.getRawParameterValue (ParamIds::masterVolume);
    bpmParam       = apvts.getRawParameterValue (ParamIds::defaultBpm);
    pitchParam     = apvts.getRawParameterValue (ParamIds::defaultPitch);
    algoParam      = apvts.getRawParameterValue (ParamIds::defaultAlgorithm);
    attackParam    = apvts.getRawParameterValue (ParamIds::defaultAttack);
    decayParam     = apvts.getRawParameterValue (ParamIds::defaultDecay);
    sustainParam   = apvts.getRawParameterValue (ParamIds::defaultSustain);
    releaseParam   = apvts.getRawParameterValue (ParamIds::defaultRelease);
    muteGroupParam = apvts.getRawParameterValue (ParamIds::defaultMuteGroup);
    stretchParam   = apvts.getRawParameterValue (ParamIds::defaultStretchEnabled);
    tonalityParam  = apvts.getRawParameterValue (ParamIds::defaultTonality);
    formantParam   = apvts.getRawParameterValue (ParamIds::defaultFormant);
    formantCompParam = apvts.getRawParameterValue (ParamIds::defaultFormantComp);
    grainModeParam   = apvts.getRawParameterValue (ParamIds::defaultGrainMode);
    releaseTailParam = apvts.getRawParameterValue (ParamIds::defaultReleaseTail);
    reverseParam     = apvts.getRawParameterValue (ParamIds::defaultReverse);
    loopParam        = apvts.getRawParameterValue (ParamIds::defaultLoop);
    oneShotParam     = apvts.getRawParameterValue (ParamIds::defaultOneShot);
    maxVoicesParam   = apvts.getRawParameterValue (ParamIds::maxVoices);
    centsDetuneParam = apvts.getRawParameterValue (ParamIds::defaultCentsDetune);
    filterEnabledParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnabled);
    filterTypeParam = apvts.getRawParameterValue (ParamIds::defaultFilterType);
    filterSlopeParam = apvts.getRawParameterValue (ParamIds::defaultFilterSlope);
    filterCutoffParam = apvts.getRawParameterValue (ParamIds::defaultFilterCutoff);
    filterResoParam = apvts.getRawParameterValue (ParamIds::defaultFilterReso);
    filterDriveParam = apvts.getRawParameterValue (ParamIds::defaultFilterDrive);
    filterKeyTrackParam = apvts.getRawParameterValue (ParamIds::defaultFilterKeyTrack);
    filterEnvAttackParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnvAttack);
    filterEnvDecayParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnvDecay);
    filterEnvSustainParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnvSustain);
    filterEnvReleaseParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnvRelease);
    filterEnvAmountParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnvAmount);
    publishUiSliceSnapshot();
}

IntersectProcessor::~IntersectProcessor()
{
    fileLoadPool.removeAllJobs (true, 5000);
    auto* pending = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
    delete pending;
    auto* failed = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
    delete failed;
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

void IntersectProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    voicePool.setSampleRate (sampleRate);
    std::fill (std::begin (heldNotes), std::end (heldNotes), false);
}

void IntersectProcessor::releaseResources() {}

void IntersectProcessor::requestSampleLoad (const juce::File& file, LoadKind kind)
{
    const int token = nextLoadToken.fetch_add (1, std::memory_order_relaxed) + 1;
    latestLoadToken.store (token, std::memory_order_release);
    latestLoadKind.store ((int) kind, std::memory_order_release);

    // Keep only the latest completed decode payload.
    auto* oldDecoded = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
    delete oldDecoded;
    auto* oldFailure = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
    delete oldFailure;

    if (! file.existsAsFile())
    {
        if (kind == LoadKindRelink)
        {
            auto* payload = new FailedLoadResult();
            payload->token = token;
            payload->kind = kind;
            payload->file = file;
            auto* old = completedLoadFailure.exchange (payload, std::memory_order_acq_rel);
            delete old;
        }
        return;
    }

    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;

    auto onSuccess = [this] (int finishedToken, LoadKind finishedKind,
                             std::unique_ptr<SampleData::DecodedSample> decoded)
    {
        if (finishedToken != latestLoadToken.load (std::memory_order_acquire))
            return;

        auto* old = completedLoadData.exchange (decoded.release(), std::memory_order_acq_rel);
        delete old;
        latestLoadKind.store ((int) finishedKind, std::memory_order_release);
    };

    auto onFailure = [this] (int finishedToken, LoadKind finishedKind, const juce::File& failedFile)
    {
        if (finishedToken != latestLoadToken.load (std::memory_order_acquire))
            return;

        auto* payload = new FailedLoadResult();
        payload->token = finishedToken;
        payload->kind = finishedKind;
        payload->file = failedFile;
        auto* old = completedLoadFailure.exchange (payload, std::memory_order_acq_rel);
        delete old;
    };

    fileLoadPool.addJob (new SampleDecodeJob (file, sr, token, kind, onSuccess, onFailure), true);
}

void IntersectProcessor::loadFileAsync (const juce::File& file)
{
    requestSampleLoad (file, LoadKindReplace);
}

void IntersectProcessor::relinkFileAsync (const juce::File& file)
{
    requestSampleLoad (file, LoadKindRelink);
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
        v.active = false;
        voicePool.voicePositions[vi].store (0.0f,
            vi == VoicePool::kPreviewVoiceIndex
                ? std::memory_order_release
                : std::memory_order_relaxed);
    }
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
        if (s.endSample - s.startSample < 64)
            s.endSample = juce::jmin (maxLen, s.startSample + 64);
    }
}

void IntersectProcessor::publishUiSliceSnapshot()
{
    const int writeIndex = 1 - uiSliceSnapshotIndex.load (std::memory_order_relaxed);
    auto& snap = uiSliceSnapshots[(size_t) writeIndex];
    auto sampleSnap = sampleData.getSnapshot();
    snap.numSlices = sliceManager.getNumSlices();
    snap.selectedSlice = sliceManager.selectedSlice.load (std::memory_order_relaxed);
    snap.rootNote = sliceManager.rootNote.load (std::memory_order_relaxed);
    snap.sampleLoaded = (sampleSnap != nullptr);
    snap.sampleMissing = sampleMissing.load (std::memory_order_relaxed);
    snap.sampleNumFrames = sampleSnap ? sampleSnap->buffer.getNumSamples() : 0;
    if (sampleSnap != nullptr)
        snap.sampleFileName = sampleSnap->fileName;
    else if (snap.sampleMissing && missingFilePath.isNotEmpty())
        snap.sampleFileName = juce::File (missingFilePath).getFileName();
    else if (sampleData.getFileName().isNotEmpty())
        snap.sampleFileName = sampleData.getFileName();
    else
        snap.sampleFileName.clear();

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
        droppedCriticalCommandTotal.fetch_add (1, std::memory_order_relaxed);
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
        pendingSetSliceParamField.store (cmd.intParam1, std::memory_order_relaxed);
        pendingSetSliceParamValue.store (cmd.floatParam1, std::memory_order_relaxed);
        pendingSetSliceParam.store (true, std::memory_order_release);
        return true;
    }

    if (cmd.type == CmdSetSliceBounds)
    {
        const int end = cmd.numPositions > 0 ? cmd.positions[0] : (int) cmd.floatParam1;
        pendingSetSliceBoundsIdx.store (cmd.intParam1, std::memory_order_relaxed);
        pendingSetSliceBoundsStart.store (cmd.intParam2, std::memory_order_relaxed);
        pendingSetSliceBoundsEnd.store (end, std::memory_order_relaxed);
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
        cmd.intParam1 = pendingSetSliceBoundsIdx.load (std::memory_order_relaxed);
        cmd.intParam2 = pendingSetSliceBoundsStart.load (std::memory_order_relaxed);
        cmd.positions[0] = pendingSetSliceBoundsEnd.load (std::memory_order_relaxed);
        cmd.numPositions = 1;
        handleCommand (cmd);
        handledAny = true;
    }

    if (pendingSetSliceParam.exchange (false, std::memory_order_acq_rel))
    {
        Command cmd;
        cmd.type = CmdSetSliceParam;
        cmd.intParam1 = pendingSetSliceParamField.load (std::memory_order_relaxed);
        cmd.floatParam1 = pendingSetSliceParamValue.load (std::memory_order_relaxed);
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

    if (handledAny)
        uiSnapshotDirty.store (true, std::memory_order_release);

    const auto dropped = droppedCommandCount.exchange (0, std::memory_order_relaxed);
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
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }
}

UndoManager::Snapshot IntersectProcessor::makeSnapshot()
{
    UndoManager::Snapshot snap;
    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
        snap.slices[(size_t) i] = sliceManager.getSlice (i);
    snap.numSlices = sliceManager.getNumSlices();
    snap.selectedSlice = sliceManager.selectedSlice;
    snap.rootNote = sliceManager.rootNote.load();
    snap.apvtsState = apvts.copyState();
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
    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
        sliceManager.getSlice (i) = snap.slices[(size_t) i];
    sliceManager.setNumSlices (snap.numSlices);
    sliceManager.selectedSlice = snap.selectedSlice;
    sliceManager.rootNote.store (snap.rootNote);
    apvts.replaceState (snap.apvtsState);
    midiSelectsSlice.store (snap.midiSelectsSlice);
    snapToZeroCrossing.store (snap.snapToZeroCrossing);
    sliceManager.rebuildMidiMap();
    uiSnapshotDirty.store (true, std::memory_order_release);
}

void IntersectProcessor::handleCommand (const Command& cmd)
{
    switch (cmd.type)
    {
        case CmdBeginGesture:
            if (! gestureSnapshotCaptured)
                captureSnapshot();
            gestureSnapshotCaptured = true;
            blocksSinceGestureActivity = 0;
            break;

        case CmdSetSliceParam:
            if (! gestureSnapshotCaptured)
                captureSnapshot();
            gestureSnapshotCaptured = true;
            blocksSinceGestureActivity = 0;
            break;

        case CmdSetSliceBounds:
        case CmdCreateSlice:
        case CmdDeleteSlice:
        case CmdStretch:
        case CmdToggleLock:
        case CmdDuplicateSlice:
        case CmdSplitSlice:
        case CmdTransientChop:
            if (! gestureSnapshotCaptured)
                captureSnapshot();
            gestureSnapshotCaptured = false;
            blocksSinceGestureActivity = 0;
            break;

        default:
            // Leave param gesture mode after idle/non-param commands.
            if (cmd.type != CmdSetSliceParam)
                gestureSnapshotCaptured = false;
            break;
    }

    switch (cmd.type)
    {
        case CmdLoadFile:
            loadFileAsync (cmd.fileParam);
            break;

        case CmdCreateSlice:
            sliceManager.createSlice (cmd.intParam1, cmd.intParam2);
            break;

        case CmdDeleteSlice:
            sliceManager.deleteSlice (cmd.intParam1);
            break;

        case CmdLazyChopStart:
            if (sampleData.isLoaded())
            {
                PreviewStretchParams psp;
                psp.stretchEnabled = stretchParam->load() > 0.5f;
                psp.algorithm      = (int) algoParam->load();
                psp.bpm            = bpmParam->load();
                psp.pitch          = pitchParam->load();
                psp.dawBpm         = dawBpm.load();
                psp.tonality       = tonalityParam->load();
                psp.formant        = formantParam->load();
                psp.formantComp    = formantCompParam->load() > 0.5f;
                psp.grainMode      = (int) grainModeParam->load();
                psp.sampleRate     = currentSampleRate;
                psp.sample         = &sampleData;
                lazyChop.start (sampleData.getNumFrames(), sliceManager, psp,
                                snapToZeroCrossing.load(), &sampleData.getBuffer());
            }
            break;

        case CmdLazyChopStop:
            lazyChop.stop (voicePool, sliceManager);
            break;

        case CmdStretch:
        {
            int sel = sliceManager.selectedSlice;
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (sel);
                float newBpm = GrainEngine::calcStretchBpm (
                    s.startSample, s.endSample, cmd.floatParam1, currentSampleRate);
                s.bpm = newBpm;
                s.lockMask |= kLockBpm;
                s.algorithm = 1;
                s.lockMask |= kLockAlgorithm;
            }
            break;
        }

        case CmdToggleLock:
        {
            int sel = sliceManager.selectedSlice;
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (sel);
                uint32_t bit = (uint32_t) cmd.intParam1;
                bool turningOn = !(s.lockMask & bit);

                if (turningOn)
                {
                    // Snapshot the current effective (global) value into the slice field
                    // so the locked value matches what was displayed before locking.
                    if      (bit == kLockBpm)         s.bpm               = bpmParam->load();
                    else if (bit == kLockPitch)        s.pitchSemitones    = pitchParam->load();
                    else if (bit == kLockAlgorithm)    s.algorithm         = (int) algoParam->load();
                    else if (bit == kLockAttack)       s.attackSec         = attackParam->load() / 1000.0f;
                    else if (bit == kLockDecay)        s.decaySec          = decayParam->load() / 1000.0f;
                    else if (bit == kLockSustain)      s.sustainLevel      = sustainParam->load() / 100.0f;
                    else if (bit == kLockRelease)      s.releaseSec        = releaseParam->load() / 1000.0f;
                    else if (bit == kLockMuteGroup)    s.muteGroup         = (int) muteGroupParam->load();
                    else if (bit == kLockLoop)         s.loopMode          = (int) loopParam->load();
                    else if (bit == kLockStretch)      s.stretchEnabled    = stretchParam->load()     > 0.5f;
                    else if (bit == kLockReleaseTail)  s.releaseTail       = releaseTailParam->load() > 0.5f;
                    else if (bit == kLockReverse)      s.reverse           = reverseParam->load()     > 0.5f;
                    else if (bit == kLockOneShot)      s.oneShot           = oneShotParam->load()     > 0.5f;
                    else if (bit == kLockCentsDetune)  s.centsDetune       = centsDetuneParam->load();
                    else if (bit == kLockTonality)     s.tonalityHz        = tonalityParam->load();
                    else if (bit == kLockFormant)      s.formantSemitones  = formantParam->load();
                    else if (bit == kLockFormantComp)  s.formantComp       = formantCompParam->load() > 0.5f;
                    else if (bit == kLockGrainMode)    s.grainMode         = (int) grainModeParam->load();
                    else if (bit == kLockVolume)       s.volume            = masterVolParam->load();
                    else if (bit == kLockFilterEnabled)    s.filterEnabled      = filterEnabledParam->load() > 0.5f;
                    else if (bit == kLockFilterType)       s.filterType         = (int) filterTypeParam->load();
                    else if (bit == kLockFilterSlope)      s.filterSlope        = (int) filterSlopeParam->load();
                    else if (bit == kLockFilterCutoff)     s.filterCutoff       = filterCutoffParam->load();
                    else if (bit == kLockFilterReso)       s.filterReso         = filterResoParam->load();
                    else if (bit == kLockFilterDrive)      s.filterDrive        = filterDriveParam->load();
                    else if (bit == kLockFilterKeyTrack)   s.filterKeyTrack     = filterKeyTrackParam->load();
                    else if (bit == kLockFilterEnvAttack)  s.filterEnvAttackSec  = filterEnvAttackParam->load() / 1000.0f;
                    else if (bit == kLockFilterEnvDecay)   s.filterEnvDecaySec   = filterEnvDecayParam->load() / 1000.0f;
                    else if (bit == kLockFilterEnvSustain) s.filterEnvSustain    = filterEnvSustainParam->load() / 100.0f;
                    else if (bit == kLockFilterEnvRelease) s.filterEnvReleaseSec = filterEnvReleaseParam->load() / 1000.0f;
                    else if (bit == kLockFilterEnvAmount)  s.filterEnvAmount     = filterEnvAmountParam->load();
                    // kLockOutputBus: no global default param — slice default (0) is correct
                }

                s.lockMask ^= bit;
            }
            break;
        }

        case CmdSetSliceParam:
        {
            int sel = sliceManager.selectedSlice;
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (sel);
                int field = cmd.intParam1;
                float val = cmd.floatParam1;
                constexpr float kCompareTolerance = 1.0e-4f;

                auto setFloatField = [&s, kCompareTolerance] (float& target, float newValue, float globalValue, uint32_t lockBit)
                {
                    target = newValue;
                    if (std::abs (target - globalValue) <= kCompareTolerance)
                        s.lockMask &= ~lockBit;
                    else
                        s.lockMask |= lockBit;
                };

                auto setIntField = [&s] (int& target, int newValue, int globalValue, uint32_t lockBit)
                {
                    target = newValue;
                    if (target == globalValue)
                        s.lockMask &= ~lockBit;
                    else
                        s.lockMask |= lockBit;
                };

                auto setBoolField = [&s] (bool& target, bool newValue, bool globalValue, uint32_t lockBit)
                {
                    target = newValue;
                    if (target == globalValue)
                        s.lockMask &= ~lockBit;
                    else
                        s.lockMask |= lockBit;
                };

                switch (field)
                {
                    case FieldBpm:
                        setFloatField (s.bpm, val, bpmParam->load(), kLockBpm);
                        break;
                    case FieldPitch:
                        setFloatField (s.pitchSemitones, val, pitchParam->load(), kLockPitch);
                        break;
                    case FieldAlgorithm:
                        setIntField (s.algorithm, (int) val, (int) algoParam->load(), kLockAlgorithm);
                        break;
                    case FieldAttack:
                        setFloatField (s.attackSec, val, attackParam->load() / 1000.0f, kLockAttack);
                        break;
                    case FieldDecay:
                        setFloatField (s.decaySec, val, decayParam->load() / 1000.0f, kLockDecay);
                        break;
                    case FieldSustain:
                        setFloatField (s.sustainLevel, val, sustainParam->load() / 100.0f, kLockSustain);
                        break;
                    case FieldRelease:
                        setFloatField (s.releaseSec, val, releaseParam->load() / 1000.0f, kLockRelease);
                        break;
                    case FieldMuteGroup:
                        setIntField (s.muteGroup, (int) val, (int) muteGroupParam->load(), kLockMuteGroup);
                        break;
                    case FieldStretchEnabled:
                        setBoolField (s.stretchEnabled, val > 0.5f, stretchParam->load() > 0.5f, kLockStretch);
                        break;
                    case FieldTonality:
                        setFloatField (s.tonalityHz, val, tonalityParam->load(), kLockTonality);
                        break;
                    case FieldFormant:
                        setFloatField (s.formantSemitones, val, formantParam->load(), kLockFormant);
                        break;
                    case FieldFormantComp:
                        setBoolField (s.formantComp, val > 0.5f, formantCompParam->load() > 0.5f, kLockFormantComp);
                        break;
                    case FieldGrainMode:
                        setIntField (s.grainMode, (int) val, (int) grainModeParam->load(), kLockGrainMode);
                        break;
                    case FieldVolume:
                        setFloatField (s.volume, val, masterVolParam->load(), kLockVolume);
                        break;
                    case FieldReleaseTail:
                        setBoolField (s.releaseTail, val > 0.5f, releaseTailParam->load() > 0.5f, kLockReleaseTail);
                        break;
                    case FieldReverse:
                        setBoolField (s.reverse, val > 0.5f, reverseParam->load() > 0.5f, kLockReverse);
                        break;
                    case FieldOutputBus:
                        s.outputBus = juce::jlimit (0, 15, (int) val);
                        s.lockMask |= kLockOutputBus;
                        break;
                    case FieldLoop:
                        setIntField (s.loopMode, (int) val, (int) loopParam->load(), kLockLoop);
                        break;
                    case FieldOneShot:
                        setBoolField (s.oneShot, val > 0.5f, oneShotParam->load() > 0.5f, kLockOneShot);
                        break;
                    case FieldCentsDetune:
                        setFloatField (s.centsDetune, val, centsDetuneParam->load(), kLockCentsDetune);
                        break;
                    case FieldFilterEnabled:
                        setBoolField (s.filterEnabled, val > 0.5f, filterEnabledParam->load() > 0.5f, kLockFilterEnabled);
                        break;
                    case FieldFilterType:
                        setIntField (s.filterType, juce::jlimit (0, 3, (int) val), (int) filterTypeParam->load(), kLockFilterType);
                        break;
                    case FieldFilterSlope:
                        setIntField (s.filterSlope, juce::jlimit (0, 1, (int) val), (int) filterSlopeParam->load(), kLockFilterSlope);
                        break;
                    case FieldFilterCutoff:
                        setFloatField (s.filterCutoff, val, filterCutoffParam->load(), kLockFilterCutoff);
                        break;
                    case FieldFilterReso:
                        setFloatField (s.filterReso, val, filterResoParam->load(), kLockFilterReso);
                        break;
                    case FieldFilterDrive:
                        setFloatField (s.filterDrive, val, filterDriveParam->load(), kLockFilterDrive);
                        break;
                    case FieldFilterKeyTrack:
                        setFloatField (s.filterKeyTrack, val, filterKeyTrackParam->load(), kLockFilterKeyTrack);
                        break;
                    case FieldFilterEnvAttack:
                        setFloatField (s.filterEnvAttackSec, val, filterEnvAttackParam->load() / 1000.0f, kLockFilterEnvAttack);
                        break;
                    case FieldFilterEnvDecay:
                        setFloatField (s.filterEnvDecaySec, val, filterEnvDecayParam->load() / 1000.0f, kLockFilterEnvDecay);
                        break;
                    case FieldFilterEnvSustain:
                        setFloatField (s.filterEnvSustain, val, filterEnvSustainParam->load() / 100.0f, kLockFilterEnvSustain);
                        break;
                    case FieldFilterEnvRelease:
                        setFloatField (s.filterEnvReleaseSec, val, filterEnvReleaseParam->load() / 1000.0f, kLockFilterEnvRelease);
                        break;
                    case FieldFilterEnvAmount:
                        setFloatField (s.filterEnvAmount, val, filterEnvAmountParam->load(), kLockFilterEnvAmount);
                        break;
                    case FieldMidiNote:
                        s.midiNote = juce::jlimit (0, 127, (int) val);
                        sliceManager.rebuildMidiMap();
                        break;
                }
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
                if (end - start < 64)
                    end = juce::jmin (maxLen, start + 64);
                s.startSample = start;
                s.endSample = end;
                sliceManager.rebuildMidiMap();
            }
            break;
        }

        case CmdDuplicateSlice:
        {
            int sel = sliceManager.selectedSlice;
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                const auto& src = sliceManager.getSlice (sel);
                int newIdx = sliceManager.createSlice (src.startSample, src.endSample);
                if (newIdx >= 0)
                {
                    auto& dst = sliceManager.getSlice (newIdx);
                    int savedNote = dst.midiNote;  // assigned by createSlice
                    dst = src;                     // copy all params, lockMask, colour
                    dst.midiNote = savedNote;      // restore unique MIDI note
                    if (cmd.intParam1 >= 0)        // ctrl-drag: use explicit position
                    {
                        dst.startSample = cmd.intParam1;
                        dst.endSample   = cmd.intParam2;
                    }
                    // else (intParam1 == -1): inherit src.startSample/endSample as-is
                    sliceManager.selectedSlice = newIdx;
                }
            }
            break;
        }

        case CmdSplitSlice:
        {
            int sel = sliceManager.selectedSlice;
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                Slice srcCopy = sliceManager.getSlice (sel);
                int startS = srcCopy.startSample;
                int endS = srcCopy.endSample;
                int count = juce::jlimit (2, 128, cmd.intParam1);
                int len = endS - startS;
                // Notes for sub-slices start one past the highest note any existing
                // slice can have, so no existing note is disturbed.
                int baseNote = sliceManager.rootNote.load() + sliceManager.getNumSlices();

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
                    if (e - s < 64) e = s + 64;
                    int idx = sliceManager.createSlice (s, e);
                    if (idx >= 0)
                    {
                        auto& dst = sliceManager.getSlice (idx);
                        juce::Colour savedColour = dst.colour;
                        dst = srcCopy;
                        dst.startSample = s;
                        dst.endSample   = e;
                        dst.midiNote    = juce::jlimit (0, 127, baseNote + i);
                        dst.colour      = savedColour;
                        dst.active      = true;
                    }
                    if (i == 0) firstNew = idx;
                }

                sliceManager.rebuildMidiMap();
                if (firstNew >= 0)
                    sliceManager.selectedSlice = firstNew;
            }
            break;
        }

        case CmdTransientChop:
        {
            int sel = sliceManager.selectedSlice;
            if (sel >= 0 && sel < sliceManager.getNumSlices() && cmd.numPositions > 0)
            {
                Slice srcCopy = sliceManager.getSlice (sel);
                int startS = srcCopy.startSample;
                int endS = srcCopy.endSample;
                int baseNote = sliceManager.rootNote.load() + sliceManager.getNumSlices();

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
                    if (e - s < 64) continue;
                    int idx = sliceManager.createSlice (s, e);
                    if (idx >= 0)
                    {
                        auto& dst = sliceManager.getSlice (idx);
                        juce::Colour savedColour = dst.colour;
                        dst = srcCopy;
                        dst.startSample = s;
                        dst.endSample   = e;
                        dst.midiNote    = juce::jlimit (0, 127, baseNote + subIdx);
                        dst.colour      = savedColour;
                        dst.active      = true;
                    }
                    if (firstNew < 0) firstNew = idx;
                    ++subIdx;
                }

                sliceManager.rebuildMidiMap();
                if (firstNew >= 0)
                    sliceManager.selectedSlice = firstNew;
            }
            break;
        }

        case CmdRelinkFile:
            relinkFileAsync (cmd.fileParam);
            break;

        case CmdFileLoadFailed:
            if (cmd.intParam1 == latestLoadToken.load (std::memory_order_acquire)
                && cmd.intParam2 == (int) LoadKindRelink)
            {
                sampleMissing.store (true);
                missingFilePath = cmd.fileParam.getFullPathName();
                sampleData.setFileName (cmd.fileParam.getFileName());
                sampleData.setFilePath (cmd.fileParam.getFullPathName());
                sampleAvailability.store ((int) SampleStateMissingAwaitingRelink,
                                         std::memory_order_relaxed);
            }
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
            lazyChop.stop (voicePool, sliceManager);
            std::fill (std::begin (heldNotes), std::end (heldNotes), false);
            break;

        case CmdSelectSlice:
            sliceManager.selectedSlice.store (
                juce::jlimit (-1, juce::jmax (-1, sliceManager.getNumSlices() - 1), cmd.intParam1),
                std::memory_order_relaxed);
            break;

        case CmdSetRootNote:
            sliceManager.rootNote.store (juce::jlimit (0, 127, cmd.intParam1),
                                         std::memory_order_relaxed);
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

void IntersectProcessor::processMidi (juce::MidiBuffer& midi)
{
    const bool editEnabled = midiEditState.enabled.load (std::memory_order_acquire);
    const int  editChannel = midiEditState.channel.load (std::memory_order_relaxed);
    const bool doConsume   = midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed);

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        if (editEnabled && msg.isController()
            && (editChannel == 0 || msg.getChannel() == editChannel))
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
                int newSliceIdx = lazyChop.onNote (note, voicePool, sliceManager);
                if (newSliceIdx >= 0)
                {
                    sliceManager.selectedSlice.store (newSliceIdx, std::memory_order_relaxed);
                    uiSnapshotDirty.store (true, std::memory_order_release);
                }
            }
            else
            {
                heldNotes[note] = true;

                // Build params once; all param loads happen here, not inside the slice loop.
                VoiceStartParams p;
                p.note             = note;
                p.velocity         = velocity;
                p.globalBpm        = bpmParam->load();
                p.globalPitch      = pitchParam->load();
                p.globalAlgorithm  = (int) algoParam->load();
                p.globalAttackSec  = attackParam->load()  / 1000.0f;
                p.globalDecaySec   = decayParam->load()   / 1000.0f;
                p.globalSustain    = sustainParam->load() / 100.0f;
                p.globalReleaseSec = releaseParam->load() / 1000.0f;
                p.globalMuteGroup  = (int) muteGroupParam->load();
                p.globalStretch    = stretchParam->load()      > 0.5f;
                p.dawBpm           = dawBpm.load();
                p.globalTonality   = tonalityParam->load();
                p.globalFormant    = formantParam->load();
                p.globalFormantComp = formantCompParam->load() > 0.5f;
                p.globalGrainMode  = (int) grainModeParam->load();
                p.globalVolume     = masterVolParam->load();
                p.globalReleaseTail = releaseTailParam->load() > 0.5f;
                p.globalReverse    = reverseParam->load()      > 0.5f;
                p.globalLoopMode   = (int) loopParam->load();
                p.globalOneShot    = oneShotParam->load()      > 0.5f;
                p.globalCentsDetune = centsDetuneParam->load();
                p.globalFilterEnabled = filterEnabledParam->load() > 0.5f;
                p.globalFilterType = (int) filterTypeParam->load();
                p.globalFilterSlope = (int) filterSlopeParam->load();
                p.globalFilterCutoff = filterCutoffParam->load();
                p.globalFilterReso = filterResoParam->load();
                p.globalFilterDrive = filterDriveParam->load();
                p.globalFilterKeyTrack = filterKeyTrackParam->load();
                p.globalFilterEnvAttackSec = filterEnvAttackParam->load() / 1000.0f;
                p.globalFilterEnvDecaySec = filterEnvDecayParam->load() / 1000.0f;
                p.globalFilterEnvSustain = filterEnvSustainParam->load() / 100.0f;
                p.globalFilterEnvReleaseSec = filterEnvReleaseParam->load() / 1000.0f;
                p.globalFilterEnvAmount = filterEnvAmountParam->load();
                p.rootNote = sliceManager.rootNote.load();

                const auto& sliceIndices = sliceManager.midiNoteToSlices (note);
                for (int sliceIdx : sliceIndices)
                {
                    if (midiSelectsSlice.load (std::memory_order_relaxed))
                    {
                        const int previous = sliceManager.selectedSlice.load (std::memory_order_relaxed);
                        sliceManager.selectedSlice.store (sliceIdx, std::memory_order_relaxed);
                        if (previous != sliceIdx)
                            uiSnapshotDirty.store (true, std::memory_order_release);
                    }

                    int voiceIdx = voicePool.allocate();

                    // Handle mute groups
                    const auto& s = sliceManager.getSlice (sliceIdx);
                    int mg = (int) sliceManager.resolveParam (sliceIdx, kLockMuteGroup,
                                                              (float) s.muteGroup, (float) p.globalMuteGroup);
                    voicePool.muteGroup (mg, voiceIdx);

                    p.sliceIdx = sliceIdx;
                    voicePool.startVoice (voiceIdx, p, sliceManager, sampleData);
                }
            }
        }
        else if (msg.isNoteOff())
        {
            int note = msg.getNoteNumber();
            if (heldNotes[note])
            {
                heldNotes[note] = false;
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

    // Strip MIDI edit CCs from the buffer so they don't pass downstream
    if (editEnabled && doConsume)
    {
        juce::MidiBuffer filtered;
        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            const int cc = msg.getControllerNumber();
            const bool strip = msg.isController()
                && (editChannel == 0 || msg.getChannel() == editChannel)
                && (cc == kNrpnCcMsb || cc == kNrpnCcLsb || cc == kNrpnCcIncr || cc == kNrpnCcDecr);
            if (! strip)
                filtered.addEvent (msg, metadata.samplePosition);
        }
        midi = std::move (filtered);
    }
}

static inline float sanitiseSample (float x)
{
    if (! std::isfinite (x)) return 0.0f;
    return juce::jlimit (-1.0f, 1.0f, x);
}

void IntersectProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Read DAW BPM from playhead
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpmOpt = pos->getBpm())
                dawBpm.store ((float) *bpmOpt, std::memory_order_relaxed);
        }
    }

    // Poll shift preview request (atomic, avoids FIFO latency)
    {
        int req = shiftPreviewRequest.exchange (-2, std::memory_order_relaxed);
        if (req == -1)
            voicePool.stopShiftPreview();
        else if (req >= 0 && ! lazyChop.isActive() && sampleData.isLoaded())
        {
            PreviewStretchParams psp;
            psp.stretchEnabled = stretchParam->load() > 0.5f;
            psp.algorithm      = (int) algoParam->load();
            psp.bpm            = bpmParam->load();
            psp.pitch          = pitchParam->load();
            psp.dawBpm         = dawBpm.load();
            psp.tonality       = tonalityParam->load();
            psp.formant        = formantParam->load();
            psp.formantComp    = formantCompParam->load() > 0.5f;
            psp.grainMode      = (int) grainModeParam->load();
            psp.sampleRate     = currentSampleRate;
            psp.sample         = &sampleData;
            voicePool.startShiftPreview (req, sampleData.getNumFrames(), psp);
        }
    }

    bool loadStateChanged = false;
    {
        auto* rawDecoded = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
        if (rawDecoded != nullptr)
        {
            std::unique_ptr<SampleData::DecodedSample> decoded (rawDecoded);
            clearVoicesBeforeSampleSwap();
            sampleData.applyDecodedSample (std::move (decoded));
            sampleMissing.store (false);
            missingFilePath.clear();
            sampleAvailability.store ((int) SampleStateLoaded, std::memory_order_relaxed);

            if (latestLoadKind.load (std::memory_order_acquire) == (int) LoadKindReplace)
                sliceManager.clearAll();
            else
            {
                clampSlicesToSampleBounds();
                sliceManager.rebuildMidiMap();
            }
            loadStateChanged = true;
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }

    {
        auto* rawFailure = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
        if (rawFailure != nullptr)
        {
            std::unique_ptr<FailedLoadResult> failed (rawFailure);
            if (failed->token == latestLoadToken.load (std::memory_order_acquire)
                && failed->kind == LoadKindRelink)
            {
                sampleMissing.store (true);
                missingFilePath = failed->file.getFullPathName();
                sampleData.setFileName (failed->file.getFileName());
                sampleData.setFilePath (failed->file.getFullPathName());
                sampleAvailability.store ((int) SampleStateMissingAwaitingRelink,
                                         std::memory_order_relaxed);
                loadStateChanged = true;
                uiSnapshotDirty.store (true, std::memory_order_release);
            }
        }
    }

    if (loadStateChanged)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));

    drainCommands();
    applyLiveDragBoundsToSlice();

    // Update max active voices from param
    voicePool.setMaxActiveVoices ((int) maxVoicesParam->load());

    processMidi (midi);

    if (midiEditState.gestureOpen && midiEditState.previewActive)
    {
        blocksSinceGestureActivity = 0;
        commitMidiSliceBoundaryGestureIfIdle (buffer.getNumSamples());
    }
    else if (gestureSnapshotCaptured)
    {
        ++blocksSinceGestureActivity;
        if (blocksSinceGestureActivity > 2)
            gestureSnapshotCaptured = false;
    }

    if (uiSnapshotDirty.exchange (false, std::memory_order_acq_rel))
        publishUiSliceSnapshot();

    if (! sampleData.isLoaded())
    {
        if (! sampleMissing.load (std::memory_order_relaxed))
            sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
        return;
    }

    // Collect write pointers for all enabled output buses
    static constexpr int kMaxBuses = 16;
    float* busL[kMaxBuses] = {};
    float* busR[kMaxBuses] = {};
    int numActiveBuses = 0;

    for (int b = 0; b < std::min (getBusCount (false), kMaxBuses); ++b)
    {
        auto* bus = getBus (false, b);
        if (bus != nullptr && bus->isEnabled())
        {
            int chOff = getChannelIndexInProcessBlockBuffer (false, b, 0);
            if (chOff < buffer.getNumChannels())
            {
                busL[b] = buffer.getWritePointer (chOff);
                busR[b] = (chOff + 1 < buffer.getNumChannels())
                              ? buffer.getWritePointer (chOff + 1) : nullptr;
                if (b + 1 > numActiveBuses) numActiveBuses = b + 1;
            }
        }
    }

    buffer.clear();

    if (numActiveBuses <= 1)
    {
        // Fast path: single stereo output
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float sL = 0.0f, sR = 0.0f;
            voicePool.processSample (sampleData, currentSampleRate, sL, sR);
            if (busL[0]) busL[0][i] = sanitiseSample (sL);
            if (busR[0]) busR[0][i] = sanitiseSample (sR);
        }
    }
    else
    {
        // Multi-out: route each voice to its assigned bus
        constexpr int previewIdx = VoicePool::kPreviewVoiceIndex;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (int vi = 0; vi < voicePool.getMaxActiveVoices(); ++vi)
            {
                float vL = 0.0f, vR = 0.0f;
                voicePool.processVoiceSample (vi, sampleData, currentSampleRate, vL, vR);

                int bus = voicePool.getVoice (vi).outputBus;
                if (bus < 0 || bus >= numActiveBuses || busL[bus] == nullptr) bus = 0;
                if (busL[bus]) busL[bus][i] += vL;
                if (busR[bus]) busR[bus][i] += vR;
            }

            // Always process preview voice (LazyChopEngine) on main bus
            if (previewIdx >= voicePool.getMaxActiveVoices()
                && voicePool.getVoice (previewIdx).active)
            {
                float vL = 0.0f, vR = 0.0f;
                voicePool.processVoiceSample (previewIdx, sampleData, currentSampleRate, vL, vR);
                if (busL[0]) busL[0][i] += vL;
                if (busR[0]) busR[0][i] += vR;
            }
        }
        // Clamp / NaN-guard every active bus after accumulation
        for (int b = 0; b < numActiveBuses; ++b)
        {
            if (busL[b])
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    busL[b][i] = sanitiseSample (busL[b][i]);
            if (busR[b])
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    busR[b][i] = sanitiseSample (busR[b][i]);
        }
    }

    // Pass through MIDI
    // (already in the buffer, no action needed)
}

juce::AudioProcessorEditor* IntersectProcessor::createEditor()
{
    return new IntersectEditor (*this);
}

void IntersectProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream (destData, false);

    // Version
    stream.writeInt (21);

    // APVTS state
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    auto xmlString = xml->toString();
    stream.writeString (xmlString);

    // UI state
    stream.writeFloat (zoom.load());
    stream.writeFloat (scroll.load());
    stream.writeInt (sliceManager.selectedSlice);
    stream.writeBool (midiSelectsSlice.load());
    stream.writeInt (sliceManager.rootNote.load());

    // Slice data
    int numSlices = sliceManager.getNumSlices();
    stream.writeInt (numSlices);
    for (int i = 0; i < numSlices; ++i)
    {
        const auto& s = sliceManager.getSlice (i);
        stream.writeBool (s.active);
        stream.writeInt (s.startSample);
        stream.writeInt (s.endSample);
        stream.writeInt (s.midiNote);
        stream.writeFloat (s.bpm);
        stream.writeFloat (s.pitchSemitones);
        stream.writeInt (s.algorithm);
        stream.writeFloat (s.attackSec);
        stream.writeFloat (s.decaySec);
        stream.writeFloat (s.sustainLevel);
        stream.writeFloat (s.releaseSec);
        stream.writeInt (s.muteGroup);
        stream.writeInt (s.loopMode);
        stream.writeBool (s.stretchEnabled);
        stream.writeInt ((int) s.lockMask);
        stream.writeInt ((int) s.colour.getARGB());
        // v5 fields
        stream.writeFloat (s.tonalityHz);
        stream.writeFloat (s.formantSemitones);
        stream.writeBool (s.formantComp);
        // v6 fields
        stream.writeInt (s.grainMode);
        // v7 fields
        stream.writeFloat (s.volume);
        // v10 fields
        stream.writeBool (s.releaseTail);
        // v11 fields
        stream.writeBool (s.reverse);
        stream.writeInt (s.outputBus);
        // v15 fields
        stream.writeBool (s.oneShot);
        // v16 fields
        stream.writeFloat (s.centsDetune);
        // v20 fields
        stream.writeBool (s.filterEnabled);
        stream.writeInt (s.filterType);
        stream.writeInt (s.filterSlope);
        stream.writeFloat (s.filterCutoff);
        stream.writeFloat (s.filterReso);
        stream.writeFloat (s.filterDrive);
        stream.writeFloat (s.filterKeyTrack);
        stream.writeFloat (s.filterEnvAttackSec);
        stream.writeFloat (s.filterEnvDecaySec);
        stream.writeFloat (s.filterEnvSustain);
        stream.writeFloat (s.filterEnvReleaseSec);
        stream.writeFloat (s.filterEnvAmount);
    }

    // v9: store file path only (no PCM)
    stream.writeString (sampleData.getFilePath());
    stream.writeString (sampleData.getFileName());

    // v12: snap-to-zero-crossing toggle
    stream.writeBool (snapToZeroCrossing.load());

    // v19: MIDI edit settings
    stream.writeBool (midiEditState.enabled.load (std::memory_order_relaxed));
    stream.writeInt  (midiEditState.channel.load (std::memory_order_relaxed));
    stream.writeBool (midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed));
}

void IntersectProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream (data, (size_t) sizeInBytes, false);

    int version = stream.readInt();
    if (version != 19 && version != 20 && version != 21)
        return;

    // APVTS state
    auto xmlString = stream.readString();
    if (auto xml = juce::parseXML (xmlString))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

    if (version == 20)
        if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (ParamIds::defaultFilterEnvAmount)))
            param->setValueNotifyingHost (param->convertTo0to1 (0.0f));

    // UI state
    zoom.store (juce::jlimit (1.0f, 16384.0f, stream.readFloat()));
    scroll.store (juce::jlimit (0.0f, 1.0f, stream.readFloat()));
    int savedSelectedSlice = stream.readInt();

    midiSelectsSlice.store (stream.readBool());
    sliceManager.rootNote.store (juce::jlimit (0, 127, stream.readInt()));

    // Slice data
    const int storedNumSlices = stream.readInt();
    if (storedNumSlices < 0 || storedNumSlices > 4096)
        return;

    const int validatedNumSlices = juce::jlimit (0, SliceManager::kMaxSlices, storedNumSlices);
    sliceManager.setNumSlices (validatedNumSlices);
    sliceManager.selectedSlice = juce::jlimit (-1, validatedNumSlices - 1, savedSelectedSlice);

    for (int i = 0; i < storedNumSlices; ++i)
    {
        Slice parsed;
        parsed.active         = stream.readBool();
        parsed.startSample    = stream.readInt();
        parsed.endSample      = stream.readInt();
        parsed.midiNote       = stream.readInt();
        parsed.bpm            = stream.readFloat();
        parsed.pitchSemitones = stream.readFloat();
        parsed.algorithm      = stream.readInt();
        parsed.attackSec      = stream.readFloat();
        parsed.decaySec       = stream.readFloat();
        parsed.sustainLevel   = stream.readFloat();
        parsed.releaseSec     = stream.readFloat();
        parsed.muteGroup      = stream.readInt();
        parsed.loopMode       = stream.readInt();
        parsed.stretchEnabled = stream.readBool();
        parsed.lockMask       = (uint32_t) stream.readInt();
        parsed.colour         = juce::Colour ((juce::uint32) stream.readInt());
        parsed.tonalityHz     = stream.readFloat();
        parsed.formantSemitones = stream.readFloat();
        parsed.formantComp    = stream.readBool();
        parsed.grainMode      = stream.readInt();
        parsed.volume         = stream.readFloat();
        parsed.releaseTail    = stream.readBool();
        parsed.reverse        = stream.readBool();
        parsed.outputBus      = stream.readInt();
        parsed.oneShot        = stream.readBool();
        parsed.centsDetune    = stream.readFloat();
        if (version >= 20)
        {
            parsed.filterEnabled = stream.readBool();
            parsed.filterType = stream.readInt();
            parsed.filterSlope = stream.readInt();
            parsed.filterCutoff = stream.readFloat();
            parsed.filterReso = stream.readFloat();
            parsed.filterDrive = stream.readFloat();
            parsed.filterKeyTrack = stream.readFloat();
            parsed.filterEnvAttackSec = stream.readFloat();
            parsed.filterEnvDecaySec = stream.readFloat();
            parsed.filterEnvSustain = stream.readFloat();
            parsed.filterEnvReleaseSec = stream.readFloat();
            parsed.filterEnvAmount = stream.readFloat();

            if (version == 20)
                parsed.filterEnvAmount = 0.0f;
        }

        if (i < validatedNumSlices)
            sliceManager.getSlice (i) = sanitiseRestoredSlice (parsed);
    }

    for (int i = validatedNumSlices; i < SliceManager::kMaxSlices; ++i)
        sliceManager.getSlice (i).active = false;

    // Path-based sample restore
    auto filePath = stream.readString();
    auto fileName = stream.readString();

    clearVoicesBeforeSampleSwap();
    sampleData.clear();

    if (filePath.isNotEmpty())
    {
        const juce::File restoredFile (filePath);
        sampleMissing.store (false);
        missingFilePath.clear();
        sampleData.setFileName (fileName.isNotEmpty() ? fileName : restoredFile.getFileName());
        sampleData.setFilePath (filePath);
        sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
        // Preserve restored slices while loading, and report missing path via relink state.
        requestSampleLoad (restoredFile, LoadKindRelink);
    }
    else
    {
        sampleMissing.store (false);
        missingFilePath.clear();
        sampleData.setFileName ({});
        sampleData.setFilePath ({});
        sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
    }

    sliceManager.rebuildMidiMap();
    publishUiSliceSnapshot();

    snapToZeroCrossing.store (stream.readBool());

    // v19: MIDI edit settings
    midiEditState.enabled.store     (stream.readBool(), std::memory_order_relaxed);
    midiEditState.channel.store     (stream.readInt(),  std::memory_order_relaxed);
    midiEditState.consumeMidiEditCc.store (stream.readBool(), std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return static_cast<juce::AudioProcessor*> (new IntersectProcessor());
}
