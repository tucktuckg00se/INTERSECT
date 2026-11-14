#include "SliceEdits.h"
#include <cmath>

bool SliceEdits::setField (Slice& s, const SampleParams& sampleLevel, int field, float val)
{
    constexpr float kCompareTolerance = 1.0e-4f;

    auto setFloatField = [&s] (float& target, float newValue, float sampleValue, uint64_t lockBit)
    {
        target = newValue;
        if (std::abs (target - sampleValue) <= kCompareTolerance)
            s.lockMask &= ~lockBit;
        else
            s.lockMask |= lockBit;
    };

    auto setIntField = [&s] (int& target, int newValue, int sampleValue, uint64_t lockBit)
    {
        target = newValue;
        if (target == sampleValue)
            s.lockMask &= ~lockBit;
        else
            s.lockMask |= lockBit;
    };

    auto setBoolField = [&s] (bool& target, bool newValue, bool sampleValue, uint64_t lockBit)
    {
        target = newValue;
        if (target == sampleValue)
            s.lockMask &= ~lockBit;
        else
            s.lockMask |= lockBit;
    };

    bool rebuildMidiMap = false;
    switch (field)
    {
        case ParamFieldIds::FieldBpm:
            setFloatField (s.bpm, val, sampleLevel.bpm, kLockBpm);
            break;
        case ParamFieldIds::FieldPitch:
            setFloatField (s.pitchSemitones, val, sampleLevel.pitchSemitones, kLockPitch);
            break;
        case ParamFieldIds::FieldAlgorithm:
            setIntField (s.algorithm, (int) val, sampleLevel.algorithm, kLockAlgorithm);
            break;
        case ParamFieldIds::FieldRepitchMode:
            setIntField (s.repitchMode, (int) val, sampleLevel.repitchMode, kLockRepitchMode);
            break;
        case ParamFieldIds::FieldAttack:
            setFloatField (s.attackSec, val, sampleLevel.attackSec, kLockAttack);
            break;
        case ParamFieldIds::FieldDecay:
            setFloatField (s.decaySec, val, sampleLevel.decaySec, kLockDecay);
            break;
        case ParamFieldIds::FieldSustain:
            setFloatField (s.sustainLevel, val, sampleLevel.sustain, kLockSustain);
            break;
        case ParamFieldIds::FieldRelease:
            setFloatField (s.releaseSec, val, sampleLevel.releaseSec, kLockRelease);
            break;
        case ParamFieldIds::FieldMuteGroup:
            setIntField (s.muteGroup, (int) val, sampleLevel.muteGroup, kLockMuteGroup);
            break;
        case ParamFieldIds::FieldStretchEnabled:
            setBoolField (s.stretchEnabled, val > 0.5f, sampleLevel.stretchEnabled, kLockStretch);
            break;
        case ParamFieldIds::FieldTonality:
            setFloatField (s.tonalityHz, val, sampleLevel.tonalityHz, kLockTonality);
            break;
        case ParamFieldIds::FieldFormant:
            setFloatField (s.formantSemitones, val, sampleLevel.formantSemitones, kLockFormant);
            break;
        case ParamFieldIds::FieldFormantComp:
            setBoolField (s.formantComp, val > 0.5f, sampleLevel.formantComp, kLockFormantComp);
            break;
        case ParamFieldIds::FieldGrainMode:
            setIntField (s.grainMode, (int) val, sampleLevel.grainMode, kLockGrainMode);
            break;
        case ParamFieldIds::FieldVolume:
            setFloatField (s.volume, val, sampleLevel.volumeDb, kLockVolume);
            break;
        case ParamFieldIds::FieldReleaseTail:
            setBoolField (s.releaseTail, val > 0.5f, sampleLevel.releaseTail, kLockReleaseTail);
            break;
        case ParamFieldIds::FieldReverse:
            setBoolField (s.reverse, val > 0.5f, sampleLevel.reverse, kLockReverse);
            break;
        case ParamFieldIds::FieldOutputBus:
            s.outputBus = juce::jlimit (0, kMaxOutputBuses - 1, (int) val);
            s.lockMask |= kLockOutputBus;
            break;
        case ParamFieldIds::FieldLoop:
            setIntField (s.loopMode, (int) val, sampleLevel.loopMode, kLockLoop);
            break;
        case ParamFieldIds::FieldOneShot:
            setBoolField (s.oneShot, val > 0.5f, sampleLevel.oneShot, kLockOneShot);
            break;
        case ParamFieldIds::FieldCentsDetune:
            setFloatField (s.centsDetune, val, sampleLevel.centsDetune, kLockCentsDetune);
            break;
        case ParamFieldIds::FieldFilterEnabled:
            setBoolField (s.filterEnabled, val > 0.5f, sampleLevel.filterEnabled, kLockFilterEnabled);
            break;
        case ParamFieldIds::FieldFilterType:
            setIntField (s.filterType, juce::jlimit (0, 3, (int) val), sampleLevel.filterType, kLockFilterType);
            break;
        case ParamFieldIds::FieldFilterSlope:
            setIntField (s.filterSlope, juce::jlimit (0, 1, (int) val), sampleLevel.filterSlope, kLockFilterSlope);
            break;
        case ParamFieldIds::FieldFilterCutoff:
            setFloatField (s.filterCutoff, val, sampleLevel.filterCutoffHz, kLockFilterCutoff);
            break;
        case ParamFieldIds::FieldFilterReso:
            setFloatField (s.filterReso, val, sampleLevel.filterReso, kLockFilterReso);
            break;
        case ParamFieldIds::FieldFilterDrive:
            setFloatField (s.filterDrive, val, sampleLevel.filterDrive, kLockFilterDrive);
            break;
        case ParamFieldIds::FieldFilterKeyTrack:
            setFloatField (s.filterKeyTrack, val, sampleLevel.filterKeyTrack, kLockFilterKeyTrack);
            break;
        case ParamFieldIds::FieldFilterEnvAttack:
            setFloatField (s.filterEnvAttackSec, val, sampleLevel.filterEnvAttackSec, kLockFilterEnvAttack);
            break;
        case ParamFieldIds::FieldFilterEnvDecay:
            setFloatField (s.filterEnvDecaySec, val, sampleLevel.filterEnvDecaySec, kLockFilterEnvDecay);
            break;
        case ParamFieldIds::FieldFilterEnvSustain:
            setFloatField (s.filterEnvSustain, val, sampleLevel.filterEnvSustain, kLockFilterEnvSustain);
            break;
        case ParamFieldIds::FieldFilterEnvRelease:
            setFloatField (s.filterEnvReleaseSec, val, sampleLevel.filterEnvReleaseSec, kLockFilterEnvRelease);
            break;
        case ParamFieldIds::FieldFilterEnvAmount:
            setFloatField (s.filterEnvAmount, val, sampleLevel.filterEnvAmount, kLockFilterEnvAmount);
            break;
        case ParamFieldIds::FieldFilterAsym:
            setFloatField (s.filterAsym, val, sampleLevel.filterAsym, kLockFilterAsym);
            break;
        case ParamFieldIds::FieldCrossfade:
            s.crossfadePct = juce::jlimit (0.0f, 100.0f, val);
            s.lockMask |= kLockCrossfade;
            break;
        case ParamFieldIds::FieldLoopStart:
            s.loopStartOffset = juce::jmax (0, (int) val);
            s.lockMask |= kLockLoopStart;
            break;
        case ParamFieldIds::FieldLoopLength:
            s.loopLength = juce::jmax (0, (int) val);
            s.lockMask |= kLockLoopLength;
            break;
        case ParamFieldIds::FieldMidiNote:
        {
            const bool wasSingleNote = (s.highNote == s.midiNote);
            s.midiNote = juce::jlimit (0, kMaxMidiNote, (int) val);

            if (wasSingleNote)
            {
                s.highNote = s.midiNote;
                s.sliceRootNote = s.midiNote;
            }
            else
            {
                if (s.highNote < s.midiNote)
                    s.highNote = s.midiNote;
                s.sliceRootNote = juce::jlimit (s.midiNote, s.highNote, s.sliceRootNote);
            }

            rebuildMidiMap = true;
            break;
        }
        case ParamFieldIds::FieldHighNote:
        {
            s.highNote = juce::jlimit (s.midiNote, kMaxMidiNote, (int) val);
            s.sliceRootNote = juce::jlimit (s.midiNote, s.highNote, s.sliceRootNote);
            rebuildMidiMap = true;
            break;
        }
        case ParamFieldIds::FieldSliceRootNote:
            s.sliceRootNote = juce::jlimit (s.midiNote, s.highNote, (int) val);
            break;
    }
    return rebuildMidiMap;
}

void SliceEdits::copySampleParam (Slice& s, const SampleParams& g, uint64_t bit)
{
    switch (bit)
    {
        case kLockBpm:              s.bpm = g.bpm;                          break;
        case kLockPitch:            s.pitchSemitones = g.pitchSemitones;    break;
        case kLockAlgorithm:        s.algorithm = g.algorithm;              break;
        case kLockRepitchMode:      s.repitchMode = g.repitchMode;          break;
        case kLockAttack:           s.attackSec = g.attackSec;              break;
        case kLockDecay:            s.decaySec = g.decaySec;                break;
        case kLockSustain:          s.sustainLevel = g.sustain;             break;
        case kLockRelease:          s.releaseSec = g.releaseSec;            break;
        case kLockMuteGroup:        s.muteGroup = g.muteGroup;              break;
        case kLockLoop:             s.loopMode = g.loopMode;                break;
        case kLockStretch:          s.stretchEnabled = g.stretchEnabled;    break;
        case kLockReleaseTail:      s.releaseTail = g.releaseTail;          break;
        case kLockReverse:          s.reverse = g.reverse;                  break;
        case kLockOneShot:          s.oneShot = g.oneShot;                  break;
        case kLockCentsDetune:      s.centsDetune = g.centsDetune;          break;
        case kLockTonality:         s.tonalityHz = g.tonalityHz;            break;
        case kLockFormant:          s.formantSemitones = g.formantSemitones; break;
        case kLockFormantComp:      s.formantComp = g.formantComp;          break;
        case kLockGrainMode:        s.grainMode = g.grainMode;              break;
        case kLockVolume:           s.volume = g.volumeDb;                  break;
        case kLockFilterEnabled:    s.filterEnabled = g.filterEnabled;      break;
        case kLockFilterType:       s.filterType = g.filterType;            break;
        case kLockFilterSlope:      s.filterSlope = g.filterSlope;          break;
        case kLockFilterCutoff:     s.filterCutoff = g.filterCutoffHz;      break;
        case kLockFilterReso:       s.filterReso = g.filterReso;            break;
        case kLockFilterDrive:      s.filterDrive = g.filterDrive;          break;
        case kLockFilterKeyTrack:   s.filterKeyTrack = g.filterKeyTrack;    break;
        case kLockFilterEnvAttack:  s.filterEnvAttackSec = g.filterEnvAttackSec;    break;
        case kLockFilterEnvDecay:   s.filterEnvDecaySec = g.filterEnvDecaySec;      break;
        case kLockFilterEnvSustain: s.filterEnvSustain = g.filterEnvSustain;        break;
        case kLockFilterEnvRelease: s.filterEnvReleaseSec = g.filterEnvReleaseSec;  break;
        case kLockFilterEnvAmount:  s.filterEnvAmount = g.filterEnvAmount;          break;
        case kLockFilterAsym:       s.filterAsym = g.filterAsym;                  break;
        // kLockOutputBus: no sample-level default — slice default (0) is correct.
        // kLockLoopStart / kLockLoopLength: no sample-level default — slice defaults (0) are correct.
        default: break;
    }
}
