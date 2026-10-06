#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

/**
    REX2 (.rx2) loop import built on the VelociLoops library.

    A REX2 file stores one full loop plus per-slice metadata (positions, lengths,
    tempo). decodeFile() renders every slice with VelociLoops (including any
    transient-stretch tails), concatenates them into a single stereo buffer and
    reports the span of each slice inside that buffer, so the caller can turn
    them straight into INTERSECT slices.
*/
namespace Rex2Import
{
    struct SliceSpan
    {
        int startSample = 0;   // first frame of this slice in the rendered buffer
        int endSample   = 0;   // one past the last frame
    };

    struct DecodedLoop
    {
        juce::AudioBuffer<float> stereo;      // rendered slices concatenated, always stereo
        double sampleRate = 0.0;              // rate of `stereo`
        float tempoBpm = 120.0f;              // loop tempo from the file header
        std::vector<SliceSpan> slices;        // one entry per REX2 slice, in order
    };

    /** Decodes a .rx2 file. Returns false (and leaves `out` empty) on any failure. */
    bool decodeFile (const juce::File& file, double targetSampleRate, DecodedLoop& out);
}
