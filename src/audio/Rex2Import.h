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

    Because tails make the rendered buffer longer than the original loop, each
    span also carries the slice's musical position (in quarter-note beats from
    the loop start) so the original groove can be recovered, e.g. for MIDI export.
*/
namespace Rex2Import
{
    struct SliceSpan
    {
        int startSample = 0;   // first frame of this slice in the rendered buffer
        int endSample   = 0;   // one past the last frame
        double beat     = 0.0; // musical position of the slice in quarter notes
    };

    struct DecodedLoop
    {
        juce::AudioBuffer<float> stereo;      // rendered slices concatenated, always stereo
        double sampleRate = 0.0;              // rate of `stereo`
        double sourceSampleRate = 0.0;        // native rate of the .rx2 file
        int sourceNumFrames = 0;              // rendered length at the native rate
        float tempoBpm = 120.0f;              // tempo of the audio (REX original_tempo, else tempo)
        std::vector<SliceSpan> slices;        // one entry per REX2 slice, in order
    };

    struct FileInfo
    {
        double sampleRate = 0.0;
        int numChannels = 0;
        int bitsPerSample = 0;
        double lengthSeconds = 0.0;
    };

    /** Decodes a .rx2 file. Returns false (and leaves `out` empty) on any failure. */
    bool decodeFile (const juce::File& file, double targetSampleRate, DecodedLoop& out);

    /** Reads only the header metadata (for the file browser). */
    bool readInfo (const juce::File& file, FileInfo& out);
}
