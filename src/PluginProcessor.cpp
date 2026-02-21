#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "audio/GrainEngine.h"
#include "audio/AudioAnalysis.h"

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
    maxVoicesParam   = apvts.getRawParameterValue (ParamIds::maxVoices);
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
}

void IntersectProcessor::releaseResources() {}

void IntersectProcessor::pushCommand (Command cmd)
{
    const auto scope = commandFifo.write (1);
    if (scope.blockSize1 > 0)
        commandBuffer[(size_t) scope.startIndex1] = std::move (cmd);
    else if (scope.blockSize2 > 0)
        commandBuffer[(size_t) scope.startIndex2] = std::move (cmd);
}

void IntersectProcessor::drainCommands()
{
    const auto scope = commandFifo.read (commandFifo.getNumReady());

    for (int i = 0; i < scope.blockSize1; ++i)
        handleCommand (commandBuffer[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)
        handleCommand (commandBuffer[(size_t) (scope.startIndex2 + i)]);

    if (scope.blockSize1 + scope.blockSize2 > 0)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
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
}

void IntersectProcessor::handleCommand (const Command& cmd)
{
    // Capture snapshot before modifying commands (not for load/relink/lazychop/undo/redo/none)
    switch (cmd.type)
    {
        case CmdNone:
        case CmdLoadFile:
        case CmdRelinkFile:
        case CmdLazyChopStart:
        case CmdLazyChopStop:
        case CmdUndo:
        case CmdRedo:
        case CmdSetSliceParam:
            break;
        default:
            captureSnapshot();
            break;
    }

    switch (cmd.type)
    {
        case CmdLoadFile:
            // Kill all active voices before replacing the sample buffer
            // to prevent dangling reads from stretcher pipelines
            for (int vi = 0; vi < VoicePool::kMaxVoices; ++vi)
            {
                auto& v = voicePool.getVoice (vi);
                v.active = false;
                v.stretcher.reset();
                v.bungeeStretcher.reset();
                // Use release on last store to ensure UI thread sees all
                // voice deactivations before the sample buffer is swapped
                voicePool.voicePositions[vi].store (0.0f,
                    vi == VoicePool::kMaxVoices - 1
                        ? std::memory_order_release
                        : std::memory_order_relaxed);
            }
            if (sampleData.loadFromFile (cmd.fileParam, currentSampleRate))
            {
                sliceManager.clearAll();
                sampleMissing.store (false);
                missingFilePath.clear();
            }
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
                s.lockMask ^= (uint32_t) cmd.intParam1;
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

                switch (field)
                {
                    case FieldBpm:       s.bpm = val;            s.lockMask |= kLockBpm;       break;
                    case FieldPitch:     s.pitchSemitones = val; s.lockMask |= kLockPitch;     break;
                    case FieldAlgorithm: s.algorithm = (int) val; s.lockMask |= kLockAlgorithm; break;
                    case FieldAttack:    s.attackSec = val;      s.lockMask |= kLockAttack;    break;
                    case FieldDecay:     s.decaySec = val;       s.lockMask |= kLockDecay;     break;
                    case FieldSustain:   s.sustainLevel = val;   s.lockMask |= kLockSustain;   break;
                    case FieldRelease:   s.releaseSec = val;     s.lockMask |= kLockRelease;   break;
                    case FieldMuteGroup: s.muteGroup = (int) val; s.lockMask |= kLockMuteGroup; break;
                    case FieldStretchEnabled: s.stretchEnabled = val > 0.5f; s.lockMask |= kLockStretch; break;
                    case FieldTonality:  s.tonalityHz = val;        s.lockMask |= kLockTonality;    break;
                    case FieldFormant:   s.formantSemitones = val;   s.lockMask |= kLockFormant;     break;
                    case FieldFormantComp: s.formantComp = val > 0.5f; s.lockMask |= kLockFormantComp; break;
                    case FieldGrainMode:  s.grainMode = (int) val;   s.lockMask |= kLockGrainMode;  break;
                    case FieldVolume:     s.volume = val;            s.lockMask |= kLockVolume;    break;
                    case FieldReleaseTail: s.releaseTail = val > 0.5f; s.lockMask |= kLockReleaseTail; break;
                    case FieldReverse:    s.reverse = val > 0.5f;    s.lockMask |= kLockReverse;    break;
                    case FieldOutputBus:  s.outputBus = juce::jlimit (0, 15, (int) val); s.lockMask |= kLockOutputBus; break;
                    case FieldLoop:       s.loopMode = (int) val;    s.lockMask |= kLockLoop;      break;
                    case FieldMidiNote:
                        s.midiNote = juce::jlimit (0, 127, (int) val);
                        sliceManager.rebuildMidiMap();
                        break;
                }
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
                    dst.bpm             = src.bpm;
                    dst.pitchSemitones  = src.pitchSemitones;
                    dst.algorithm       = src.algorithm;
                    dst.attackSec       = src.attackSec;
                    dst.decaySec        = src.decaySec;
                    dst.sustainLevel    = src.sustainLevel;
                    dst.releaseSec      = src.releaseSec;
                    dst.muteGroup       = src.muteGroup;
                    dst.loopMode        = src.loopMode;
                    dst.stretchEnabled  = src.stretchEnabled;
                    dst.tonalityHz      = src.tonalityHz;
                    dst.formantSemitones = src.formantSemitones;
                    dst.formantComp     = src.formantComp;
                    dst.grainMode       = src.grainMode;
                    dst.volume          = src.volume;
                    dst.releaseTail     = src.releaseTail;
                    dst.reverse         = src.reverse;
                    dst.outputBus       = src.outputBus;
                    dst.lockMask        = src.lockMask;
                    dst.colour          = src.colour;
                    // midiNote is already assigned by createSlice
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
                        dst.bpm = srcCopy.bpm;
                        dst.pitchSemitones = srcCopy.pitchSemitones;
                        dst.algorithm = srcCopy.algorithm;
                        dst.attackSec = srcCopy.attackSec;
                        dst.decaySec = srcCopy.decaySec;
                        dst.sustainLevel = srcCopy.sustainLevel;
                        dst.releaseSec = srcCopy.releaseSec;
                        dst.muteGroup = srcCopy.muteGroup;
                        dst.loopMode = srcCopy.loopMode;
                        dst.stretchEnabled = srcCopy.stretchEnabled;
                        dst.tonalityHz = srcCopy.tonalityHz;
                        dst.formantSemitones = srcCopy.formantSemitones;
                        dst.formantComp = srcCopy.formantComp;
                        dst.grainMode = srcCopy.grainMode;
                        dst.volume = srcCopy.volume;
                        dst.releaseTail = srcCopy.releaseTail;
                        dst.reverse = srcCopy.reverse;
                        dst.outputBus = srcCopy.outputBus;
                        dst.lockMask = srcCopy.lockMask;
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
            if (sel >= 0 && sel < sliceManager.getNumSlices() && ! cmd.positions.empty())
            {
                Slice srcCopy = sliceManager.getSlice (sel);
                int startS = srcCopy.startSample;
                int endS = srcCopy.endSample;

                sliceManager.deleteSlice (sel);

                // Build boundary list: [startS, ...positions..., endS]
                std::vector<int> bounds;
                bounds.push_back (startS);
                std::copy (cmd.positions.begin(), cmd.positions.end(), std::back_inserter (bounds));
                bounds.push_back (endS);

                int firstNew = -1;
                for (size_t i = 0; i + 1 < bounds.size(); ++i)
                {
                    int s = bounds[i];
                    int e = bounds[i + 1];
                    if (e - s < 64) continue;
                    int idx = sliceManager.createSlice (s, e);
                    if (idx >= 0)
                    {
                        auto& dst = sliceManager.getSlice (idx);
                        dst.bpm = srcCopy.bpm;
                        dst.pitchSemitones = srcCopy.pitchSemitones;
                        dst.algorithm = srcCopy.algorithm;
                        dst.attackSec = srcCopy.attackSec;
                        dst.decaySec = srcCopy.decaySec;
                        dst.sustainLevel = srcCopy.sustainLevel;
                        dst.releaseSec = srcCopy.releaseSec;
                        dst.muteGroup = srcCopy.muteGroup;
                        dst.loopMode = srcCopy.loopMode;
                        dst.stretchEnabled = srcCopy.stretchEnabled;
                        dst.tonalityHz = srcCopy.tonalityHz;
                        dst.formantSemitones = srcCopy.formantSemitones;
                        dst.formantComp = srcCopy.formantComp;
                        dst.grainMode = srcCopy.grainMode;
                        dst.volume = srcCopy.volume;
                        dst.releaseTail = srcCopy.releaseTail;
                        dst.reverse = srcCopy.reverse;
                        dst.outputBus = srcCopy.outputBus;
                        dst.lockMask = srcCopy.lockMask;
                    }
                    if (firstNew < 0) firstNew = idx;
                }

                sliceManager.rebuildMidiMap();
                if (firstNew >= 0)
                    sliceManager.selectedSlice = firstNew;
            }
            break;
        }

        case CmdRelinkFile:
            if (sampleData.loadFromFile (cmd.fileParam, currentSampleRate))
            {
                sampleMissing.store (false);
                missingFilePath.clear();
                sliceManager.rebuildMidiMap();
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
            break; // snapshot already captured by default path above

        case CmdNone:
            break;
    }
}

void IntersectProcessor::processMidi (const juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            int note = msg.getNoteNumber();
            float velocity = (float) msg.getVelocity();

            if (lazyChop.isActive())
            {
                // Any MIDI note places a chop boundary at the playhead
                int newSliceIdx = lazyChop.onNote (note, voicePool, sliceManager);
                if (midiSelectsSlice.load (std::memory_order_relaxed) && newSliceIdx >= 0)
                    sliceManager.selectedSlice = newSliceIdx;
            }
            else
            {
                const auto& sliceIndices = sliceManager.midiNoteToSlices (note);
                for (int sliceIdx : sliceIndices)
                {
                    if (midiSelectsSlice.load (std::memory_order_relaxed))
                        sliceManager.selectedSlice = sliceIdx;

                    int voiceIdx = voicePool.allocate();

                    // Handle mute groups
                    float globalMG = muteGroupParam->load();
                    const auto& s = sliceManager.getSlice (sliceIdx);
                    int mg = (int) sliceManager.resolveParam (sliceIdx, kLockMuteGroup,
                                                              (float) s.muteGroup, globalMG);
                    voicePool.muteGroup (mg, voiceIdx);

                    voicePool.startVoice (voiceIdx, sliceIdx, velocity, note,
                                          sliceManager,
                                          bpmParam->load(),
                                          pitchParam->load(),
                                          (int) algoParam->load(),
                                          attackParam->load() / 1000.0f,
                                          decayParam->load() / 1000.0f,
                                          sustainParam->load() / 100.0f,
                                          releaseParam->load() / 1000.0f,
                                          (int) muteGroupParam->load(),
                                          stretchParam->load() > 0.5f,
                                          dawBpm.load(),
                                          tonalityParam->load(),
                                          formantParam->load(),
                                          formantCompParam->load() > 0.5f,
                                          (int) grainModeParam->load(),
                                          masterVolParam->load(),
                                          releaseTailParam->load() > 0.5f,
                                          reverseParam->load() > 0.5f,
                                          (int) loopParam->load(),
                                          sampleData);
                }
            }
        }
        else if (msg.isNoteOff())
        {
            voicePool.releaseNote (msg.getNoteNumber());
        }
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

    drainCommands();

    // Update max active voices from param
    voicePool.setMaxActiveVoices ((int) maxVoicesParam->load());

    processMidi (midi);

    if (! sampleData.isLoaded())
        return;

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
        constexpr int previewIdx = VoicePool::kMaxVoices - 1;
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
    stream.writeInt (14);

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
    }

    // v9: store file path only (no PCM)
    stream.writeString (sampleData.getFilePath());
    stream.writeString (sampleData.getFileName());

    // v12: snap-to-zero-crossing toggle
    stream.writeBool (snapToZeroCrossing.load());
}

void IntersectProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream (data, (size_t) sizeInBytes, false);

    int version = stream.readInt();
    if (version != 14)
        return;

    // APVTS state
    auto xmlString = stream.readString();
    if (auto xml = juce::parseXML (xmlString))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // UI state
    zoom.store (juce::jlimit (1.0f, 16384.0f, stream.readFloat()));
    scroll.store (juce::jlimit (0.0f, 1.0f, stream.readFloat()));
    sliceManager.selectedSlice = stream.readInt();

    midiSelectsSlice.store (stream.readBool());
    sliceManager.rootNote.store (stream.readInt());

    // Slice data
    int numSlices = stream.readInt();
    sliceManager.setNumSlices (numSlices);
    for (int i = 0; i < numSlices; ++i)
    {
        auto& s = sliceManager.getSlice (i);
        s.active         = stream.readBool();
        s.startSample    = stream.readInt();
        s.endSample      = stream.readInt();
        s.midiNote       = stream.readInt();
        s.bpm            = stream.readFloat();
        s.pitchSemitones = stream.readFloat();
        s.algorithm      = stream.readInt();
        s.attackSec      = stream.readFloat();
        s.decaySec       = stream.readFloat();
        s.sustainLevel   = stream.readFloat();
        s.releaseSec     = stream.readFloat();
        s.muteGroup      = stream.readInt();
        s.loopMode       = stream.readInt();
        s.stretchEnabled = stream.readBool();
        s.lockMask       = (uint32_t) stream.readInt();
        s.colour         = juce::Colour ((juce::uint32) stream.readInt());
        s.tonalityHz      = stream.readFloat();
        s.formantSemitones = stream.readFloat();
        s.formantComp     = stream.readBool();
        s.grainMode = stream.readInt();
        s.volume = stream.readFloat();
        s.releaseTail = stream.readBool();
        s.reverse = stream.readBool();
        s.outputBus = stream.readInt();
    }

    // Path-based sample restore
    auto filePath = stream.readString();
    auto fileName = stream.readString();

    if (filePath.isNotEmpty())
    {
        juce::File f (filePath);
        double sr = currentSampleRate > 0 ? currentSampleRate : 44100.0;
        if (f.existsAsFile() && sampleData.loadFromFile (f, sr))
        {
            sampleMissing.store (false);
        }
        else
        {
            sampleMissing.store (true);
            missingFilePath = filePath;
            sampleData.setFileName (fileName);
            sampleData.setFilePath (filePath);
        }
    }

    sliceManager.rebuildMidiMap();

    snapToZeroCrossing.store (stream.readBool());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IntersectProcessor();
}
