#pragma once

#include "../audio/SampleData.h"
#include "../audio/Slice.h"
#include "../audio/StemSeparation.h"
#include "../params/SampleParamTable.h"
#include <optional>
#include <vector>

/** Everything INTERSECT saves for a project or preset (the getStateInformation blob), as plain
    data. StateFormat reads and writes it; the processor captures one from its engine and applies
    one back (IntersectProcessor::captureSessionState / applySessionState). No threads, no
    processor: this file only knows the format.

    Format: a base version (19-24) followed by an append-only extension block ("RPM1", currently
    v8). CLAUDE.md's Serialization section describes each version. Never change what an existing
    version means; tests/fixtures/expected/ pins the bytes a save writes. */
struct SessionState
{
    int version = 0;                 // base version the state was read as
    juce::String apvtsXml;           // parsed when applied; unparseable XML leaves the parameters as they are
    float zoom = 1.0f;
    float scroll = 0.0f;
    int selectedSlice = -1;
    bool midiSelectsSlice = false;
    int rootNote = kDefaultRootNote;
    std::vector<Slice> slices;       // at most SliceManager::kMaxSlices; sanitised when read

    // The first sample's file and the kit's decoded/source length, kept for single-file states
    // and for builds that predate multi-sample kits.
    juce::String filePath;
    juce::String fileName;
    int decodedNumFrames = 0;
    double decodedSampleRate = 0.0;
    int sourceNumFrames = 0;
    double sourceSampleRate = 0.0;

    bool snapToZeroCrossing = false;
    bool midiEditEnabled = false;
    int midiEditChannel = 0;
    bool consumeMidiEditCc = true;

    std::vector<SampleData::SessionSample> sessionSamples;   // id, path, name and stem metadata
    int selectedSessionSampleId = -1;
    std::optional<StemComputeDevice> stemComputeDevice;      // extension v6
    std::optional<SampleParamTable> sampleParams;            // extension v7; absent: migrate the legacy APVTS values
};

namespace StateFormat
{
    constexpr int kCurrentStateVersion = 24;
    constexpr int kOldestStateVersion = 19;

    /** Writes `state` in the current format (base v24 + extension v8). */
    void write (const SessionState& state, juce::OutputStream& out);

    struct ReadResult
    {
        std::optional<SessionState> state;
        juce::String error;   // why `state` is empty: an unsupported version or damaged data
    };

    /** Reads a state written by any supported version. Fails without side effects. */
    ReadResult read (const void* data, size_t size);
}
