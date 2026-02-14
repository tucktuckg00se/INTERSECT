#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "audio/WsolaEngine.h"

IntersectProcessor::IntersectProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
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
}

void IntersectProcessor::handleCommand (const Command& cmd)
{
    switch (cmd.type)
    {
        case CmdLoadFile:
            if (sampleData.loadFromFile (cmd.fileParam, currentSampleRate))
            {
                // Clear existing slices but don't create a default one
                sliceManager.clearAll();
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
                lazyChop.start (sampleData.getNumFrames());
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
                float newBpm = WsolaEngine::calcStretchBpm (
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
                    dst.lockMask        = src.lockMask;
                    dst.colour          = src.colour;
                    // midiNote is already assigned by createSlice
                    sliceManager.selectedSlice = newIdx;
                }
            }
            break;
        }

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
                int sliceIdx = sliceManager.midiNoteToSlice (note);
                if (sliceIdx >= 0)
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
    processMidi (midi);

    if (! sampleData.isLoaded())
        return;

    float masterVol = masterVolParam->load();
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float sL = 0.0f, sR = 0.0f;
        voicePool.processSample (sampleData, currentSampleRate, sL, sR);

        outL[i] = sL * masterVol;
        if (outR != nullptr)
            outR[i] = sR * masterVol;
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
    stream.writeInt (5);

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
    }

    // Audio PCM data (stereo interleaved floats)
    int numFrames = sampleData.getNumFrames();
    stream.writeInt (numFrames);
    if (numFrames > 0)
    {
        const auto& buf = sampleData.getBuffer();
        for (int f = 0; f < numFrames; ++f)
        {
            stream.writeFloat (buf.getSample (0, f));
            stream.writeFloat (buf.getSample (1, f));
        }
    }
}

void IntersectProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream (data, (size_t) sizeInBytes, false);

    int version = stream.readInt();
    if (version < 1)
        return;

    // APVTS state
    auto xmlString = stream.readString();
    if (auto xml = juce::parseXML (xmlString))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // UI state
    zoom.store (stream.readFloat());
    scroll.store (stream.readFloat());
    sliceManager.selectedSlice = stream.readInt();

    if (version >= 4)
    {
        midiSelectsSlice.store (stream.readBool());
    }

    // Sample bounds (version 2/3 — read and discard for backward compat)
    if (version >= 2 && version <= 3)
    {
        stream.readInt();  // sampleStart (discarded)
        stream.readInt();  // sampleEnd (discarded)
    }

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
        if (version >= 3)
            s.stretchEnabled = stream.readBool();
        else
            s.stretchEnabled = false;
        s.lockMask       = (uint32_t) stream.readInt();
        s.colour         = juce::Colour ((juce::uint32) stream.readInt());
        if (version >= 5)
        {
            s.tonalityHz      = stream.readFloat();
            s.formantSemitones = stream.readFloat();
            s.formantComp     = stream.readBool();
        }
    }

    // Audio PCM
    int numFrames = stream.readInt();
    if (numFrames > 0)
    {
        juce::AudioBuffer<float> restoredBuf (2, numFrames);
        for (int f = 0; f < numFrames; ++f)
        {
            restoredBuf.setSample (0, f, stream.readFloat());
            restoredBuf.setSample (1, f, stream.readFloat());
        }
        sampleData.loadFromBuffer (std::move (restoredBuf));
    }

    sliceManager.rebuildMidiMap();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IntersectProcessor();
}
