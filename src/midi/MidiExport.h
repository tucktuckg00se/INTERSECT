#pragma once
#include <juce_core/juce_core.h>
#include "../PluginProcessor.h"

namespace intersectMidi
{
    /** Builds a type-0 Standard MIDI File that plays one session sample's active slices in time order.

        One note per slice (its MIDI note), timed from the slice's position in `sample`.
        REX2 samples are timed from their beat anchors, so the original groove survives the
        rendered slice tails; other samples are timed at `sampleBpm`, the sample's own BPM (a slice's locked BPM wins).
        When `sample` is null every slice is exported against the whole session timeline.
        Returns an empty block when there is nothing to export.
    */
    juce::MemoryBlock buildKitMidiFile (const IntersectProcessor::UiSliceSnapshot& ui,
                                        const SampleData::SessionSample* sample,
                                        float sampleBpm);

    /** Writes buildKitMidiFile() to `dest`. Returns false if there is nothing to export or the write fails. */
    bool writeKitMidiFile (const juce::File& dest,
                           const IntersectProcessor::UiSliceSnapshot& ui,
                           const SampleData::SessionSample* sample,
                           float sampleBpm);
}
