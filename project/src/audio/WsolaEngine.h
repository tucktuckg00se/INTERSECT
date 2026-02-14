#pragma once
#include "Voice.h"
#include "SampleData.h"

class WsolaEngine
{
public:
    static constexpr int kGrainSize = 1024;
    static constexpr int kFadeLen   = 512;
    static constexpr int kHalfGrain = 512;

    static void processVoice (Voice& v, const SampleData& sample, double sampleRate,
                              float& outL, float& outR);

    static float calcStretchBpm (int startSample, int endSample, float timeUnitBars, double sampleRate);
};
