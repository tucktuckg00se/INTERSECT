#pragma once

struct ParamUndoState
{
    float masterVolume = 0.0f;
    float defaultBpm = 120.0f;
    float defaultPitch = 0.0f;
    float defaultAlgorithm = 0.0f;
    float defaultAttack = 5.0f;
    float defaultDecay = 100.0f;
    float defaultSustain = 100.0f;
    float defaultRelease = 20.0f;
    float defaultMuteGroup = 1.0f;
    float defaultLoop = 0.0f;
    float defaultStretchEnabled = 0.0f;
    float defaultTonality = 0.0f;
    float defaultFormant = 0.0f;
    float defaultFormantComp = 0.0f;
    float defaultGrainMode = 0.0f;
    float defaultReleaseTail = 0.0f;
    float defaultReverse = 0.0f;
    float defaultOneShot = 0.0f;
    float defaultCentsDetune = 0.0f;
    float defaultFilterEnabled = 0.0f;
    float defaultFilterType = 0.0f;
    float defaultFilterSlope = 0.0f;
    float defaultFilterCutoff = 8200.0f;
    float defaultFilterReso = 0.0f;
    float defaultFilterDrive = 0.0f;
    float defaultFilterKeyTrack = 0.0f;
    float defaultFilterEnvAttack = 0.0f;
    float defaultFilterEnvDecay = 0.0f;
    float defaultFilterEnvSustain = 100.0f;
    float defaultFilterEnvRelease = 0.0f;
    float defaultFilterEnvAmount = 0.0f;
    float maxVoices = 16.0f;
};
