#include "SampleParams.h"
#include "ParamIds.h"
#include <cmath>

namespace
{
using F = ParamFieldIds;

struct FieldSpec
{
    int field;
    const juce::String* legacyParamId;   // APVTS id of the old shared parameter
    float minValue;                      // range in SAMPLE-tab units
    float maxValue;
};

// Ranges mirror ParamLayout.cpp. Repitch mode allows Sinc (2) like slices do.
const std::array<FieldSpec, SampleParams::kNumFields>& specs()
{
    static const std::array<FieldSpec, SampleParams::kNumFields> table {{
        { F::FieldBpm,              &ParamIds::defaultBpm,              20.0f,   999.0f },
        { F::FieldPitch,            &ParamIds::defaultPitch,            -48.0f,  48.0f },
        { F::FieldCentsDetune,      &ParamIds::defaultCentsDetune,      -100.0f, 100.0f },
        { F::FieldAlgorithm,        &ParamIds::defaultAlgorithm,        0.0f,    2.0f },
        { F::FieldRepitchMode,      &ParamIds::defaultRepitchMode,      0.0f,    2.0f },
        { F::FieldAttack,           &ParamIds::defaultAttack,           0.0f,    1000.0f },
        { F::FieldDecay,            &ParamIds::defaultDecay,            0.0f,    5000.0f },
        { F::FieldSustain,          &ParamIds::defaultSustain,          0.0f,    100.0f },
        { F::FieldRelease,          &ParamIds::defaultRelease,          0.0f,    5000.0f },
        { F::FieldMuteGroup,        &ParamIds::defaultMuteGroup,        0.0f,    (float) kMaxMuteGroups },
        { F::FieldLoop,             &ParamIds::defaultLoop,             0.0f,    2.0f },
        { F::FieldStretchEnabled,   &ParamIds::defaultStretchEnabled,   0.0f,    1.0f },
        { F::FieldTonality,         &ParamIds::defaultTonality,         0.0f,    8000.0f },
        { F::FieldFormant,          &ParamIds::defaultFormant,          -24.0f,  24.0f },
        { F::FieldFormantComp,      &ParamIds::defaultFormantComp,      0.0f,    1.0f },
        { F::FieldGrainMode,        &ParamIds::defaultGrainMode,        0.0f,    2.0f },
        { F::FieldReleaseTail,      &ParamIds::defaultReleaseTail,      0.0f,    1.0f },
        { F::FieldReverse,          &ParamIds::defaultReverse,          0.0f,    1.0f },
        { F::FieldOneShot,          &ParamIds::defaultOneShot,          0.0f,    1.0f },
        { F::FieldVolume,           &ParamIds::masterVolume,            -100.0f, 24.0f },
        { F::FieldCrossfade,        &ParamIds::defaultCrossfade,        0.0f,    100.0f },
        { F::FieldFilterEnabled,    &ParamIds::defaultFilterEnabled,    0.0f,    1.0f },
        { F::FieldFilterType,       &ParamIds::defaultFilterType,       0.0f,    3.0f },
        { F::FieldFilterSlope,      &ParamIds::defaultFilterSlope,      0.0f,    1.0f },
        { F::FieldFilterCutoff,     &ParamIds::defaultFilterCutoff,     kMinFilterCutoffHz, kMaxFilterCutoffHz },
        { F::FieldFilterReso,       &ParamIds::defaultFilterReso,       0.0f,    100.0f },
        { F::FieldFilterDrive,      &ParamIds::defaultFilterDrive,      0.0f,    100.0f },
        { F::FieldFilterAsym,       &ParamIds::defaultFilterAsym,       0.0f,    100.0f },
        { F::FieldFilterKeyTrack,   &ParamIds::defaultFilterKeyTrack,   0.0f,    100.0f },
        { F::FieldFilterEnvAttack,  &ParamIds::defaultFilterEnvAttack,  0.0f,    10000.0f },
        { F::FieldFilterEnvDecay,   &ParamIds::defaultFilterEnvDecay,   0.0f,    10000.0f },
        { F::FieldFilterEnvSustain, &ParamIds::defaultFilterEnvSustain, 0.0f,    100.0f },
        { F::FieldFilterEnvRelease, &ParamIds::defaultFilterEnvRelease, 0.0f,    10000.0f },
        { F::FieldFilterEnvAmount,  &ParamIds::defaultFilterEnvAmount,  -96.0f,  96.0f },
    }};
    return table;
}

const FieldSpec* findSpec (int field)
{
    for (const auto& spec : specs())
        if (spec.field == field)
            return &spec;
    return nullptr;
}
} // namespace

const std::array<int, SampleParams::kNumFields>& SampleParams::fieldIds()
{
    static const auto ids = []
    {
        std::array<int, kNumFields> out {};
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = specs()[i].field;
        return out;
    }();
    return ids;
}

SampleParams SampleParams::fromLegacyApvts (const juce::AudioProcessorValueTreeState& apvts)
{
    SampleParams params;
    for (const auto& spec : specs())
        if (auto* raw = apvts.getRawParameterValue (*spec.legacyParamId))
            params.setField (spec.field, raw->load());
    return params;
}

float SampleParams::getField (int field) const
{
    switch (field)
    {
        case F::FieldBpm:              return bpm;
        case F::FieldPitch:            return pitchSemitones;
        case F::FieldCentsDetune:      return centsDetune;
        case F::FieldAlgorithm:        return (float) algorithm;
        case F::FieldRepitchMode:      return (float) repitchMode;
        case F::FieldAttack:           return attackSec * 1000.0f;
        case F::FieldDecay:            return decaySec * 1000.0f;
        case F::FieldSustain:          return sustain * 100.0f;
        case F::FieldRelease:          return releaseSec * 1000.0f;
        case F::FieldMuteGroup:        return (float) muteGroup;
        case F::FieldLoop:             return (float) loopMode;
        case F::FieldStretchEnabled:   return stretchEnabled ? 1.0f : 0.0f;
        case F::FieldTonality:         return tonalityHz;
        case F::FieldFormant:          return formantSemitones;
        case F::FieldFormantComp:      return formantComp ? 1.0f : 0.0f;
        case F::FieldGrainMode:        return (float) grainMode;
        case F::FieldReleaseTail:      return releaseTail ? 1.0f : 0.0f;
        case F::FieldReverse:          return reverse ? 1.0f : 0.0f;
        case F::FieldOneShot:          return oneShot ? 1.0f : 0.0f;
        case F::FieldVolume:           return volumeDb;
        case F::FieldCrossfade:        return crossfadePct;
        case F::FieldFilterEnabled:    return filterEnabled ? 1.0f : 0.0f;
        case F::FieldFilterType:       return (float) filterType;
        case F::FieldFilterSlope:      return (float) filterSlope;
        case F::FieldFilterCutoff:     return filterCutoffHz;
        case F::FieldFilterReso:       return filterReso;
        case F::FieldFilterDrive:      return filterDrive;
        case F::FieldFilterAsym:       return filterAsym;
        case F::FieldFilterKeyTrack:   return filterKeyTrack;
        case F::FieldFilterEnvAttack:  return filterEnvAttackSec * 1000.0f;
        case F::FieldFilterEnvDecay:   return filterEnvDecaySec * 1000.0f;
        case F::FieldFilterEnvSustain: return filterEnvSustain * 100.0f;
        case F::FieldFilterEnvRelease: return filterEnvReleaseSec * 1000.0f;
        case F::FieldFilterEnvAmount:  return filterEnvAmount;
        default:                       return 0.0f;
    }
}

bool SampleParams::setField (int field, float value)
{
    const auto* spec = findSpec (field);
    if (spec == nullptr || ! std::isfinite (value))
        return false;

    const float v = juce::jlimit (spec->minValue, spec->maxValue, value);
    const int i = juce::roundToInt (v);
    const bool b = v > 0.5f;

    switch (field)
    {
        case F::FieldBpm:              bpm = v;                          break;
        case F::FieldPitch:            pitchSemitones = v;               break;
        case F::FieldCentsDetune:      centsDetune = v;                  break;
        case F::FieldAlgorithm:        algorithm = i;                    break;
        case F::FieldRepitchMode:      repitchMode = i;                  break;
        case F::FieldAttack:           attackSec = v / 1000.0f;          break;
        case F::FieldDecay:            decaySec = v / 1000.0f;           break;
        case F::FieldSustain:          sustain = v / 100.0f;             break;
        case F::FieldRelease:          releaseSec = v / 1000.0f;         break;
        case F::FieldMuteGroup:        muteGroup = i;                    break;
        case F::FieldLoop:             loopMode = i;                     break;
        case F::FieldStretchEnabled:   stretchEnabled = b;               break;
        case F::FieldTonality:         tonalityHz = v;                   break;
        case F::FieldFormant:          formantSemitones = v;             break;
        case F::FieldFormantComp:      formantComp = b;                  break;
        case F::FieldGrainMode:        grainMode = i;                    break;
        case F::FieldReleaseTail:      releaseTail = b;                  break;
        case F::FieldReverse:          reverse = b;                      break;
        case F::FieldOneShot:          oneShot = b;                      break;
        case F::FieldVolume:           volumeDb = v;                     break;
        case F::FieldCrossfade:        crossfadePct = v;                 break;
        case F::FieldFilterEnabled:    filterEnabled = b;                break;
        case F::FieldFilterType:       filterType = i;                   break;
        case F::FieldFilterSlope:      filterSlope = i;                  break;
        case F::FieldFilterCutoff:     filterCutoffHz = v;               break;
        case F::FieldFilterReso:       filterReso = v;                   break;
        case F::FieldFilterDrive:      filterDrive = v;                  break;
        case F::FieldFilterAsym:       filterAsym = v;                   break;
        case F::FieldFilterKeyTrack:   filterKeyTrack = v;               break;
        case F::FieldFilterEnvAttack:  filterEnvAttackSec = v / 1000.0f; break;
        case F::FieldFilterEnvDecay:   filterEnvDecaySec = v / 1000.0f;  break;
        case F::FieldFilterEnvSustain: filterEnvSustain = v / 100.0f;    break;
        case F::FieldFilterEnvRelease: filterEnvReleaseSec = v / 1000.0f; break;
        case F::FieldFilterEnvAmount:  filterEnvAmount = v;              break;
        default:                       return false;
    }
    return true;
}

bool SampleParams::operator== (const SampleParams& other) const
{
    for (int field : fieldIds())
        if (getField (field) != other.getField (field))
            return false;
    return true;
}
