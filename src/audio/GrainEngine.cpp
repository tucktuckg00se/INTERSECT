#include "GrainEngine.h"
#include <cmath>

static float hannWindow (double pos)
{
    double t = pos / GrainEngine::kGrainSize;
    return 0.5f * (1.0f - std::cos (2.0 * juce::MathConstants<double>::pi * t));
}

void GrainEngine::processVoice (Voice& v, const SampleData& sample, double sampleRate,
                                float& outL, float& outR)
{
    outL = 0.0f;
    outR = 0.0f;

    if (! v.active)
        return;

    float env = v.envelope.processSample (sampleRate);

    if (v.envelope.isDone())
    {
        v.active = false;
        return;
    }

    double srcPos    = v.wsolaSrcPos;
    float  pitchR    = v.pitchRatio;
    float  timeR     = v.timeRatio;
    int    sliceStart = v.startSample;
    int    sliceEnd   = v.endSample;
    double phase     = v.wsolaPhase;

    // Grain position within current cycle
    int grainPos = (int) std::fmod (phase, (double) kGrainSize);
    if (grainPos < 0) grainPos += kGrainSize;

    // Two overlapping read heads
    double readPosA = srcPos;
    double readPosB = srcPos + kHalfGrain * timeR;

    // Wrap readPosB within slice bounds
    if (readPosB >= sliceEnd)
    {
        if (v.pingPong)
            readPosB = sliceEnd - (readPosB - sliceEnd);
        else
        {
            readPosB = sliceStart + std::fmod (readPosB - sliceEnd, (double) (sliceEnd - sliceStart));
            if (readPosB >= sliceEnd) readPosB = sliceStart;
        }
    }

    // Window values for crossfade
    float winA = hannWindow (grainPos);
    float winB = hannWindow ((grainPos + kHalfGrain) % kGrainSize);

    // Read and mix two grains
    float la = sample.getInterpolatedSample (readPosA, 0);
    float ra = sample.getInterpolatedSample (readPosA, 1);
    float lb = sample.getInterpolatedSample (readPosB, 0);
    float rb = sample.getInterpolatedSample (readPosB, 1);

    outL = (la * winA + lb * winB) * env * v.velocity;
    outR = (ra * winA + rb * winB) * env * v.velocity;

    // Advance source position by time-stretch rate
    srcPos += timeR;

    // Advance phase by pitch ratio
    phase += pitchR;

    // Handle source position wrapping
    if (srcPos >= sliceEnd)
    {
        if (v.pingPong)
        {
            v.direction = -1;
            srcPos = sliceEnd - (srcPos - sliceEnd);
        }
        else
        {
            if (v.envelope.getState() != AdsrEnvelope::Release)
                v.envelope.noteOff();
            srcPos = sliceEnd - 1;
        }
    }
    else if (srcPos < sliceStart)
    {
        if (v.pingPong)
        {
            v.direction = 1;
            srcPos = sliceStart + (sliceStart - srcPos);
        }
        else
        {
            srcPos = sliceStart;
        }
    }

    v.wsolaSrcPos = srcPos;
    v.wsolaPhase  = phase;
    v.age++;
}

float GrainEngine::calcStretchBpm (int startSample, int endSample, float timeUnitBars, double sampleRate)
{
    double durationSec = (endSample - startSample) / sampleRate;
    float beats = timeUnitBars * 4.0f;  // 4 beats per bar
    return durationSec > 0.0 ? (beats / (float) durationSec) * 60.0f : 120.0f;
}
