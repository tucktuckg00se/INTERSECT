#pragma once

#include "Slice.h"
#include "../params/SampleParams.h"
#include <cstdint>

/** Edits that touch only the slice itself: no processor state, no allocation, safe on the audio
    thread. handleCommand applies them (CmdSetSliceParam, CmdToggleLock). */
namespace SliceEdits
{
    /** Sets one field (ParamFieldIds) from a SLICE-tab edit, in SAMPLE-tab units. A value equal to
        the slice's sample-level value clears that field's lock; anything else sets it. Returns true
        when a note field changed, so the caller rebuilds the MIDI map. */
    bool setField (Slice& slice, const SampleParams& sampleLevel, int field, float value);

    /** Copies the sample-level value for `lockBit` into the slice (when that lock is switched on). */
    void copySampleParam (Slice& slice, const SampleParams& sampleLevel, uint64_t lockBit);
}
