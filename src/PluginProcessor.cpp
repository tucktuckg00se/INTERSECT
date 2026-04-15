#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Constants.h"
#include "audio/GrainEngine.h"
#include "audio/AudioAnalysis.h"
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
                     double targetRate, int loadToken,
                     IntersectProcessor::LoadKind kind,
                     SuccessFn onSuccessIn, FailureFn onFailureIn)
        : juce::ThreadPoolJob ("SampleDecodeJob"),
          files (std::move (sourceFiles)),
          sampleIds (std::move (sourceSampleIds)),
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

        auto decoded = SampleData::decodeFromFiles (files, sampleRate, &sampleIds);
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
    double sampleRate = 44100.0;
    int token = 0;
    IntersectProcessor::LoadKind loadKind = IntersectProcessor::LoadKindReplace;
    SuccessFn onSuccess;
    FailureFn onFailure;
};

static constexpr uint64_t kValidLockMask =
    kLockBpm | kLockPitch | kLockAlgorithm | kLockAttack | kLockDecay | kLockSustain
    | kLockRelease | kLockMuteGroup | kLockStretch | kLockTonality | kLockFormant
    | kLockFormantComp | kLockGrainMode | kLockVolume | kLockReleaseTail | kLockReverse
    | kLockOutputBus | kLockLoop | kLockOneShot | kLockCentsDetune | kLockFilterEnabled
    | kLockFilterType | kLockFilterSlope | kLockFilterCutoff | kLockFilterReso
    | kLockFilterDrive | kLockFilterKeyTrack | kLockFilterEnvAttack | kLockFilterEnvDecay
    | kLockFilterEnvSustain | kLockFilterEnvRelease | kLockFilterEnvAmount
    | kLockFilterAsym | kLockCrossfade | kLockRepitchMode
    | kLockLoopStart | kLockLoopLength
    | kLockHighNote | kLockSliceRootNote;

// Copies a global parameter value into a slice field based on the lock bit.
// Used when locking a parameter to snapshot the current effective value.
static void copyGlobalToSlice (Slice& s, const GlobalParamSnapshot& g, uint64_t bit)
{
    switch (bit)
    {
        case kLockBpm:              s.bpm = g.bpm;                          break;
        case kLockPitch:            s.pitchSemitones = g.pitchSemitones;    break;
        case kLockAlgorithm:        s.algorithm = g.algorithm;              break;
        case kLockRepitchMode:      s.repitchMode = g.repitchMode;          break;
        case kLockAttack:           s.attackSec = g.attackSec;              break;
        case kLockDecay:            s.decaySec = g.decaySec;                break;
        case kLockSustain:          s.sustainLevel = g.sustain;             break;
        case kLockRelease:          s.releaseSec = g.releaseSec;            break;
        case kLockMuteGroup:        s.muteGroup = g.muteGroup;              break;
        case kLockLoop:             s.loopMode = g.loopMode;                break;
        case kLockStretch:          s.stretchEnabled = g.stretchEnabled;    break;
        case kLockReleaseTail:      s.releaseTail = g.releaseTail;          break;
        case kLockReverse:          s.reverse = g.reverse;                  break;
        case kLockOneShot:          s.oneShot = g.oneShot;                  break;
        case kLockCentsDetune:      s.centsDetune = g.centsDetune;          break;
        case kLockTonality:         s.tonalityHz = g.tonalityHz;            break;
        case kLockFormant:          s.formantSemitones = g.formantSemitones; break;
        case kLockFormantComp:      s.formantComp = g.formantComp;          break;
        case kLockGrainMode:        s.grainMode = g.grainMode;              break;
        case kLockVolume:           s.volume = g.volumeDb;                  break;
        case kLockFilterEnabled:    s.filterEnabled = g.filterEnabled;      break;
        case kLockFilterType:       s.filterType = g.filterType;            break;
        case kLockFilterSlope:      s.filterSlope = g.filterSlope;          break;
        case kLockFilterCutoff:     s.filterCutoff = g.filterCutoffHz;      break;
        case kLockFilterReso:       s.filterReso = g.filterReso;            break;
        case kLockFilterDrive:      s.filterDrive = g.filterDrive;          break;
        case kLockFilterKeyTrack:   s.filterKeyTrack = g.filterKeyTrack;    break;
        case kLockFilterEnvAttack:  s.filterEnvAttackSec = g.filterEnvAttackSec;    break;
        case kLockFilterEnvDecay:   s.filterEnvDecaySec = g.filterEnvDecaySec;      break;
        case kLockFilterEnvSustain: s.filterEnvSustain = g.filterEnvSustain;        break;
        case kLockFilterEnvRelease: s.filterEnvReleaseSec = g.filterEnvReleaseSec;  break;
        case kLockFilterEnvAmount:  s.filterEnvAmount = g.filterEnvAmount;          break;
        case kLockFilterAsym:       s.filterAsym = g.filterAsym;                  break;
        // kLockOutputBus: no global default — slice default (0) is correct.
        // kLockLoopStart / kLockLoopLength: no global default — slice defaults (0) are correct.
        default: break;
    }
}

static Slice sanitiseRestoredSlice (Slice s)
{
    s.sampleId = juce::jmax (0, s.sampleId);
    s.startInSample = juce::jmax (0, s.startInSample);
    s.endInSample = juce::jmax (s.startInSample + 1, s.endInSample);
    s.startSample = juce::jmax (0, s.startSample);
    s.endSample = juce::jmax (s.startSample + 1, s.endSample);
    if (s.endSample - s.startSample < kMinSliceLengthSamples)
        s.endSample = s.startSample + kMinSliceLengthSamples;

    s.midiNote = juce::jlimit (0, kMaxMidiNote, s.midiNote);
    s.highNote = juce::jlimit (0, kMaxMidiNote, s.highNote);
    s.sliceRootNote = juce::jlimit (0, kMaxMidiNote, s.sliceRootNote);
    if (s.highNote < s.midiNote)
        s.highNote = s.midiNote;
    s.sliceRootNote = juce::jlimit (s.midiNote, s.highNote, s.sliceRootNote);
    s.bpm = juce::jlimit (20.0f, 999.0f, s.bpm);
    s.pitchSemitones = juce::jlimit (-48.0f, 48.0f, s.pitchSemitones);
    s.algorithm = juce::jlimit (0, 2, s.algorithm);
    s.repitchMode = juce::jlimit (0, 2, s.repitchMode);
    s.attackSec = juce::jlimit (0.0f, 1.0f, s.attackSec);
    s.decaySec = juce::jlimit (0.0f, 5.0f, s.decaySec);
    s.sustainLevel = juce::jlimit (0.0f, 1.0f, s.sustainLevel);
    s.releaseSec = juce::jlimit (0.0f, 5.0f, s.releaseSec);
    s.muteGroup = juce::jlimit (0, kMaxMuteGroups, s.muteGroup);
    s.loopMode = juce::jlimit (0, 2, s.loopMode);
    s.tonalityHz = juce::jlimit (0.0f, 8000.0f, s.tonalityHz);
    s.formantSemitones = juce::jlimit (-24.0f, 24.0f, s.formantSemitones);
    s.grainMode = juce::jlimit (0, 2, s.grainMode);
    s.volume = juce::jlimit (-100.0f, 24.0f, s.volume);
    s.outputBus = juce::jlimit (0, kMaxOutputBuses - 1, s.outputBus);
    s.centsDetune = juce::jlimit (-100.0f, 100.0f, s.centsDetune);
    s.filterType = juce::jlimit (0, 3, s.filterType);
    s.filterSlope = juce::jlimit (0, 1, s.filterSlope);
    s.filterCutoff = juce::jlimit (kMinFilterCutoffHz, kMaxFilterCutoffHz, s.filterCutoff);
    s.filterReso = juce::jlimit (0.0f, 100.0f, s.filterReso);
    s.filterDrive = juce::jlimit (0.0f, 100.0f, s.filterDrive);
    s.filterAsym = juce::jlimit (0.0f, 100.0f, s.filterAsym);
    s.filterKeyTrack = juce::jlimit (0.0f, 100.0f, s.filterKeyTrack);
    s.filterEnvAttackSec = juce::jlimit (0.0f, 10.0f, s.filterEnvAttackSec);
    s.filterEnvDecaySec = juce::jlimit (0.0f, 10.0f, s.filterEnvDecaySec);
    s.filterEnvSustain = juce::jlimit (0.0f, 1.0f, s.filterEnvSustain);
    s.filterEnvReleaseSec = juce::jlimit (0.0f, 10.0f, s.filterEnvReleaseSec);
    s.filterEnvAmount = juce::jlimit (-96.0f, 96.0f, s.filterEnvAmount);
    s.crossfadePct = juce::jlimit (0.0f, 100.0f, s.crossfadePct);
    {
        const int sliceLen = juce::jmax (0, s.endSample - s.startSample);
        s.loopStartOffset = juce::jlimit (0, juce::jmax (0, sliceLen - 1), s.loopStartOffset);
        if (s.loopLength > 0)
            s.loopLength = juce::jlimit (1, juce::jmax (1, sliceLen - s.loopStartOffset), s.loopLength);
    }
    s.lockMask &= kValidLockMask;
    return s;
}

constexpr int kCurrentStateVersion = 24;
constexpr int kStateExtensionMagic = 0x52504D31; // "RPM1"
constexpr std::array<double, 6> kLegacyCommonSampleRates { 44100.0, 48000.0, 88200.0,
                                                           96000.0, 176400.0, 192000.0 };

static bool isCoalescableCommand (IntersectProcessor::CommandType type)
{
    return type == IntersectProcessor::CmdSetSliceParam
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
        case IntersectProcessor::CmdSetSliceBounds:
        case IntersectProcessor::CmdRepackMidi:
        case IntersectProcessor::CmdFileLoadCompleted:
        case IntersectProcessor::CmdFileLoadFailed:
        case IntersectProcessor::CmdBeginGesture:
            return false;

        case IntersectProcessor::CmdLoadFile:
        case IntersectProcessor::CmdAppendFiles:
        case IntersectProcessor::CmdCreateSlice:
        case IntersectProcessor::CmdDeleteSlice:
        case IntersectProcessor::CmdDeleteSessionSample:
        case IntersectProcessor::CmdDuplicateSlice:
        case IntersectProcessor::CmdSplitSlice:
        case IntersectProcessor::CmdTransientChop:
        case IntersectProcessor::CmdRelinkFile:
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

PreviewStretchParams makePreviewStretchParams (const GlobalParamSnapshot& globals,
                                               float dawBpm,
                                               double sampleRate,
                                               const SampleData* sample)
{
    PreviewStretchParams params;
    params.stretchEnabled = globals.stretchEnabled;
    params.algorithm = globals.algorithm;
    params.repitchMode = globals.repitchMode;
    params.bpm = globals.bpm;
    params.pitch = globals.pitchSemitones;
    params.dawBpm = dawBpm;
    params.tonality = globals.tonalityHz;
    params.formant = globals.formantSemitones;
    params.formantComp = globals.formantComp;
    params.grainMode = globals.grainMode;
    params.sampleRate = sampleRate;
    params.sample = sample;
    return params;
}

VoiceStartParams makeVoiceStartParams (const GlobalParamSnapshot& globals,
                                       int note,
                                       float velocity,
                                       float dawBpm)
{
    VoiceStartParams params;
    params.note = note;
    params.velocity = velocity;
    params.globalBpm = globals.bpm;
    params.globalPitch = globals.pitchSemitones;
    params.globalAlgorithm = globals.algorithm;
    params.globalRepitchMode = globals.repitchMode;
    params.globalAttackSec = globals.attackSec;
    params.globalDecaySec = globals.decaySec;
    params.globalSustain = globals.sustain;
    params.globalReleaseSec = globals.releaseSec;
    params.globalMuteGroup = globals.muteGroup;
    params.globalStretch = globals.stretchEnabled;
    params.dawBpm = dawBpm;
    params.globalTonality = globals.tonalityHz;
    params.globalFormant = globals.formantSemitones;
    params.globalFormantComp = globals.formantComp;
    params.globalGrainMode = globals.grainMode;
    params.globalVolume = globals.volumeDb;
    params.globalReleaseTail = globals.releaseTail;
    params.globalReverse = globals.reverse;
    params.globalLoopMode = globals.loopMode;
    params.globalOneShot = globals.oneShot;
    params.globalCentsDetune = globals.centsDetune;
    params.globalFilterEnabled = globals.filterEnabled;
    params.globalFilterType = globals.filterType;
    params.globalFilterSlope = globals.filterSlope;
    params.globalFilterCutoff = globals.filterCutoffHz;
    params.globalFilterReso = globals.filterReso;
    params.globalFilterDrive = globals.filterDrive;
    params.globalFilterAsym = globals.filterAsym;
    params.globalFilterKeyTrack = globals.filterKeyTrack;
    params.globalFilterEnvAttackSec = globals.filterEnvAttackSec;
    params.globalFilterEnvDecaySec = globals.filterEnvDecaySec;
    params.globalFilterEnvSustain = globals.filterEnvSustain;
    params.globalFilterEnvReleaseSec = globals.filterEnvReleaseSec;
    params.globalFilterEnvAmount = globals.filterEnvAmount;
    params.globalCrossfadePct = globals.crossfadePct;
    params.rootNote = globals.rootNote;
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
    masterVolParam = apvts.getRawParameterValue (ParamIds::masterVolume);
    bpmParam       = apvts.getRawParameterValue (ParamIds::defaultBpm);
    pitchParam     = apvts.getRawParameterValue (ParamIds::defaultPitch);
    algoParam      = apvts.getRawParameterValue (ParamIds::defaultAlgorithm);
    repitchModeParam = apvts.getRawParameterValue (ParamIds::defaultRepitchMode);
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
    filterAsymParam = apvts.getRawParameterValue (ParamIds::defaultFilterAsym);
    filterKeyTrackParam = apvts.getRawParameterValue (ParamIds::defaultFilterKeyTrack);
	    filterEnvAttackParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnvAttack);
	    filterEnvDecayParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnvDecay);
	    filterEnvSustainParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnvSustain);
	    filterEnvReleaseParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnvRelease);
	    filterEnvAmountParam = apvts.getRawParameterValue (ParamIds::defaultFilterEnvAmount);
	    uiScaleParam = apvts.getRawParameterValue (ParamIds::uiScale);
	    publishUiSliceSnapshot();
}

IntersectProcessor::~IntersectProcessor()
{
	    fileLoadPool.removeAllJobs (true, 5000);
	    auto* pending = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
	    delete pending;
	    auto* failed = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
	    delete failed;
	    auto* stemPending = pendingStemImport.exchange (nullptr, std::memory_order_acq_rel);
	    delete stemPending;
}

GlobalParamSnapshot IntersectProcessor::loadGlobalParamSnapshot() const
{
    return GlobalParamSnapshot::loadFrom (apvts, sliceManager.rootNote.load());
}

void IntersectProcessor::setStandaloneTransportBpm (float newBpm) noexcept
{
    standaloneTransportBpm.store (juce::jlimit (20.0f, 999.0f, newBpm), std::memory_order_relaxed);
}

float IntersectProcessor::getStandaloneTransportBpm() const noexcept
{
    return standaloneTransportBpm.load (std::memory_order_relaxed);
}

void IntersectProcessor::setMissingFileInfo (const RtText<512>& fileName, const RtText<4096>& filePath)
{
    const int writeIndex = 1 - missingFileInfoIndex.load (std::memory_order_relaxed);
    missingFileInfos[(size_t) writeIndex].fileName = fileName;
    missingFileInfos[(size_t) writeIndex].filePath = filePath;
    missingFileInfoIndex.store (writeIndex, std::memory_order_release);
}

void IntersectProcessor::clearMissingFileInfo()
{
    const int writeIndex = 1 - missingFileInfoIndex.load (std::memory_order_relaxed);
    missingFileInfos[(size_t) writeIndex] = {};
    missingFileInfoIndex.store (writeIndex, std::memory_order_release);
}

const IntersectProcessor::MissingFileInfo& IntersectProcessor::getMissingFileInfo() const
{
    return missingFileInfos[(size_t) missingFileInfoIndex.load (std::memory_order_acquire)];
}

void IntersectProcessor::setUiStatusMessage (const juce::String& text, bool isWarning,
                                             UiStatusMessage::Source source)
{
    RtText<256> rtText;
    rtText.assign (text);
    setUiStatusMessage (rtText, isWarning, source);
}

void IntersectProcessor::setUiStatusMessage (const RtText<256>& text,
                                             bool isWarning,
                                             UiStatusMessage::Source source)
{
    const int writeIndex = 1 - uiStatusMessageIndex.load (std::memory_order_relaxed);
    auto& status = uiStatusMessages[(size_t) writeIndex];
    status.text = text;
    status.isWarning = isWarning;
    status.source = source;
    uiStatusMessageIndex.store (writeIndex, std::memory_order_release);
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
    RtText<256> text;
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

void IntersectProcessor::setPendingStateFiles (const std::vector<juce::File>& files)
{
    const juce::ScopedLock sl (pendingStateFileLock);
    pendingStateFiles = files;
}

void IntersectProcessor::clearPendingStateFiles()
{
    const juce::ScopedLock sl (pendingStateFileLock);
    pendingStateFiles.clear();
}

std::vector<juce::File> IntersectProcessor::getPendingStateFiles() const
{
    const juce::ScopedLock sl (pendingStateFileLock);
    return pendingStateFiles;
}

ParamUndoState IntersectProcessor::captureParamUndoState() const
{
    const auto load = [] (const std::atomic<float>* param, float fallback)
    {
        return param != nullptr ? param->load (std::memory_order_relaxed) : fallback;
    };

    ParamUndoState state;
    state.masterVolume = load (masterVolParam, state.masterVolume);
    state.defaultBpm = load (bpmParam, state.defaultBpm);
    state.defaultPitch = load (pitchParam, state.defaultPitch);
    state.defaultAlgorithm = load (algoParam, state.defaultAlgorithm);
    state.defaultRepitchMode = load (repitchModeParam, state.defaultRepitchMode);
    state.defaultAttack = load (attackParam, state.defaultAttack);
    state.defaultDecay = load (decayParam, state.defaultDecay);
    state.defaultSustain = load (sustainParam, state.defaultSustain);
    state.defaultRelease = load (releaseParam, state.defaultRelease);
    state.defaultMuteGroup = load (muteGroupParam, state.defaultMuteGroup);
    state.defaultLoop = load (loopParam, state.defaultLoop);
    state.defaultStretchEnabled = load (stretchParam, state.defaultStretchEnabled);
    state.defaultTonality = load (tonalityParam, state.defaultTonality);
    state.defaultFormant = load (formantParam, state.defaultFormant);
    state.defaultFormantComp = load (formantCompParam, state.defaultFormantComp);
    state.defaultGrainMode = load (grainModeParam, state.defaultGrainMode);
    state.defaultReleaseTail = load (releaseTailParam, state.defaultReleaseTail);
    state.defaultReverse = load (reverseParam, state.defaultReverse);
    state.defaultOneShot = load (oneShotParam, state.defaultOneShot);
    state.defaultCentsDetune = load (centsDetuneParam, state.defaultCentsDetune);
    state.defaultFilterEnabled = load (filterEnabledParam, state.defaultFilterEnabled);
    state.defaultFilterType = load (filterTypeParam, state.defaultFilterType);
    state.defaultFilterSlope = load (filterSlopeParam, state.defaultFilterSlope);
    state.defaultFilterCutoff = load (filterCutoffParam, state.defaultFilterCutoff);
    state.defaultFilterReso = load (filterResoParam, state.defaultFilterReso);
    state.defaultFilterDrive = load (filterDriveParam, state.defaultFilterDrive);
    state.defaultFilterAsym = load (filterAsymParam, state.defaultFilterAsym);
    state.defaultFilterKeyTrack = load (filterKeyTrackParam, state.defaultFilterKeyTrack);
    state.defaultFilterEnvAttack = load (filterEnvAttackParam, state.defaultFilterEnvAttack);
    state.defaultFilterEnvDecay = load (filterEnvDecayParam, state.defaultFilterEnvDecay);
    state.defaultFilterEnvSustain = load (filterEnvSustainParam, state.defaultFilterEnvSustain);
    state.defaultFilterEnvRelease = load (filterEnvReleaseParam, state.defaultFilterEnvRelease);
    state.defaultFilterEnvAmount = load (filterEnvAmountParam, state.defaultFilterEnvAmount);
    state.maxVoices = load (maxVoicesParam, state.maxVoices);
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
    const auto apply = [this] (const juce::String& paramId, float value)
    {
        if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (paramId)))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    apply (ParamIds::masterVolume, state.masterVolume);
    apply (ParamIds::defaultBpm, state.defaultBpm);
    apply (ParamIds::defaultPitch, state.defaultPitch);
    apply (ParamIds::defaultAlgorithm, state.defaultAlgorithm);
    apply (ParamIds::defaultRepitchMode, state.defaultRepitchMode);
    apply (ParamIds::defaultAttack, state.defaultAttack);
    apply (ParamIds::defaultDecay, state.defaultDecay);
    apply (ParamIds::defaultSustain, state.defaultSustain);
    apply (ParamIds::defaultRelease, state.defaultRelease);
    apply (ParamIds::defaultMuteGroup, state.defaultMuteGroup);
    apply (ParamIds::defaultLoop, state.defaultLoop);
    apply (ParamIds::defaultStretchEnabled, state.defaultStretchEnabled);
    apply (ParamIds::defaultTonality, state.defaultTonality);
    apply (ParamIds::defaultFormant, state.defaultFormant);
    apply (ParamIds::defaultFormantComp, state.defaultFormantComp);
    apply (ParamIds::defaultGrainMode, state.defaultGrainMode);
    apply (ParamIds::defaultReleaseTail, state.defaultReleaseTail);
    apply (ParamIds::defaultReverse, state.defaultReverse);
    apply (ParamIds::defaultOneShot, state.defaultOneShot);
    apply (ParamIds::defaultCentsDetune, state.defaultCentsDetune);
    apply (ParamIds::defaultFilterEnabled, state.defaultFilterEnabled);
    apply (ParamIds::defaultFilterType, state.defaultFilterType);
    apply (ParamIds::defaultFilterSlope, state.defaultFilterSlope);
    apply (ParamIds::defaultFilterCutoff, state.defaultFilterCutoff);
    apply (ParamIds::defaultFilterReso, state.defaultFilterReso);
    apply (ParamIds::defaultFilterDrive, state.defaultFilterDrive);
    apply (ParamIds::defaultFilterAsym, state.defaultFilterAsym);
    apply (ParamIds::defaultFilterKeyTrack, state.defaultFilterKeyTrack);
    apply (ParamIds::defaultFilterEnvAttack, state.defaultFilterEnvAttack);
    apply (ParamIds::defaultFilterEnvDecay, state.defaultFilterEnvDecay);
    apply (ParamIds::defaultFilterEnvSustain, state.defaultFilterEnvSustain);
    apply (ParamIds::defaultFilterEnvRelease, state.defaultFilterEnvRelease);
    apply (ParamIds::defaultFilterEnvAmount, state.defaultFilterEnvAmount);
    apply (ParamIds::maxVoices, state.maxVoices);
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
        requestSampleLoad (files, LoadKindRelink, &sampleIds);
    }
}

void IntersectProcessor::releaseResources() {}

int IntersectProcessor::requestSampleLoad (const std::vector<juce::File>& files, LoadKind kind,
                                           const std::vector<int>* sampleIds)
{
    const int token = nextLoadToken.fetch_add (1, std::memory_order_relaxed) + 1;
    latestLoadToken.store (token, std::memory_order_release);
    latestLoadKind.store ((int) kind, std::memory_order_release);

    if (kind == LoadKindReplace)
        clearPendingSliceTimelineRemap();
    else if (pendingSliceTimelineRemap.active.load (std::memory_order_acquire))
        pendingSliceTimelineRemap.expectedLoadToken.store (token, std::memory_order_release);

    // Keep only the latest completed decode payload.
    auto* oldDecoded = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
    delete oldDecoded;
    auto* oldFailure = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
    delete oldFailure;

    if (files.empty())
        return token;

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
        payload->fileName.assign (failedFile.getFileName());
        payload->filePath.assign (failedFile.getFullPathName());
        auto* old = completedLoadFailure.exchange (payload, std::memory_order_acq_rel);
        delete old;
    };

    fileLoadPool.addJob (new SampleDecodeJob (files, copiedSampleIds, sr, token, kind, onSuccess, onFailure), true);
    return token;
}

void IntersectProcessor::loadFileAsync (const juce::File& file)
{
    loadFilesAsync (std::vector<juce::File> { file }, false);
}

void IntersectProcessor::loadFilesAsync (const std::vector<juce::File>& files, bool append)
{
    pendingStateRestoreToken.store (0, std::memory_order_release);
    if (files.empty())
        return;

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
        for (const auto& file : files)
        {
            orderedFiles.push_back (file);
            sampleIds.push_back (generateSessionSampleId());
        }
        setPendingStateFile (orderedFiles.front());
        setPendingStateFiles (orderedFiles);
        requestSampleLoad (orderedFiles, LoadKindPreserveSlices, &sampleIds);
        return;
    }

    orderedFiles = files;
    setPendingStateFile (orderedFiles.front());
    setPendingStateFiles (orderedFiles);
    requestSampleLoad (orderedFiles, LoadKindReplace);
}

void IntersectProcessor::relinkFileAsync (const juce::File& file)
{
    pendingStateRestoreToken.store (0, std::memory_order_release);
    requestSampleLoad (std::vector<juce::File> { file }, LoadKindRelink);
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

bool IntersectProcessor::isStemModelInstalled (StemModelId modelId) const
{
    return resolveStemModelFile (getResolvedStemModelFolder(), modelId).existsAsFile();
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

void IntersectProcessor::cancelStemModelDownload()
{
    stemModelDownloadJob.cancel();
}

void IntersectProcessor::cancelStemSeparation()
{
    if (stemJob.getState() != StemJobState::idle)
        stemJob.cancel();
}

void IntersectProcessor::startStemSeparation (int sampleId,
                                              StemModelId modelId,
                                              StemSelectionMask stemSelectionMask,
                                              const juce::File& outputFolder)
{
    if (stemJob.getState() != StemJobState::idle)
    {
        setUiStatusMessage ("Stem separation already in progress", true);
        return;
    }

    const auto modelFolder = getResolvedStemModelFolder();
    const auto catalogEntry = getEffectiveStemModelCatalogEntry (modelId, modelFolder);
    const auto modelFile = modelFolder.getChildFile (catalogEntry.fileName);
    if (! modelFile.existsAsFile())
    {
        setUiStatusMessage ("Selected stem model is not installed", true);
        return;
    }

    if (stemSelectionMask == 0)
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
    auto jobDir = outputRoot.getChildFile (sourceName + "_" + stemModelIdToString (modelId) + "_" + timestamp);

    stemJob.start (regionAudio, sampleSnap->decodedSampleRate, sampleId,
                   sourceName, modelId, catalogEntry, stemSelectionMask, stemComputeDevice, modelFile, jobDir);
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

    const double ratio = (double) targetFrames / (double) sourceFrames;
    const bool needsRemap = std::abs (ratio - 1.0) > 0.0001
        || (sourceRate > 0.0 && targetRate > 0.0 && std::abs (sourceRate - targetRate) > 0.01);

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

void IntersectProcessor::syncSliceOwnershipFromAbsolute (Slice& slice, bool clampToSessionBounds)
{
    const auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap == nullptr || sampleSnap->sessionSamples.empty())
        return;

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
    slice.startInSample = startInSample;
    slice.endInSample = endInSample;
    slice.startSample = owner->startFrame + startInSample;
    slice.endSample = owner->startFrame + endInSample;

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
    const auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap == nullptr || sampleSnap->sessionSamples.empty())
        return;

    int removeIndex = -1;
    for (int i = 0; i < (int) sampleSnap->sessionSamples.size(); ++i)
    {
        if (sampleSnap->sessionSamples[(size_t) i].sampleId == sampleId)
        {
            removeIndex = i;
            break;
        }
    }
    if (removeIndex < 0)
        return;

    if (sampleSnap->sessionSamples.size() == 1)
    {
        clearVoicesBeforeSampleSwap();
        sampleData.clear();
        sliceManager.clearAll();
        sampleMissing.store (false, std::memory_order_relaxed);
        sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
        clearMissingFileInfo();
        clearPendingStateFiles();
        selectedSessionSampleId.store (-1, std::memory_order_relaxed);
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }

    std::vector<SampleData::SessionSample> remainingSamples;
    remainingSamples.reserve (sampleSnap->sessionSamples.size() - 1);
    for (int i = 0; i < (int) sampleSnap->sessionSamples.size(); ++i)
    {
        if (i != removeIndex)
            remainingSamples.push_back (sampleSnap->sessionSamples[(size_t) i]);
    }

    const int nextSelectedSampleId = removeIndex < (int) remainingSamples.size()
        ? remainingSamples[(size_t) removeIndex].sampleId
        : remainingSamples.back().sampleId;

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

    auto rebuilt = SampleData::rebuildWithSessionSamples (*sampleSnap, remainingSamples);
    clearVoicesBeforeSampleSwap();
    sampleData.applyDecodedSample (std::move (rebuilt));
    sampleMissing.store (false, std::memory_order_relaxed);
    sampleAvailability.store ((int) SampleStateLoaded, std::memory_order_relaxed);
    clearMissingFileInfo();
    selectedSessionSampleId.store (nextSelectedSampleId, std::memory_order_relaxed);
    syncAllSliceAbsolutePositions();
    clampSlicesToSampleBounds();
    sliceManager.rebuildMidiMap();
    uiSnapshotDirty.store (true, std::memory_order_release);
}

void IntersectProcessor::publishUiSliceSnapshot()
{
    const int writeIndex = 1 - uiSliceSnapshotIndex.load (std::memory_order_relaxed);
    auto& snap = uiSliceSnapshots[(size_t) writeIndex];
    auto sampleSnap = sampleData.getSnapshot();
    const auto& missingInfo = getMissingFileInfo();
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
            snap.sampleFileName.assign (juce::String (snap.numSessionSamples) + " samples loaded");
        else
            snap.sampleFileName.assign (sampleSnap->fileName);
    }
    else if (snap.sampleMissing)
        snap.sampleFileName = missingInfo.fileName;
    else
        snap.sampleFileName.clear();

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
    std::vector<juce::File> files;
    files.reserve ((size_t) snap.numSessionSamples);
    std::vector<int> sampleIds;
    sampleIds.reserve ((size_t) snap.numSessionSamples);
    for (int i = 0; i < snap.numSessionSamples; ++i)
    {
        const auto& sample = snap.sessionSamples[(size_t) i];
        if (sample.filePath.isNotEmpty())
        {
            files.emplace_back (sample.filePath);
            sampleIds.push_back (sample.sampleId);
        }
    }

    // Apply slice state immediately (safe — no allocation).
    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
        sliceManager.getSlice (i) = snap.slices[(size_t) i];
    sliceManager.setNumSlices (snap.numSlices);
    sliceManager.selectedSlice = snap.selectedSlice;
    sliceManager.rootNote.store (snap.rootNote);
    selectedSessionSampleId.store (snap.selectedSessionSampleId, std::memory_order_relaxed);
    midiSelectsSlice.store (snap.midiSelectsSlice);
    snapToZeroCrossing.store (snap.snapToZeroCrossing);

    if (! files.empty())
    {
        syncAllSliceAbsolutePositions();
        sliceManager.rebuildMidiMap();
        pendingStateRestoreToken.store (0, std::memory_order_release);
        setPendingStateFile (files.front());
        setPendingStateFiles (files);
        requestSampleLoad (files, LoadKindPreserveSlices, &sampleIds);
    }
    else
    {
        clearVoicesBeforeSampleSwap();
        sampleData.clear();
        sampleMissing.store (false, std::memory_order_relaxed);
        sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
        clearMissingFileInfo();
        clearPendingStateFile();
        clearPendingStateFiles();
        sliceManager.rebuildMidiMap();
    }

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
        case CmdLoadFile:
        case CmdAppendFiles:
        case CmdLazyChopStart:
        case CmdLazyChopStop:
        case CmdRelinkFile:
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
        case CmdLoadFile:
            if (! cmd.filesParam.empty())
                loadFilesAsync (cmd.filesParam, false);
            else
                loadFileAsync (cmd.fileParam);
            break;

        case CmdAppendFiles:
            loadFilesAsync (cmd.filesParam, true);
            break;

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
                const auto globals = loadGlobalParamSnapshot();
                const auto psp = makePreviewStretchParams (globals, dawBpm.load(), currentSampleRate, &sampleData);
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

        case CmdToggleLock:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                const auto globals = loadGlobalParamSnapshot();
                auto& s = sliceManager.getSlice (sel);
                uint64_t bit = cmd.lockBitParam;
                bool turningOn = !(s.lockMask & bit);

                if (turningOn)
                    copyGlobalToSlice (s, globals, bit);

                s.lockMask ^= bit;
            }
            break;
        }

        case CmdSetSliceParam:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                const auto globals = loadGlobalParamSnapshot();
                auto& s = sliceManager.getSlice (sel);
                int field = cmd.intParam1;
                float val = cmd.floatParam1;
                constexpr float kCompareTolerance = 1.0e-4f;

                auto setFloatField = [&s, kCompareTolerance] (float& target, float newValue, float globalValue, uint64_t lockBit)
                {
                    target = newValue;
                    if (std::abs (target - globalValue) <= kCompareTolerance)
                        s.lockMask &= ~lockBit;
                    else
                        s.lockMask |= lockBit;
                };

                auto setIntField = [&s] (int& target, int newValue, int globalValue, uint64_t lockBit)
                {
                    target = newValue;
                    if (target == globalValue)
                        s.lockMask &= ~lockBit;
                    else
                        s.lockMask |= lockBit;
                };

                auto setBoolField = [&s] (bool& target, bool newValue, bool globalValue, uint64_t lockBit)
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
                        setFloatField (s.bpm, val, globals.bpm, kLockBpm);
                        break;
                    case FieldPitch:
                        setFloatField (s.pitchSemitones, val, globals.pitchSemitones, kLockPitch);
                        break;
                    case FieldAlgorithm:
                        setIntField (s.algorithm, (int) val, globals.algorithm, kLockAlgorithm);
                        break;
                    case FieldRepitchMode:
                        setIntField (s.repitchMode, (int) val, globals.repitchMode, kLockRepitchMode);
                        break;
                    case FieldAttack:
                        setFloatField (s.attackSec, val, globals.attackSec, kLockAttack);
                        break;
                    case FieldDecay:
                        setFloatField (s.decaySec, val, globals.decaySec, kLockDecay);
                        break;
                    case FieldSustain:
                        setFloatField (s.sustainLevel, val, globals.sustain, kLockSustain);
                        break;
                    case FieldRelease:
                        setFloatField (s.releaseSec, val, globals.releaseSec, kLockRelease);
                        break;
                    case FieldMuteGroup:
                        setIntField (s.muteGroup, (int) val, globals.muteGroup, kLockMuteGroup);
                        break;
                    case FieldStretchEnabled:
                        setBoolField (s.stretchEnabled, val > 0.5f, globals.stretchEnabled, kLockStretch);
                        break;
                    case FieldTonality:
                        setFloatField (s.tonalityHz, val, globals.tonalityHz, kLockTonality);
                        break;
                    case FieldFormant:
                        setFloatField (s.formantSemitones, val, globals.formantSemitones, kLockFormant);
                        break;
                    case FieldFormantComp:
                        setBoolField (s.formantComp, val > 0.5f, globals.formantComp, kLockFormantComp);
                        break;
                    case FieldGrainMode:
                        setIntField (s.grainMode, (int) val, globals.grainMode, kLockGrainMode);
                        break;
                    case FieldVolume:
                        setFloatField (s.volume, val, globals.volumeDb, kLockVolume);
                        break;
                    case FieldReleaseTail:
                        setBoolField (s.releaseTail, val > 0.5f, globals.releaseTail, kLockReleaseTail);
                        break;
                    case FieldReverse:
                        setBoolField (s.reverse, val > 0.5f, globals.reverse, kLockReverse);
                        break;
                    case FieldOutputBus:
                        s.outputBus = juce::jlimit (0, kMaxOutputBuses - 1, (int) val);
                        s.lockMask |= kLockOutputBus;
                        break;
                    case FieldLoop:
                        setIntField (s.loopMode, (int) val, globals.loopMode, kLockLoop);
                        break;
                    case FieldOneShot:
                        setBoolField (s.oneShot, val > 0.5f, globals.oneShot, kLockOneShot);
                        break;
                    case FieldCentsDetune:
                        setFloatField (s.centsDetune, val, globals.centsDetune, kLockCentsDetune);
                        break;
                    case FieldFilterEnabled:
                        setBoolField (s.filterEnabled, val > 0.5f, globals.filterEnabled, kLockFilterEnabled);
                        break;
                    case FieldFilterType:
                        setIntField (s.filterType, juce::jlimit (0, 3, (int) val), globals.filterType, kLockFilterType);
                        break;
                    case FieldFilterSlope:
                        setIntField (s.filterSlope, juce::jlimit (0, 1, (int) val), globals.filterSlope, kLockFilterSlope);
                        break;
                    case FieldFilterCutoff:
                        setFloatField (s.filterCutoff, val, globals.filterCutoffHz, kLockFilterCutoff);
                        break;
                    case FieldFilterReso:
                        setFloatField (s.filterReso, val, globals.filterReso, kLockFilterReso);
                        break;
                    case FieldFilterDrive:
                        setFloatField (s.filterDrive, val, globals.filterDrive, kLockFilterDrive);
                        break;
                    case FieldFilterKeyTrack:
                        setFloatField (s.filterKeyTrack, val, globals.filterKeyTrack, kLockFilterKeyTrack);
                        break;
                    case FieldFilterEnvAttack:
                        setFloatField (s.filterEnvAttackSec, val, globals.filterEnvAttackSec, kLockFilterEnvAttack);
                        break;
                    case FieldFilterEnvDecay:
                        setFloatField (s.filterEnvDecaySec, val, globals.filterEnvDecaySec, kLockFilterEnvDecay);
                        break;
                    case FieldFilterEnvSustain:
                        setFloatField (s.filterEnvSustain, val, globals.filterEnvSustain, kLockFilterEnvSustain);
                        break;
                    case FieldFilterEnvRelease:
                        setFloatField (s.filterEnvReleaseSec, val, globals.filterEnvReleaseSec, kLockFilterEnvRelease);
                        break;
                    case FieldFilterEnvAmount:
                        setFloatField (s.filterEnvAmount, val, globals.filterEnvAmount, kLockFilterEnvAmount);
                        break;
                    case FieldFilterAsym:
                        setFloatField (s.filterAsym, val, globals.filterAsym, kLockFilterAsym);
                        break;
                    case FieldCrossfade:
                        s.crossfadePct = juce::jlimit (0.0f, 100.0f, val);
                        s.lockMask |= kLockCrossfade;
                        break;
                    case FieldLoopStart:
                        s.loopStartOffset = juce::jmax (0, (int) val);
                        s.lockMask |= kLockLoopStart;
                        break;
                    case FieldLoopLength:
                        s.loopLength = juce::jmax (0, (int) val);
                        s.lockMask |= kLockLoopLength;
                        break;
                    case FieldMidiNote:
                    {
                        const bool wasSingleNote = (s.highNote == s.midiNote);
                        s.midiNote = juce::jlimit (0, kMaxMidiNote, (int) val);

                        if (wasSingleNote)
                        {
                            s.highNote = s.midiNote;
                            s.sliceRootNote = s.midiNote;
                        }
                        else
                        {
                            if (s.highNote < s.midiNote)
                                s.highNote = s.midiNote;
                            s.sliceRootNote = juce::jlimit (s.midiNote, s.highNote, s.sliceRootNote);
                        }

                        sliceManager.rebuildMidiMap();
                        break;
                    }
                    case FieldHighNote:
                    {
                        s.highNote = juce::jlimit (s.midiNote, kMaxMidiNote, (int) val);
                        s.sliceRootNote = juce::jlimit (s.midiNote, s.highNote, s.sliceRootNote);
                        sliceManager.rebuildMidiMap();
                        break;
                    }
                    case FieldSliceRootNote:
                        s.sliceRootNote = juce::jlimit (s.midiNote, s.highNote, (int) val);
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

        case CmdRelinkFile:
            relinkFileAsync (cmd.fileParam);
            break;

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
            lazyChop.stop (voicePool, sliceManager);
            std::fill (std::begin (heldNotes), std::end (heldNotes), false);
            break;

        case CmdSelectSlice:
        {
            const int selected = juce::jlimit (-1, juce::jmax (-1, sliceManager.getNumSlices() - 1), cmd.intParam1);
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
                const auto noteIndex = static_cast<size_t> (note);
                heldNotes[noteIndex] = true;

                // Build params once; all param loads happen here, not inside the slice loop.
                const auto globals = loadGlobalParamSnapshot();
                auto p = makeVoiceStartParams (globals, note, velocity, dawBpm.load());

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

                    // Handle mute groups
                    const auto& s = sliceManager.getSlice (sliceIdx);
                    int mg = (int) sliceManager.resolveParam (sliceIdx, kLockMuteGroup,
                                                              (float) s.muteGroup, (float) p.globalMuteGroup);
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
#if JUCE_DEBUG
    if (std::isfinite (x))
        jassert (std::abs (x) < 10.0f);
#endif

    if (! std::isfinite (x)) return 0.0f;
    return juce::jlimit (-1.0f, 1.0f, x);
}

void IntersectProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
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
            const auto globals = loadGlobalParamSnapshot();
            const auto psp = makePreviewStretchParams (globals, dawBpm.load(), currentSampleRate, &sampleData);
            voicePool.startShiftPreview (req, sampleData.getNumFrames(), psp);
        }
    }

    bool loadStateChanged = false;
    {
        auto* rawDecoded = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
        if (rawDecoded != nullptr)
        {
            std::unique_ptr<SampleData::DecodedSample> decoded (rawDecoded);
            const int currentToken = latestLoadToken.load (std::memory_order_acquire);
            const auto currentLoadKind = (LoadKind) latestLoadKind.load (std::memory_order_acquire);
            const bool isStateRestoreLoad = pendingStateRestoreToken.load (std::memory_order_acquire) == currentToken;

            const bool needsSampleRateRetry = decoded->filePath.isNotEmpty()
                && currentSampleRate > 0.0
                && decoded->decodedSampleRate > 0.0
                && std::abs (decoded->decodedSampleRate - currentSampleRate) > 0.01;

            if (needsSampleRateRetry)
            {
                std::vector<juce::File> files;
                std::vector<int> sampleIds;
                for (const auto& sample : decoded->sessionSamples)
                {
                    files.emplace_back (sample.filePath);
                    sampleIds.push_back (sample.sampleId);
                }
                const int retryToken = requestSampleLoad (files, currentLoadKind, &sampleIds);
                if (isStateRestoreLoad)
                    pendingStateRestoreToken.store (retryToken, std::memory_order_release);
            }
            else
            {
                clearVoicesBeforeSampleSwap();
                sampleData.applyDecodedSample (std::move (decoded));
                sampleMissing.store (false);
                clearMissingFileInfo();
                clearPendingStateFiles();
                sampleAvailability.store ((int) SampleStateLoaded, std::memory_order_relaxed);
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
                    auto* pending = pendingStemImport.exchange (nullptr, std::memory_order_acq_rel);
                    if (pending != nullptr)
                    {
                        const auto& sessionSamples = sampleData.getSessionSamples();
                        const auto numNewStems = (int) pending->roles.size();
                        const int totalSamples = (int) sessionSamples.size();
                        const int firstStemIdx = totalSamples - numNewStems;
                        for (int i = 0; i < numNewStems && firstStemIdx + i < totalSamples; ++i)
                        {
                            StemMetadata meta;
                            meta.parentSourceSampleId = pending->parentSourceSampleId;
                            meta.role = pending->roles[(size_t) i];
                            meta.isGenerated = true;
                            setStemMeta (sessionSamples[(size_t) (firstStemIdx + i)].sampleId, meta);
                        }
                        delete pending;
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
                    sliceManager.rebuildMidiMap();
                }

                if (isStateRestoreLoad)
                    pendingStateRestoreToken.store (0, std::memory_order_release);

                loadStateChanged = true;
                uiSnapshotDirty.store (true, std::memory_order_release);
            }
        }
    }

    {
        auto* rawFailure = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
        if (rawFailure != nullptr)
        {
            std::unique_ptr<FailedLoadResult> failed (rawFailure);
            const bool isStateRestoreLoad = pendingStateRestoreToken.load (std::memory_order_acquire) == failed->token;
            if (failed->token == latestLoadToken.load (std::memory_order_acquire)
                && failed->kind == LoadKindRelink)
            {
                sampleMissing.store (true);
                setMissingFileInfo (failed->fileName, failed->filePath);
                sampleAvailability.store ((int) SampleStateMissingAwaitingRelink,
                                         std::memory_order_relaxed);

                if (isStateRestoreLoad)
                    pendingStateRestoreToken.store (0, std::memory_order_release);

                loadStateChanged = true;
                uiSnapshotDirty.store (true, std::memory_order_release);
            }
        }
    }

    // Poll model downloads for completion
    {
        const auto downloadState = stemModelDownloadJob.getState();
        if (downloadState == StemModelDownloadState::completed)
        {
            setUiStatusMessage (stemModelDownloadJob.consumeResultMessage(), false);
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
        else if (downloadState == StemModelDownloadState::failed)
        {
            setUiStatusMessage (stemModelDownloadJob.consumeResultMessage(), true);
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
        else if (downloadState == StemModelDownloadState::cancelled)
        {
            setUiStatusMessage (stemModelDownloadJob.consumeResultMessage(), true);
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }

    // Poll stem separation job for completion
    {
        const auto stemState = stemJob.getState();
        if (stemState == StemJobState::completed)
        {
            const int sourceSampleId = stemJob.getSourceSampleId();
            auto stemResult = stemJob.consumeResult();
            const auto fallbackWarning = stemResult.warningMessage;
            if (! stemResult.stemFiles.empty())
            {
                // Store pending stem metadata for application after load completes
                auto* pending = new PendingStemImport();
                pending->roles = std::move (stemResult.stemRoles);
                pending->parentSourceSampleId = sourceSampleId;
                auto* old = pendingStemImport.exchange (pending, std::memory_order_acq_rel);
                delete old;

                loadFilesAsync (stemResult.stemFiles, true);
                setUiStatusMessage (fallbackWarning.isNotEmpty() ? fallbackWarning
                                                                : juce::String ("Stems imported"),
                                    fallbackWarning.isNotEmpty());
            }
            loadStateChanged = true;
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
        else if (stemState == StemJobState::cancelled)
        {
            (void) stemJob.consumeResult();
            setUiStatusMessage ("Stem separation cancelled", true);
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
        else if (stemState == StemJobState::failed)
        {
            auto stemResult = stemJob.consumeResult();
            setUiStatusMessage (stemResult.errorMessage.isNotEmpty()
                                    ? stemResult.errorMessage
                                    : juce::String ("Stem separation failed"),
                                true);
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

    processMidi (midi);

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

    if (stemJob.getState() != StemJobState::idle
        || stemModelDownloadJob.getState() == StemModelDownloadState::downloading)
        uiSnapshotDirty.store (true, std::memory_order_release);

    if (uiSnapshotDirty.exchange (false, std::memory_order_acq_rel))
        publishUiSliceSnapshot();

    if (! sampleData.isLoaded())
    {
        if (! sampleMissing.load (std::memory_order_relaxed))
            sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
        return;
    }

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

    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    if (numActiveBuses <= 1)
    {
        // Fast path: single stereo output — voice-first block render
        voicePool.renderMainBusBlock (sampleData, busL[0], busR[0], numSamples);

        // Sanitise once after all voices have been mixed
        if (busL[0])
            for (int i = 0; i < numSamples; ++i)
                busL[0][i] = sanitiseSample (busL[0][i]);
        if (busR[0])
            for (int i = 0; i < numSamples; ++i)
                busR[0][i] = sanitiseSample (busR[0][i]);
    }
    else
    {
        // Multi-out: voice-first block render with per-voice bus routing
        voicePool.renderRoutedBlock (sampleData, busL.data(), busR.data(), numActiveBuses, numSamples);

        // Clamp / NaN-guard every active bus after accumulation
        for (int b = 0; b < numActiveBuses; ++b)
        {
            const auto busIndex = static_cast<size_t> (b);
            if (busL[busIndex])
                for (int i = 0; i < numSamples; ++i)
                    busL[busIndex][i] = sanitiseSample (busL[busIndex][i]);
            if (busR[busIndex])
                for (int i = 0; i < numSamples; ++i)
                    busR[busIndex][i] = sanitiseSample (busR[busIndex][i]);
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
    stream.writeInt (kCurrentStateVersion);

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
        stream.writeInt ((int)(s.lockMask & 0xFFFFFFFF));
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
        // v23 fields
        stream.writeFloat (s.filterAsym);
        stream.writeInt ((int)(s.lockMask >> 32));
        // v24 fields
        stream.writeFloat (s.crossfadePct);
    }

    // v9: store file path only (no PCM)
    juce::String filePath;
    juce::String fileName;
    if (auto sampleSnap = sampleData.getSnapshot())
    {
        if (! sampleSnap->sessionSamples.empty())
        {
            filePath = sampleSnap->sessionSamples.front().filePath;
            fileName = sampleSnap->sessionSamples.front().fileName;
        }
        else
        {
            filePath = sampleSnap->filePath;
            fileName = sampleSnap->fileName;
        }
    }
    else if (sampleMissing.load (std::memory_order_relaxed))
    {
        const auto& missingInfo = getMissingFileInfo();
        filePath = missingInfo.filePath.toString();
        fileName = missingInfo.fileName.toString();
    }
    else
    {
        const auto pendingFile = getPendingStateFile();
        if (pendingFile != juce::File())
        {
            filePath = pendingFile.getFullPathName();
            fileName = pendingFile.getFileName();
        }
    }
    stream.writeString (filePath);
    stream.writeString (fileName);

    stream.writeInt (sampleData.getNumFrames());
    stream.writeDouble (sampleData.getDecodedSampleRate());
    stream.writeInt (sampleData.getSourceNumFrames());
    stream.writeDouble (sampleData.getSourceSampleRate());

    // v12: snap-to-zero-crossing toggle
    stream.writeBool (snapToZeroCrossing.load());

    // v19: MIDI edit settings
    stream.writeBool (midiEditState.enabled.load (std::memory_order_relaxed));
    stream.writeInt  (midiEditState.channel.load (std::memory_order_relaxed));
    stream.writeBool (midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed));

    // Optional v24 extension block for fields added without changing the base version.
    stream.writeInt (kStateExtensionMagic);
    stream.writeInt (5);
    stream.writeInt (numSlices);
    for (int i = 0; i < numSlices; ++i)
        stream.writeInt (sliceManager.getSlice (i).repitchMode);
    stream.writeInt (numSlices);
    for (int i = 0; i < numSlices; ++i)
    {
        const auto& s = sliceManager.getSlice (i);
        stream.writeInt (s.loopStartOffset);
        stream.writeInt (s.loopLength);
    }
    // Extension v3: per-slice note ranges
    stream.writeInt (numSlices);
    for (int i = 0; i < numSlices; ++i)
    {
        const auto& s = sliceManager.getSlice (i);
        stream.writeInt (s.highNote);
        stream.writeInt (s.sliceRootNote);
    }
    const auto sampleSnap = sampleData.getSnapshot();
    const int numSessionSamples = sampleSnap != nullptr
        ? juce::jmin ((int) sampleSnap->sessionSamples.size(), SampleData::kMaxSessionSamples)
        : 0;
    stream.writeInt (numSessionSamples);
    stream.writeInt (selectedSessionSampleId.load (std::memory_order_relaxed));
    for (int i = 0; i < numSessionSamples; ++i)
    {
        const auto& sample = sampleSnap->sessionSamples[(size_t) i];
        stream.writeInt (sample.sampleId);
        stream.writeString (sample.filePath);
        stream.writeString (sample.fileName);
    }
    stream.writeInt (numSlices);
    for (int i = 0; i < numSlices; ++i)
    {
        const auto& s = sliceManager.getSlice (i);
        stream.writeInt (s.sampleId);
        stream.writeInt (s.startInSample);
        stream.writeInt (s.endInSample);
    }
    // Extension v5: per-session-sample stem metadata
    stream.writeInt (numSessionSamples);
    for (int i = 0; i < numSessionSamples; ++i)
    {
        const auto& sample = sampleSnap->sessionSamples[(size_t) i];
        auto meta = getStemMeta (sample.sampleId);
        stream.writeInt (meta.parentSourceSampleId);
        stream.writeInt (static_cast<int> (meta.role));
        stream.writeBool (meta.isGenerated);
    }
}

void IntersectProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream (data, (size_t) sizeInBytes, false);
    pendingStateRestoreToken.store (0, std::memory_order_release);
    clearPendingStateFiles();

    int version = stream.readInt();
    if (version != 19 && version != 20 && version != 21 && version != 22 && version != 23 && version != kCurrentStateVersion)
    {
        setUiStatusMessage ("Unsupported project state version v" + juce::String (version)
                            + ". This build supports v19-v" + juce::String (kCurrentStateVersion) + ".",
                            true);
        uiSnapshotDirty.store (true, std::memory_order_release);
        publishUiSliceSnapshot();
        return;
    }

    clearUiStatusMessage();

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
    sliceManager.rootNote.store (juce::jlimit (0, kMaxMidiNote, stream.readInt()));

    // Slice data
    const int storedNumSlices = stream.readInt();
    if (storedNumSlices < 0 || storedNumSlices > 4096)
        return;

    const int validatedNumSlices = juce::jlimit (0, SliceManager::kMaxSlices, storedNumSlices);
    sliceManager.setNumSlices (validatedNumSlices);
    sliceManager.selectedSlice = juce::jlimit (-1, validatedNumSlices - 1, savedSelectedSlice);

    std::vector<Slice> restoredSlices ((size_t) validatedNumSlices);
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
        parsed.lockMask       = (uint64_t)(uint32_t) stream.readInt();
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

        if (version >= 23)
        {
            parsed.filterAsym = stream.readFloat();
            parsed.lockMask |= ((uint64_t)(uint32_t) stream.readInt() << 32);
        }

        if (version >= 24)
        {
            parsed.crossfadePct = stream.readFloat();
        }

        if (i < validatedNumSlices)
            restoredSlices[(size_t) i] = parsed;
    }

    struct PostSliceParseResult
    {
        bool valid = false;
        int score = -1000000;
        juce::String filePath;
        juce::String fileName;
        int savedDecodedNumFrames = 0;
        double savedDecodedSampleRate = 0.0;
        int savedSourceNumFrames = 0;
        double savedSourceSampleRate = 0.0;
        bool snapToZeroCrossing = false;
        bool midiEditEnabled = false;
        int midiEditChannel = 0;
        bool consumeMidiEditCc = true;
        std::vector<int> repitchModes;
        std::vector<int> loopStartOffsets;
        std::vector<int> loopLengths;
        std::vector<int> highNotes;
        std::vector<int> sliceRootNotes;
        std::vector<SampleData::SessionSample> sessionSamples;
        int selectedSessionSampleId = -1;
        std::vector<int> sliceSampleIds;
        std::vector<int> sliceStartsInSample;
        std::vector<int> sliceEndsInSample;
    };

    const auto postSliceBasePosition = stream.getPosition();
    auto tryParsePostSliceData = [&] (bool readInlineLoopFields) -> PostSliceParseResult
    {
        PostSliceParseResult result;
        result.repitchModes.assign ((size_t) validatedNumSlices, 0);
        result.loopStartOffsets.assign ((size_t) validatedNumSlices, 0);
        result.loopLengths.assign ((size_t) validatedNumSlices, 0);
        result.highNotes.assign ((size_t) validatedNumSlices, -1);
        result.sliceRootNotes.assign ((size_t) validatedNumSlices, -1);
        result.sliceSampleIds.assign ((size_t) validatedNumSlices, 0);
        result.sliceStartsInSample.assign ((size_t) validatedNumSlices, 0);
        result.sliceEndsInSample.assign ((size_t) validatedNumSlices, 0);

        juce::MemoryInputStream trialStream (data, (size_t) sizeInBytes, false);
        if (! trialStream.setPosition (postSliceBasePosition))
            return result;

        auto bytesRemaining = [&trialStream]() -> int64_t
        {
            return trialStream.getTotalLength() - trialStream.getPosition();
        };

        auto requireBytes = [&bytesRemaining] (int64_t count) -> bool
        {
            return bytesRemaining() >= count;
        };

        if (readInlineLoopFields)
        {
            for (int i = 0; i < storedNumSlices; ++i)
            {
                if (! requireBytes (8))
                    return result;

                const int loopStartOffset = trialStream.readInt();
                const int loopLength = trialStream.readInt();
                if (i < validatedNumSlices)
                {
                    result.loopStartOffsets[(size_t) i] = loopStartOffset;
                    result.loopLengths[(size_t) i] = loopLength;
                }
            }
        }

        result.filePath = trialStream.readString();
        result.fileName = trialStream.readString();

        if (version >= 22)
        {
            if (! requireBytes (24))
                return result;

            result.savedDecodedNumFrames = trialStream.readInt();
            result.savedDecodedSampleRate = trialStream.readDouble();
            result.savedSourceNumFrames = trialStream.readInt();
            result.savedSourceSampleRate = trialStream.readDouble();
        }

        if (! requireBytes (7))
            return result;

        result.snapToZeroCrossing = trialStream.readBool();
        result.midiEditEnabled = trialStream.readBool();
        result.midiEditChannel = trialStream.readInt();
        result.consumeMidiEditCc = trialStream.readBool();

        bool foundExtension = false;
        if (bytesRemaining() >= 12)
        {
            const int maybeMagic = trialStream.readInt();
            if (maybeMagic == kStateExtensionMagic)
            {
                foundExtension = true;
                const int extensionVersion = trialStream.readInt();
                if (extensionVersion >= 1)
                {
                    if (! requireBytes (4))
                        return result;

                    const int storedRepitchModes = juce::jlimit (0, storedNumSlices, trialStream.readInt());
                    for (int i = 0; i < storedRepitchModes; ++i)
                    {
                        if (! requireBytes (4))
                            return result;

                        const int repitchMode = juce::jlimit (0, 2, trialStream.readInt());
                        if (i < validatedNumSlices)
                            result.repitchModes[(size_t) i] = repitchMode;
                    }
                }

                if (extensionVersion >= 2)
                {
                    if (! requireBytes (4))
                        return result;

                    const int storedLoopBounds = juce::jlimit (0, storedNumSlices, trialStream.readInt());
                    for (int i = 0; i < storedLoopBounds; ++i)
                    {
                        if (! requireBytes (8))
                            return result;

                        const int loopStartOffset = trialStream.readInt();
                        const int loopLength = trialStream.readInt();
                        if (i < validatedNumSlices)
                        {
                            result.loopStartOffsets[(size_t) i] = loopStartOffset;
                            result.loopLengths[(size_t) i] = loopLength;
                        }
                    }
                }

                if (extensionVersion >= 3)
                {
                    if (! requireBytes (4))
                        return result;

                    const int storedNoteRanges = juce::jlimit (0, storedNumSlices, trialStream.readInt());
                    for (int i = 0; i < storedNoteRanges; ++i)
                    {
                        if (! requireBytes (8))
                            return result;

                        const int highNote = trialStream.readInt();
                        const int sliceRootNote = trialStream.readInt();
                        if (i < validatedNumSlices)
                        {
                            result.highNotes[(size_t) i] = highNote;
                            result.sliceRootNotes[(size_t) i] = sliceRootNote;
                        }
                    }
                }

                if (extensionVersion >= 4)
                {
                    if (! requireBytes (8))
                        return result;

                    const int storedSessionSamples = juce::jlimit (0, SampleData::kMaxSessionSamples,
                                                                   trialStream.readInt());
                    result.selectedSessionSampleId = trialStream.readInt();
                    result.sessionSamples.reserve ((size_t) storedSessionSamples);
                    for (int i = 0; i < storedSessionSamples; ++i)
                    {
                        if (! requireBytes (4))
                            return result;

                        SampleData::SessionSample sample;
                        sample.sampleId = trialStream.readInt();
                        sample.filePath = trialStream.readString();
                        sample.fileName = trialStream.readString();
                        result.sessionSamples.push_back (std::move (sample));
                    }

                    if (! requireBytes (4))
                        return result;

                    const int storedSliceOwnership = juce::jlimit (0, storedNumSlices, trialStream.readInt());
                    for (int i = 0; i < storedSliceOwnership; ++i)
                    {
                        if (! requireBytes (12))
                            return result;

                        const int sampleId = trialStream.readInt();
                        const int startInSample = trialStream.readInt();
                        const int endInSample = trialStream.readInt();
                        if (i < validatedNumSlices)
                        {
                            result.sliceSampleIds[(size_t) i] = sampleId;
                            result.sliceStartsInSample[(size_t) i] = startInSample;
                            result.sliceEndsInSample[(size_t) i] = endInSample;
                        }
                    }
                }

                if (extensionVersion >= 5)
                {
                    if (! requireBytes (4))
                        return result;

                    const int storedStemMeta = juce::jlimit (0, SampleData::kMaxSessionSamples,
                                                              trialStream.readInt());
                    for (int i = 0; i < storedStemMeta; ++i)
                    {
                        if (! requireBytes (9))  // int + int + bool
                            return result;

                        const int parentId = trialStream.readInt();
                        const int roleInt = trialStream.readInt();
                        const bool isGenerated = trialStream.readBool();
                        if (i < (int) result.sessionSamples.size())
                        {
                            auto& meta = result.sessionSamples[(size_t) i].stemMeta;
                            meta.parentSourceSampleId = parentId;
                            meta.role = static_cast<StemRole> (juce::jlimit (0, 4, roleInt));
                            meta.isGenerated = isGenerated;
                        }
                    }
                }
            }
            else
            {
                trialStream.setPosition (trialStream.getPosition() - 4);
            }
        }

        result.valid = true;
        result.score = 0;
        if (foundExtension)
            result.score += 6;
        if (result.filePath.isNotEmpty() || result.fileName.isNotEmpty())
            result.score += 4;
        if (result.savedDecodedNumFrames >= 0 && result.savedDecodedNumFrames < 200000000)
            result.score += 1;
        else
            result.score -= 2;
        if (result.savedSourceNumFrames >= 0 && result.savedSourceNumFrames < 200000000)
            result.score += 1;
        else
            result.score -= 2;
        if (result.midiEditChannel >= 0 && result.midiEditChannel <= 16)
            result.score += 1;
        else
            result.score -= 3;
        if (bytesRemaining() == 0)
            result.score += 1;
        else if (! foundExtension)
            result.score -= 2;

        return result;
    };

    const auto baseLayoutResult = tryParsePostSliceData (false);
    const auto inlineLoopLayoutResult = (version >= 24 && storedNumSlices > 0)
        ? tryParsePostSliceData (true)
        : PostSliceParseResult {};

    const auto* postSliceResult = &baseLayoutResult;
    if (inlineLoopLayoutResult.valid
        && (! baseLayoutResult.valid || inlineLoopLayoutResult.score > baseLayoutResult.score))
    {
        postSliceResult = &inlineLoopLayoutResult;
    }

    if (! postSliceResult->valid)
        return;

    for (int i = 0; i < validatedNumSlices; ++i)
    {
        auto parsed = restoredSlices[(size_t) i];
        parsed.repitchMode = postSliceResult->repitchModes[(size_t) i];
        parsed.loopStartOffset = postSliceResult->loopStartOffsets[(size_t) i];
        parsed.loopLength = postSliceResult->loopLengths[(size_t) i];

        if (postSliceResult->highNotes[(size_t) i] >= 0)
        {
            parsed.highNote      = postSliceResult->highNotes[(size_t) i];
            parsed.sliceRootNote = postSliceResult->sliceRootNotes[(size_t) i];
        }
        else
        {
            // Legacy: no range data — single-note trigger, root = global rootNote
            // to preserve existing filter key-track sound
            parsed.highNote      = parsed.midiNote;
            parsed.sliceRootNote = sliceManager.rootNote.load();
        }

        if (! postSliceResult->sessionSamples.empty())
        {
            parsed.sampleId = postSliceResult->sliceSampleIds[(size_t) i];
            parsed.startInSample = postSliceResult->sliceStartsInSample[(size_t) i];
            parsed.endInSample = postSliceResult->sliceEndsInSample[(size_t) i];
        }

        sliceManager.getSlice (i) = sanitiseRestoredSlice (parsed);
    }

    for (int i = validatedNumSlices; i < SliceManager::kMaxSlices; ++i)
        sliceManager.getSlice (i).active = false;

    // Path-based sample restore
    auto filePath = postSliceResult->filePath;
    auto fileName = postSliceResult->fileName;
    int savedDecodedNumFrames = postSliceResult->savedDecodedNumFrames;
    double savedDecodedSampleRate = postSliceResult->savedDecodedSampleRate;
    int savedSourceNumFrames = postSliceResult->savedSourceNumFrames;
    double savedSourceSampleRate = postSliceResult->savedSourceSampleRate;

    clearVoicesBeforeSampleSwap();
    sampleData.clear();

    // Restore stem metadata from parsed session samples
    stemMetaEntryCount = 0;
    if (! postSliceResult->sessionSamples.empty())
    {
        for (const auto& sample : postSliceResult->sessionSamples)
        {
            if (sample.stemMeta.isGenerated)
                setStemMeta (sample.sampleId, sample.stemMeta);
        }
    }

    std::vector<juce::File> restoreFiles;
    std::vector<int> restoreSampleIds;
    if (! postSliceResult->sessionSamples.empty())
    {
        restoreFiles.reserve (postSliceResult->sessionSamples.size());
        restoreSampleIds.reserve (postSliceResult->sessionSamples.size());
        for (const auto& sample : postSliceResult->sessionSamples)
        {
            if (sample.filePath.isNotEmpty())
            {
                restoreFiles.emplace_back (sample.filePath);
                restoreSampleIds.push_back (sample.sampleId);
            }
        }
    }
    else if (filePath.isNotEmpty())
    {
        restoreFiles.emplace_back (filePath);
        restoreSampleIds.push_back (generateSessionSampleId());
    }

    if (! restoreFiles.empty())
    {
        sampleMissing.store (false);
        clearMissingFileInfo();
        setPendingStateFile (restoreFiles.front());
        setPendingStateFiles (restoreFiles);
        sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
        primePendingSliceTimelineRemap (version,
                                        savedDecodedNumFrames,
                                        savedDecodedSampleRate,
                                        savedSourceNumFrames,
                                        savedSourceSampleRate);
        // Preserve restored slices while loading, and report missing path via relink state.
        selectedSessionSampleId.store (postSliceResult->selectedSessionSampleId, std::memory_order_relaxed);
        pendingStateRestoreToken.store (requestSampleLoad (restoreFiles, LoadKindRelink, &restoreSampleIds),
                                        std::memory_order_release);
    }
    else
    {
        sampleMissing.store (false);
        clearMissingFileInfo();
        clearPendingStateFile();
        clearPendingStateFiles();
        sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
        clearPendingSliceTimelineRemap();
        selectedSessionSampleId.store (-1, std::memory_order_relaxed);
    }

    snapToZeroCrossing.store (postSliceResult->snapToZeroCrossing);

    // v19: MIDI edit settings
    midiEditState.enabled.store (postSliceResult->midiEditEnabled, std::memory_order_relaxed);
    midiEditState.channel.store (juce::jlimit (0, 16, postSliceResult->midiEditChannel), std::memory_order_relaxed);
    midiEditState.consumeMidiEditCc.store (postSliceResult->consumeMidiEditCc, std::memory_order_relaxed);

    sliceManager.rebuildMidiMap();
    publishUiSliceSnapshot();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return static_cast<juce::AudioProcessor*> (new IntersectProcessor());
}
