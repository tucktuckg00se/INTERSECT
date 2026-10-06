#pragma once
#include <juce_core/juce_core.h>
#include "../PluginProcessor.h"

namespace intersectMidi
{
    /** Builds a type-0 Standard MIDI File that plays the kit's active slices in time order.

        One note per slice (its midiNote, timed from its sample position at the sample's
        rate), with a tempo meta event carrying the kit BPM. Returns an empty block when
        there is nothing to export.
    */
    juce::MemoryBlock buildKitMidiFile (const IntersectProcessor::UiSliceSnapshot& ui, float globalBpm);

    /** Writes the kit's slices to `dest` as a .mid file. Returns false if there are no active slices. */
    bool writeKitMidiFile (const juce::File& dest, const IntersectProcessor::UiSliceSnapshot& ui, float globalBpm);
}
