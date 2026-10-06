#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

#include "../Constants.h"
#include "ParamFields.h"

/** Sample-level parameters: the defaults every slice of ONE session sample inherits unless
    the slice locks its own value.

    INTERSECT has three levels:
      - plugin level  — shared by the whole kit (max voices, root note, NRPN, UI scale);
      - sample level  — this struct, stored separately for each session sample;
      - slice level   — Slice fields, used only when the matching lock bit is set.

    Members are in engine units (seconds, 0..1). getField()/setField() use the units of the
    SAMPLE tab and of the legacy APVTS "default*" parameters (ms, %), which is also what the
    saved state stores.
*/
struct SampleParams
{
    float bpm = 120.0f;
    float pitchSemitones = 0.0f;
    float centsDetune = 0.0f;
    int algorithm = 0;
    int repitchMode = (int) RepitchMode::Linear;

    float attackSec = 0.005f;
    float decaySec = 0.1f;
    float sustain = 1.0f;
    float releaseSec = 0.02f;

    int muteGroup = 0;
    bool stretchEnabled = false;
    bool reverse = false;
    int loopMode = 0;
    bool oneShot = false;
    bool releaseTail = false;

    float tonalityHz = 0.0f;
    float formantSemitones = 0.0f;
    bool formantComp = false;
    int grainMode = 1;

    float volumeDb = 0.0f;
    float crossfadePct = 0.0f;

    bool filterEnabled = false;
    int filterType = 0;
    int filterSlope = 0;
    float filterCutoffHz = 8200.0f;
    float filterReso = 0.0f;
    float filterDrive = 0.0f;
    float filterAsym = 0.0f;
    float filterKeyTrack = 0.0f;
    float filterEnvAttackSec = 0.0f;
    float filterEnvDecaySec = 0.0f;
    float filterEnvSustain = 1.0f;
    float filterEnvReleaseSec = 0.0f;
    float filterEnvAmount = 0.0f;

    static constexpr int kNumFields = 34;

    /** Every field id that belongs to the sample level, in a stable order. */
    static const std::array<int, kNumFields>& fieldIds();

    /** Factory defaults, used for every newly added sample. */
    static SampleParams factoryDefaults() { return {}; }

    /** The values of the legacy shared APVTS "default*" parameters. Only used to migrate
        projects saved before sample parameters were stored per sample. */
    static SampleParams fromLegacyApvts (const juce::AudioProcessorValueTreeState& apvts);

    /** Reads a field in SAMPLE-tab / saved-state units. Returns 0 for non-sample fields. */
    float getField (int field) const;

    /** Writes a field in SAMPLE-tab / saved-state units, clamped to its range.
        Returns false for fields that are not sample-level. Real-time safe. */
    bool setField (int field, float value);

    bool operator== (const SampleParams& other) const;
    bool operator!= (const SampleParams& other) const { return ! (*this == other); }
};
