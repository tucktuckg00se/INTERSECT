#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Constants.h"
#include "ParamIds.h"

struct GlobalParamSnapshot
{
    float bpm = 120.0f;
    float pitchSemitones = 0.0f;
    float centsDetune = 0.0f;
    int algorithm = 0;

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
    int maxVoices = 16;

    bool filterEnabled = false;
    int filterType = 0;
    int filterSlope = 0;
    float filterCutoffHz = 8200.0f;
    float filterReso = 0.0f;
    float filterDrive = 0.0f;
    float filterKeyTrack = 0.0f;
    float filterEnvAttackSec = 0.0f;
    float filterEnvDecaySec = 0.0f;
    float filterEnvSustain = 1.0f;
    float filterEnvReleaseSec = 0.0f;
    float filterEnvAmount = 0.0f;

    int rootNote = kDefaultRootNote;

    static GlobalParamSnapshot loadFrom (const juce::AudioProcessorValueTreeState& apvts,
                                         int rootNoteValue = kDefaultRootNote)
    {
        auto loadFloat = [&apvts] (const juce::String& paramId, float fallback) -> float
        {
            if (auto* param = apvts.getRawParameterValue (paramId))
                return param->load();
            return fallback;
        };

        auto loadBool = [&loadFloat] (const juce::String& paramId, bool fallback) -> bool
        {
            return loadFloat (paramId, fallback ? 1.0f : 0.0f) > 0.5f;
        };

        auto loadInt = [&loadFloat] (const juce::String& paramId, int fallback) -> int
        {
            return juce::roundToInt (loadFloat (paramId, (float) fallback));
        };

        GlobalParamSnapshot snapshot;
        snapshot.bpm = loadFloat (ParamIds::defaultBpm, snapshot.bpm);
        snapshot.pitchSemitones = loadFloat (ParamIds::defaultPitch, snapshot.pitchSemitones);
        snapshot.centsDetune = loadFloat (ParamIds::defaultCentsDetune, snapshot.centsDetune);
        snapshot.algorithm = loadInt (ParamIds::defaultAlgorithm, snapshot.algorithm);

        snapshot.attackSec = loadFloat (ParamIds::defaultAttack, snapshot.attackSec * 1000.0f) / 1000.0f;
        snapshot.decaySec = loadFloat (ParamIds::defaultDecay, snapshot.decaySec * 1000.0f) / 1000.0f;
        snapshot.sustain = loadFloat (ParamIds::defaultSustain, snapshot.sustain * 100.0f) / 100.0f;
        snapshot.releaseSec = loadFloat (ParamIds::defaultRelease, snapshot.releaseSec * 1000.0f) / 1000.0f;

        snapshot.muteGroup = loadInt (ParamIds::defaultMuteGroup, snapshot.muteGroup);
        snapshot.stretchEnabled = loadBool (ParamIds::defaultStretchEnabled, snapshot.stretchEnabled);
        snapshot.reverse = loadBool (ParamIds::defaultReverse, snapshot.reverse);
        snapshot.loopMode = loadInt (ParamIds::defaultLoop, snapshot.loopMode);
        snapshot.oneShot = loadBool (ParamIds::defaultOneShot, snapshot.oneShot);
        snapshot.releaseTail = loadBool (ParamIds::defaultReleaseTail, snapshot.releaseTail);

        snapshot.tonalityHz = loadFloat (ParamIds::defaultTonality, snapshot.tonalityHz);
        snapshot.formantSemitones = loadFloat (ParamIds::defaultFormant, snapshot.formantSemitones);
        snapshot.formantComp = loadBool (ParamIds::defaultFormantComp, snapshot.formantComp);
        snapshot.grainMode = loadInt (ParamIds::defaultGrainMode, snapshot.grainMode);

        snapshot.volumeDb = loadFloat (ParamIds::masterVolume, snapshot.volumeDb);
        snapshot.maxVoices = loadInt (ParamIds::maxVoices, snapshot.maxVoices);

        snapshot.filterEnabled = loadBool (ParamIds::defaultFilterEnabled, snapshot.filterEnabled);
        snapshot.filterType = loadInt (ParamIds::defaultFilterType, snapshot.filterType);
        snapshot.filterSlope = loadInt (ParamIds::defaultFilterSlope, snapshot.filterSlope);
        snapshot.filterCutoffHz = loadFloat (ParamIds::defaultFilterCutoff, snapshot.filterCutoffHz);
        snapshot.filterReso = loadFloat (ParamIds::defaultFilterReso, snapshot.filterReso);
        snapshot.filterDrive = loadFloat (ParamIds::defaultFilterDrive, snapshot.filterDrive);
        snapshot.filterKeyTrack = loadFloat (ParamIds::defaultFilterKeyTrack, snapshot.filterKeyTrack);
        snapshot.filterEnvAttackSec = loadFloat (ParamIds::defaultFilterEnvAttack, snapshot.filterEnvAttackSec * 1000.0f) / 1000.0f;
        snapshot.filterEnvDecaySec = loadFloat (ParamIds::defaultFilterEnvDecay, snapshot.filterEnvDecaySec * 1000.0f) / 1000.0f;
        snapshot.filterEnvSustain = loadFloat (ParamIds::defaultFilterEnvSustain, snapshot.filterEnvSustain * 100.0f) / 100.0f;
        snapshot.filterEnvReleaseSec = loadFloat (ParamIds::defaultFilterEnvRelease, snapshot.filterEnvReleaseSec * 1000.0f) / 1000.0f;
        snapshot.filterEnvAmount = loadFloat (ParamIds::defaultFilterEnvAmount, snapshot.filterEnvAmount);

        snapshot.rootNote = rootNoteValue;
        return snapshot;
    }
};
