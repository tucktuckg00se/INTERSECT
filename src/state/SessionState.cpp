#include "SessionState.h"
#include "../audio/SliceManager.h"

namespace
{
constexpr int kStateExtensionMagic = 0x52504D31;   // "RPM1"
constexpr int kExtensionVersion = 8;

const juce::String kDamagedMessage = "This project's INTERSECT data is damaged, so it wasn't loaded.";

constexpr uint64_t kValidLockMask =
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

Slice sanitiseRestoredSlice (Slice s)
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
        // Clamp the loop to the slice's span inside its sample. A slice saved while its sample
        // was missing (parked) only carries a stub of absolute bounds; clamping to that stub
        // would lose its loop by the time the sample is relinked.
        const int sliceLen = juce::jmax (0, s.endInSample - s.startInSample);
        s.loopStartOffset = juce::jlimit (0, juce::jmax (0, sliceLen - 1), s.loopStartOffset);
        if (s.loopLength > 0)
            s.loopLength = juce::jlimit (1, juce::jmax (1, sliceLen - s.loopStartOffset), s.loopLength);
    }
    s.lockMask &= kValidLockMask;
    return s;
}

// Everything after the slice records. Two historical layouts exist for v24 (with and without
// inline loop fields), so both are tried and the more plausible parse wins.
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
    std::optional<StemComputeDevice> stemComputeDevice;
    std::optional<SampleParamTable> sampleParams;   // extension v7; absent in older states
};

PostSliceParseResult tryParsePostSliceData (const void* data, size_t size, juce::int64 postSliceBasePosition,
                                            int version, int storedNumSlices, int validatedNumSlices,
                                            bool readInlineLoopFields)
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

    juce::MemoryInputStream trialStream (data, size, false);
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
                        meta.role = static_cast<StemRole> (juce::jlimit (0, (int) StemRole::instrumental, roleInt));
                        // Sessions saved before extension v8 have no names; the role is the name.
                        if (isGenerated)
                            meta.setName (stemRoleToString (meta.role));
                        meta.isGenerated = isGenerated;
                    }
                }
            }

            if (extensionVersion >= 6)
            {
                if (requireBytes (4))
                {
                    const int deviceInt = trialStream.readInt();
                    result.stemComputeDevice = static_cast<StemComputeDevice> (juce::jlimit (0, 1, deviceInt));
                }
            }

            if (extensionVersion >= 7 && requireBytes (4))
            {
                const int count = trialStream.readInt();
                if (count >= 0 && count <= SampleData::kMaxSessionSamples)
                {
                    SampleParamTable table;
                    bool ok = true;
                    for (int i = 0; i < count && ok; ++i)
                    {
                        ok = requireBytes (8);
                        if (! ok)
                            break;
                        const int sampleId = trialStream.readInt();
                        const int numFields = trialStream.readInt();
                        ok = numFields >= 0 && numFields <= 1024 && requireBytes ((juce::int64) numFields * 8);
                        if (! ok)
                            break;

                        // Fields missing from the file keep factory defaults; unknown ones are skipped.
                        auto params = SampleParams::factoryDefaults();
                        for (int f = 0; f < numFields; ++f)
                        {
                            const int field = trialStream.readInt();
                            const float value = trialStream.readFloat();
                            params.setField (field, value);
                        }
                        table.set (sampleId, params);
                    }
                    if (ok)
                        result.sampleParams = table;
                }

                // v8 follows v7, so only read it when v7 parsed cleanly.
                if (extensionVersion >= 8 && result.sampleParams.has_value() && requireBytes (4))
                {
                    const int storedNames = trialStream.readInt();
                    if (storedNames >= 0 && storedNames <= SampleData::kMaxSessionSamples)
                    {
                        for (int i = 0; i < storedNames && requireBytes (1); ++i)
                        {
                            const auto name = trialStream.readString();
                            if (i < (int) result.sessionSamples.size() && name.isNotEmpty())
                                result.sessionSamples[(size_t) i].stemMeta.setName (name);
                        }
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
}
}

void StateFormat::write (const SessionState& state, juce::OutputStream& stream)
{
    // Version
    stream.writeInt (kCurrentStateVersion);

    // APVTS state
    stream.writeString (state.apvtsXml);

    // UI state
    stream.writeFloat (state.zoom);
    stream.writeFloat (state.scroll);
    stream.writeInt (state.selectedSlice);
    stream.writeBool (state.midiSelectsSlice);
    stream.writeInt (state.rootNote);

    // Slice data
    const int numSlices = (int) state.slices.size();
    stream.writeInt (numSlices);
    for (const auto& s : state.slices)
    {
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
    stream.writeString (state.filePath);
    stream.writeString (state.fileName);

    stream.writeInt (state.decodedNumFrames);
    stream.writeDouble (state.decodedSampleRate);
    stream.writeInt (state.sourceNumFrames);
    stream.writeDouble (state.sourceSampleRate);

    // v12: snap-to-zero-crossing toggle
    stream.writeBool (state.snapToZeroCrossing);

    // v19: MIDI edit settings
    stream.writeBool (state.midiEditEnabled);
    stream.writeInt  (state.midiEditChannel);
    stream.writeBool (state.consumeMidiEditCc);

    // Optional v24 extension block for fields added without changing the base version.
    stream.writeInt (kStateExtensionMagic);
    stream.writeInt (kExtensionVersion);
    stream.writeInt (numSlices);
    for (const auto& s : state.slices)
        stream.writeInt (s.repitchMode);
    stream.writeInt (numSlices);
    for (const auto& s : state.slices)
    {
        stream.writeInt (s.loopStartOffset);
        stream.writeInt (s.loopLength);
    }
    // Extension v3: per-slice note ranges
    stream.writeInt (numSlices);
    for (const auto& s : state.slices)
    {
        stream.writeInt (s.highNote);
        stream.writeInt (s.sliceRootNote);
    }
    // Extension v4: session samples and which sample each slice belongs to
    const int numSessionSamples = juce::jmin ((int) state.sessionSamples.size(), SampleData::kMaxSessionSamples);
    stream.writeInt (numSessionSamples);
    stream.writeInt (state.selectedSessionSampleId);
    for (int i = 0; i < numSessionSamples; ++i)
    {
        const auto& sample = state.sessionSamples[(size_t) i];
        stream.writeInt (sample.sampleId);
        stream.writeString (sample.filePath);
        stream.writeString (sample.fileName);
    }
    stream.writeInt (numSlices);
    for (const auto& s : state.slices)
    {
        stream.writeInt (s.sampleId);
        stream.writeInt (s.startInSample);
        stream.writeInt (s.endInSample);
    }
    // Extension v5: per-session-sample stem metadata
    stream.writeInt (numSessionSamples);
    for (int i = 0; i < numSessionSamples; ++i)
    {
        const auto& meta = state.sessionSamples[(size_t) i].stemMeta;
        stream.writeInt (meta.parentSourceSampleId);
        stream.writeInt (static_cast<int> (meta.role));
        stream.writeBool (meta.isGenerated);
    }
    // Extension v6: stem compute device preference
    stream.writeInt (static_cast<int> (state.stemComputeDevice.value_or (StemComputeDevice::cpu)));
    // Extension v7: per-session-sample sample-level parameters as (field id, value) pairs in
    // SAMPLE-tab units, so new sample params can be added without another format change.
    const SampleParamTable noParams;
    const auto& params = state.sampleParams.has_value() ? *state.sampleParams : noParams;
    stream.writeInt (numSessionSamples);
    for (int i = 0; i < numSessionSamples; ++i)
    {
        const int sampleId = state.sessionSamples[(size_t) i].sampleId;
        const auto& sampleParams = params.get (sampleId);   // factory defaults if missing
        stream.writeInt (sampleId);
        stream.writeInt (SampleParams::kNumFields);
        for (int field : SampleParams::fieldIds())
        {
            stream.writeInt (field);
            stream.writeFloat (sampleParams.getField (field));
        }
    }
    // Extension v8: per-session-sample stem names (model output names such as "lead-vocal").
    stream.writeInt (numSessionSamples);
    for (int i = 0; i < numSessionSamples; ++i)
        stream.writeString (state.sessionSamples[(size_t) i].stemMeta.getName());
}

StateFormat::ReadResult StateFormat::read (const void* data, size_t size)
{
    ReadResult failed;
    juce::MemoryInputStream stream (data, size, false);

    const int version = stream.readInt();
    if (version < kOldestStateVersion || version > kCurrentStateVersion)
    {
        failed.error = "Unsupported project state version v" + juce::String (version)
                     + ". This build supports v" + juce::String (kOldestStateVersion)
                     + "-v" + juce::String (kCurrentStateVersion) + ".";
        return failed;
    }

    SessionState state;
    state.version = version;

    // APVTS state
    state.apvtsXml = stream.readString();

    // UI state
    state.zoom = juce::jlimit (1.0f, 16384.0f, stream.readFloat());
    state.scroll = juce::jlimit (0.0f, 1.0f, stream.readFloat());
    const int savedSelectedSlice = stream.readInt();
    state.midiSelectsSlice = stream.readBool();
    state.rootNote = juce::jlimit (0, kMaxMidiNote, stream.readInt());

    // Slice data
    const int storedNumSlices = stream.readInt();
    if (storedNumSlices < 0 || storedNumSlices > 4096)
    {
        failed.error = kDamagedMessage;
        return failed;
    }

    const int validatedNumSlices = juce::jlimit (0, SliceManager::kMaxSlices, storedNumSlices);
    state.selectedSlice = juce::jlimit (-1, validatedNumSlices - 1, savedSelectedSlice);

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

            // v20 stored the envelope amount in Hz; the unit is semitones now.
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

    const auto postSliceBasePosition = stream.getPosition();
    const auto baseLayoutResult = tryParsePostSliceData (data, size, postSliceBasePosition, version,
                                                         storedNumSlices, validatedNumSlices, false);
    const auto inlineLoopLayoutResult = (version >= 24 && storedNumSlices > 0)
        ? tryParsePostSliceData (data, size, postSliceBasePosition, version, storedNumSlices, validatedNumSlices, true)
        : PostSliceParseResult {};

    const auto* postSliceResult = &baseLayoutResult;
    if (inlineLoopLayoutResult.valid
        && (! baseLayoutResult.valid || inlineLoopLayoutResult.score > baseLayoutResult.score))
    {
        postSliceResult = &inlineLoopLayoutResult;
    }

    if (! postSliceResult->valid)
    {
        failed.error = kDamagedMessage;
        return failed;
    }

    state.slices.reserve ((size_t) validatedNumSlices);
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
            // Legacy: no range data — single-note trigger, root = the state's root note
            // to preserve existing filter key-track sound
            parsed.highNote      = parsed.midiNote;
            parsed.sliceRootNote = state.rootNote;
        }

        if (! postSliceResult->sessionSamples.empty())
        {
            parsed.sampleId = postSliceResult->sliceSampleIds[(size_t) i];
            parsed.startInSample = postSliceResult->sliceStartsInSample[(size_t) i];
            parsed.endInSample = postSliceResult->sliceEndsInSample[(size_t) i];
        }
        else
        {
            // Legacy (pre multi-sample) states store only absolute positions on a single
            // sample starting at frame 0, so in-sample positions equal absolute ones.
            parsed.startInSample = parsed.startSample;
            parsed.endInSample = parsed.endSample;
        }

        state.slices.push_back (sanitiseRestoredSlice (parsed));
    }

    state.filePath = postSliceResult->filePath;
    state.fileName = postSliceResult->fileName;
    state.decodedNumFrames = postSliceResult->savedDecodedNumFrames;
    state.decodedSampleRate = postSliceResult->savedDecodedSampleRate;
    state.sourceNumFrames = postSliceResult->savedSourceNumFrames;
    state.sourceSampleRate = postSliceResult->savedSourceSampleRate;
    state.snapToZeroCrossing = postSliceResult->snapToZeroCrossing;
    state.midiEditEnabled = postSliceResult->midiEditEnabled;
    state.midiEditChannel = postSliceResult->midiEditChannel;
    state.consumeMidiEditCc = postSliceResult->consumeMidiEditCc;
    state.sessionSamples = postSliceResult->sessionSamples;
    state.selectedSessionSampleId = postSliceResult->selectedSessionSampleId;
    state.stemComputeDevice = postSliceResult->stemComputeDevice;
    state.sampleParams = postSliceResult->sampleParams;

    ReadResult ok;
    ok.state = std::move (state);
    return ok;
}
