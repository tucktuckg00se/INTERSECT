#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "audio/GrainEngine.h"

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
    pingPongParam  = apvts.getRawParameterValue (ParamIds::defaultPingPong);
    stretchParam   = apvts.getRawParameterValue (ParamIds::defaultStretchEnabled);
    tonalityParam  = apvts.getRawParameterValue (ParamIds::defaultTonality);
    formantParam   = apvts.getRawParameterValue (ParamIds::defaultFormant);
    formantCompParam = apvts.getRawParameterValue (ParamIds::defaultFormantComp);
    grainModeParam   = apvts.getRawParameterValue (ParamIds::defaultGrainMode);
    releaseTailParam = apvts.getRawParameterValue (ParamIds::defaultReleaseTail);
    reverseParam     = apvts.getRawParameterValue (ParamIds::defaultReverse);
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

void IntersectProcessor::handleCommand (const Command& cmd)
{
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
                voicePool.voicePositions[vi].store (0.0f, std::memory_order_relaxed);
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
                lazyChop.start (sampleData.getNumFrames(), sliceManager, psp);
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
                    case FieldPingPong:  s.pingPong = val > 0.5f; s.lockMask |= kLockPingPong; break;
                    case FieldStretchEnabled: s.stretchEnabled = val > 0.5f; s.lockMask |= kLockStretch; break;
                    case FieldTonality:  s.tonalityHz = val;        s.lockMask |= kLockTonality;    break;
                    case FieldFormant:   s.formantSemitones = val;   s.lockMask |= kLockFormant;     break;
                    case FieldFormantComp: s.formantComp = val > 0.5f; s.lockMask |= kLockFormantComp; break;
                    case FieldGrainMode:  s.grainMode = (int) val;   s.lockMask |= kLockGrainMode;  break;
                    case FieldVolume:     s.volume = val;            s.lockMask |= kLockVolume;    break;
                    case FieldReleaseTail: s.releaseTail = val > 0.5f; s.lockMask |= kLockReleaseTail; break;
                    case FieldReverse:    s.reverse = val > 0.5f;    s.lockMask |= kLockReverse;    break;
                    case FieldOutputBus:  s.outputBus = juce::jlimit (0, 15, (int) val); s.lockMask |= kLockOutputBus; break;
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
                    dst.pingPong        = src.pingPong;
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
                const auto& src = sliceManager.getSlice (sel);
                int startS = src.startSample;
                int endS = src.endSample;
                int count = juce::jlimit (2, 128, cmd.intParam1);
                int len = endS - startS;

                sliceManager.deleteSlice (sel);

                int firstNew = -1;
                for (int i = 0; i < count; ++i)
                {
                    int s = startS + i * len / count;
                    int e = startS + (i + 1) * len / count;
                    int idx = sliceManager.createSlice (s, e);
                    if (i == 0) firstNew = idx;
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

        case CmdNone:
            break;
    }
}

void IntersectProcessor::processMidi (juce::MidiBuffer& midi)
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
                lazyChop.onNote (note, voicePool, sliceManager);
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
                                          pingPongParam->load() > 0.5f,
                                          stretchParam->load() > 0.5f,
                                          dawBpm.load(),
                                          tonalityParam->load(),
                                          formantParam->load(),
                                          formantCompParam->load() > 0.5f,
                                          (int) grainModeParam->load(),
                                          masterVolParam->load(),
                                          releaseTailParam->load() > 0.5f,
                                          reverseParam->load() > 0.5f,
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
            busL[b] = buffer.getWritePointer (chOff);
            busR[b] = buffer.getNumChannels() > chOff + 1
                          ? buffer.getWritePointer (chOff + 1) : nullptr;
            if (b + 1 > numActiveBuses) numActiveBuses = b + 1;
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
            if (busL[0]) busL[0][i] = sL;
            if (busR[0]) busR[0][i] = sR;
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
                if (bus < 0 || bus >= numActiveBuses) bus = 0;
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
    stream.writeInt (11);

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
        stream.writeBool (s.pingPong);
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
}

void IntersectProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream (data, (size_t) sizeInBytes, false);

    int version = stream.readInt();
    if (version < 9 || version > 11)
        return;

    // APVTS state
    auto xmlString = stream.readString();
    if (auto xml = juce::parseXML (xmlString))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // v9 -> v10 migration: convert APVTS masterVolume from linear (0-1) to dB
    if (version == 9)
    {
        float oldLinVol = masterVolParam->load();
        // Old range was 0-1, but APVTS loaded it as-is into new -100..+24 range
        // So the stored value will be the raw 0-1 value. Convert to dB.
        float dbVal = (oldLinVol <= 0.0f) ? -100.0f : 20.0f * std::log10 (oldLinVol);
        dbVal = juce::jlimit (-100.0f, 24.0f, dbVal);
        if (auto* p = apvts.getParameter (ParamIds::masterVolume))
            p->setValueNotifyingHost (p->convertTo0to1 (dbVal));
    }

    // UI state
    zoom.store (stream.readFloat());
    scroll.store (stream.readFloat());
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
        s.pingPong       = stream.readBool();
        s.stretchEnabled = stream.readBool();
        s.lockMask       = (uint32_t) stream.readInt();
        s.colour         = juce::Colour ((juce::uint32) stream.readInt());
        s.tonalityHz      = stream.readFloat();
        s.formantSemitones = stream.readFloat();
        s.formantComp     = stream.readBool();
        s.grainMode = stream.readInt();
        s.volume = stream.readFloat();

        if (version >= 10)
        {
            s.releaseTail = stream.readBool();
        }

        if (version >= 11)
        {
            s.reverse = stream.readBool();
            s.outputBus = stream.readInt();
        }
        else
        {
            s.reverse = false;
            s.outputBus = 0;
        }

        // v9 -> v10 migration: convert linear volume (0-1) to dB
        if (version == 9)
        {
            float linVol = s.volume;
            if (linVol <= 0.0f)
                s.volume = -100.0f;
            else
                s.volume = 20.0f * std::log10 (linVol);
        }
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
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IntersectProcessor();
}
