#pragma once

#include <cstdint>
#include <vector>
#include <juce_core/juce_core.h>

#include "../audio/Slice.h"
#include "../params/SampleParams.h"

/** Self-description of every editable parameter, sent to remote controllers so they can build
    their pages without hard-coding INTERSECT's parameter set (`/intersect/params/describe`).

    Remote values always use SAMPLE-tab units (ms, %, dB, Hz, semitones), for both levels.
    Slices store a few fields in engine units (seconds, 0..1), so slice reads and writes go
    through toSliceUnits() / fromSliceUnits().
*/
namespace RemoteParams
{
enum Level : int
{
    kLevelSample = 1,
    kLevelSlice  = 2,
    kLevelBoth   = kLevelSample | kLevelSlice,
};

struct Info
{
    int field = 0;              // ParamFieldIds::SliceParamField, stable and append-only
    const char* name = "";      // short label, as in the signal chain
    const char* group = "";     // TIME/PITCH, FILTER, AMP, PLAYBACK, NOTES
    const char* unit = "";      // "", "ms", "%", "dB", "Hz", "st", "ct", "bpm", "smp"
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float step = 0.0f;          // 0 = continuous
    const char* choices = "";   // ';'-separated labels for choice/bool fields, else empty
    int levels = kLevelBoth;
    uint64_t lockBit = 0;       // 0 = not lockable (slice-only fields)
    float sliceScale = 1.0f;    // slice engine value = remote value * sliceScale
};

const std::vector<Info>& all();
const Info* find (int field);

/** The slice's own stored value for a field, in remote units. */
float readSliceField (const Slice& slice, int field);

/** The value a slice actually uses: its own value when locked (or slice-only), otherwise
    the value of its sample. `locked` reports which one was used. */
float resolveSliceField (const Slice& slice, const SampleParams& sampleLevel, int field, bool& locked);

/** Remote units -> the units CmdSetSliceParam expects for this field. */
float toSliceUnits (int field, float remoteValue);
}
