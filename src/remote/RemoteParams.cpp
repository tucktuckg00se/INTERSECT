#include "RemoteParams.h"

namespace RemoteParams
{
namespace
{
using F = ParamFieldIds;

constexpr float kMs = 0.001f;    // ms (remote) -> seconds (slice)
constexpr float kPct = 0.01f;    // % (remote) -> 0..1 (slice)
constexpr float kMaxFrames = 2.0e9f;

// Ranges mirror SampleParams.cpp (sample level) and the slice clamps in handleCommand.
const std::vector<Info>& table()
{
    static const std::vector<Info> infos {
        // TIME/PITCH
        { F::FieldBpm,            "BPM",     "TIME/PITCH", "bpm", 20.0f,   999.0f,  0.0f, "",                        kLevelBoth,  kLockBpm },
        { F::FieldPitch,          "PITCH",   "TIME/PITCH", "st",  -48.0f,  48.0f,   1.0f, "",                        kLevelBoth,  kLockPitch },
        { F::FieldCentsDetune,    "TUNE",    "TIME/PITCH", "ct",  -100.0f, 100.0f,  1.0f, "",                        kLevelBoth,  kLockCentsDetune },
        { F::FieldStretchEnabled, "STRETCH", "TIME/PITCH", "",    0.0f,    1.0f,    1.0f, "Off;On",                  kLevelBoth,  kLockStretch },
        { F::FieldAlgorithm,      "ALGO",    "TIME/PITCH", "",    0.0f,    2.0f,    1.0f, "Repitch;Signalsmith;Bungee", kLevelBoth, kLockAlgorithm },
        { F::FieldRepitchMode,    "MODE",    "TIME/PITCH", "",    0.0f,    2.0f,    1.0f, "Linear;Cubic;Sinc",       kLevelBoth,  kLockRepitchMode },
        { F::FieldTonality,       "TONAL",   "TIME/PITCH", "Hz",  0.0f,    8000.0f, 0.0f, "",                        kLevelBoth,  kLockTonality },
        { F::FieldFormant,        "FMNT",    "TIME/PITCH", "st",  -24.0f,  24.0f,   0.0f, "",                        kLevelBoth,  kLockFormant },
        { F::FieldFormantComp,    "FMNT C",  "TIME/PITCH", "",    0.0f,    1.0f,    1.0f, "Off;On",                  kLevelBoth,  kLockFormantComp },
        { F::FieldGrainMode,      "GRAIN",   "TIME/PITCH", "",    0.0f,    2.0f,    1.0f, "Fast;Normal;Smooth",      kLevelBoth,  kLockGrainMode },

        // FILTER
        { F::FieldFilterEnabled,    "FILTER", "FILTER", "",   0.0f,  1.0f,     1.0f, "Off;On",          kLevelBoth, kLockFilterEnabled },
        { F::FieldFilterType,       "TYPE",   "FILTER", "",   0.0f,  3.0f,     1.0f, "LP;HP;BP;NT",     kLevelBoth, kLockFilterType },
        { F::FieldFilterSlope,      "SLOPE",  "FILTER", "",   0.0f,  1.0f,     1.0f, "12dB;24dB",       kLevelBoth, kLockFilterSlope },
        { F::FieldFilterCutoff,     "CUT",    "FILTER", "Hz", kMinFilterCutoffHz, kMaxFilterCutoffHz, 0.0f, "", kLevelBoth, kLockFilterCutoff },
        { F::FieldFilterReso,       "RESO",   "FILTER", "%",  0.0f,  100.0f,   0.0f, "",                kLevelBoth, kLockFilterReso },
        { F::FieldFilterDrive,      "DRIVE",  "FILTER", "%",  0.0f,  100.0f,   0.0f, "",                kLevelBoth, kLockFilterDrive },
        { F::FieldFilterAsym,       "ASYM",   "FILTER", "%",  0.0f,  100.0f,   0.0f, "",                kLevelBoth, kLockFilterAsym },
        { F::FieldFilterKeyTrack,   "KEY",    "FILTER", "%",  0.0f,  100.0f,   0.0f, "",                kLevelBoth, kLockFilterKeyTrack },
        { F::FieldFilterEnvAmount,  "AMT",    "FILTER", "st", -96.0f, 96.0f,   0.0f, "",                kLevelBoth, kLockFilterEnvAmount },
        { F::FieldFilterEnvAttack,  "F ATK",  "FILTER", "ms", 0.0f,  10000.0f, 0.0f, "",                kLevelBoth, kLockFilterEnvAttack,  kMs },
        { F::FieldFilterEnvDecay,   "F DEC",  "FILTER", "ms", 0.0f,  10000.0f, 0.0f, "",                kLevelBoth, kLockFilterEnvDecay,   kMs },
        { F::FieldFilterEnvSustain, "F SUS",  "FILTER", "%",  0.0f,  100.0f,   0.0f, "",                kLevelBoth, kLockFilterEnvSustain, kPct },
        { F::FieldFilterEnvRelease, "F REL",  "FILTER", "ms", 0.0f,  10000.0f, 0.0f, "",                kLevelBoth, kLockFilterEnvRelease, kMs },

        // AMP
        { F::FieldAttack,      "ATK",  "AMP", "ms", 0.0f,    1000.0f, 0.0f, "",       kLevelBoth, kLockAttack,  kMs },
        { F::FieldDecay,       "DEC",  "AMP", "ms", 0.0f,    5000.0f, 0.0f, "",       kLevelBoth, kLockDecay,   kMs },
        { F::FieldSustain,     "SUS",  "AMP", "%",  0.0f,    100.0f,  0.0f, "",       kLevelBoth, kLockSustain, kPct },
        { F::FieldRelease,     "REL",  "AMP", "ms", 0.0f,    5000.0f, 0.0f, "",       kLevelBoth, kLockRelease, kMs },
        { F::FieldReleaseTail, "TAIL", "AMP", "",   0.0f,    1.0f,    1.0f, "Off;On", kLevelBoth, kLockReleaseTail },
        { F::FieldVolume,      "GAIN", "AMP", "dB", -100.0f, 24.0f,   0.0f, "",       kLevelBoth, kLockVolume },

        // PLAYBACK
        { F::FieldReverse,    "REV",    "PLAYBACK", "",    0.0f, 1.0f,                  1.0f, "Off;On",      kLevelBoth,  kLockReverse },
        { F::FieldLoop,       "LOOP",   "PLAYBACK", "",    0.0f, 2.0f,                  1.0f, "Off;Loop;PP", kLevelBoth,  kLockLoop },
        { F::FieldOneShot,    "1SHOT",  "PLAYBACK", "",    0.0f, 1.0f,                  1.0f, "Off;On",      kLevelBoth,  kLockOneShot },
        { F::FieldCrossfade,  "FADE",   "PLAYBACK", "%",   0.0f, 100.0f,                0.0f, "",            kLevelBoth,  kLockCrossfade },
        { F::FieldMuteGroup,  "MUTE",   "PLAYBACK", "",    0.0f, (float) kMaxMuteGroups, 1.0f, "",           kLevelBoth,  kLockMuteGroup },
        { F::FieldOutputBus,  "OUT",    "PLAYBACK", "",    0.0f, (float) (kMaxOutputBuses - 1), 1.0f, "",    kLevelSlice, kLockOutputBus },
        { F::FieldLoopStart,  "L START", "PLAYBACK", "smp", 0.0f, kMaxFrames,           1.0f, "",            kLevelSlice, kLockLoopStart },
        { F::FieldLoopLength, "L LEN",  "PLAYBACK", "smp", 0.0f, kMaxFrames,            1.0f, "",            kLevelSlice, kLockLoopLength },

        // NOTES
        { F::FieldMidiNote,      "NOTE", "NOTES", "", 0.0f, (float) kMaxMidiNote, 1.0f, "", kLevelSlice, 0 },
        { F::FieldHighNote,      "HIGH", "NOTES", "", 0.0f, (float) kMaxMidiNote, 1.0f, "", kLevelSlice, 0 },
        { F::FieldSliceRootNote, "ROOT", "NOTES", "", 0.0f, (float) kMaxMidiNote, 1.0f, "", kLevelSlice, 0 },
    };
    return infos;
}
} // namespace

const std::vector<Info>& all()
{
    return table();
}

const Info* find (int field)
{
    for (const auto& info : table())
        if (info.field == field)
            return &info;
    return nullptr;
}

float readSliceField (const Slice& s, int field)
{
    switch (field)
    {
        case F::FieldBpm:              return s.bpm;
        case F::FieldPitch:            return s.pitchSemitones;
        case F::FieldCentsDetune:      return s.centsDetune;
        case F::FieldAlgorithm:        return (float) s.algorithm;
        case F::FieldRepitchMode:      return (float) s.repitchMode;
        case F::FieldAttack:           return s.attackSec / kMs;
        case F::FieldDecay:            return s.decaySec / kMs;
        case F::FieldSustain:          return s.sustainLevel / kPct;
        case F::FieldRelease:          return s.releaseSec / kMs;
        case F::FieldMuteGroup:        return (float) s.muteGroup;
        case F::FieldMidiNote:         return (float) s.midiNote;
        case F::FieldStretchEnabled:   return s.stretchEnabled ? 1.0f : 0.0f;
        case F::FieldTonality:         return s.tonalityHz;
        case F::FieldFormant:          return s.formantSemitones;
        case F::FieldFormantComp:      return s.formantComp ? 1.0f : 0.0f;
        case F::FieldGrainMode:        return (float) s.grainMode;
        case F::FieldVolume:           return s.volume;
        case F::FieldReleaseTail:      return s.releaseTail ? 1.0f : 0.0f;
        case F::FieldReverse:          return s.reverse ? 1.0f : 0.0f;
        case F::FieldOutputBus:        return (float) s.outputBus;
        case F::FieldLoop:             return (float) s.loopMode;
        case F::FieldOneShot:          return s.oneShot ? 1.0f : 0.0f;
        case F::FieldFilterEnabled:    return s.filterEnabled ? 1.0f : 0.0f;
        case F::FieldFilterType:       return (float) s.filterType;
        case F::FieldFilterSlope:      return (float) s.filterSlope;
        case F::FieldFilterCutoff:     return s.filterCutoff;
        case F::FieldFilterReso:       return s.filterReso;
        case F::FieldFilterDrive:      return s.filterDrive;
        case F::FieldFilterKeyTrack:   return s.filterKeyTrack;
        case F::FieldFilterEnvAttack:  return s.filterEnvAttackSec / kMs;
        case F::FieldFilterEnvDecay:   return s.filterEnvDecaySec / kMs;
        case F::FieldFilterEnvSustain: return s.filterEnvSustain / kPct;
        case F::FieldFilterEnvRelease: return s.filterEnvReleaseSec / kMs;
        case F::FieldFilterEnvAmount:  return s.filterEnvAmount;
        case F::FieldFilterAsym:       return s.filterAsym;
        case F::FieldCrossfade:        return s.crossfadePct;
        case F::FieldLoopStart:        return (float) s.loopStartOffset;
        case F::FieldLoopLength:       return (float) s.loopLength;
        case F::FieldHighNote:         return (float) s.highNote;
        case F::FieldSliceRootNote:    return (float) s.sliceRootNote;
        default:                       return 0.0f;
    }
}

float resolveSliceField (const Slice& slice, const SampleParams& sampleLevel, int field, bool& locked)
{
    const auto* info = find (field);
    if (info == nullptr)
    {
        locked = false;
        return 0.0f;
    }

    if ((info->levels & kLevelSample) == 0)
    {
        locked = info->lockBit == 0 || (slice.lockMask & info->lockBit) != 0;
        return readSliceField (slice, field);
    }

    locked = (slice.lockMask & info->lockBit) != 0;
    return locked ? readSliceField (slice, field) : sampleLevel.getField (field);
}

float toSliceUnits (int field, float remoteValue)
{
    if (const auto* info = find (field))
        return remoteValue * info->sliceScale;
    return remoteValue;
}
}
