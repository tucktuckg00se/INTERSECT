#include "GrainEngine.h"

float GrainEngine::calcStretchBpm (int startSample, int endSample, float timeUnitBars, double sampleRate)
{
    double durationSec = (endSample - startSample) / sampleRate;
    float beats = timeUnitBars * 4.0f;  // 4 beats per bar
    return durationSec > 0.0 ? (beats / (float) durationSec) * 60.0f : 120.0f;
}
