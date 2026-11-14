#pragma once

// Helpers shared by the files that implement IntersectProcessor (PluginProcessor.cpp,
// ProcessorCommands.cpp). Not part of the processor's interface.

#include "Constants.h"
#include "audio/SampleData.h"
#include "audio/VoicePool.h"
#include "params/SampleParams.h"

namespace ProcessorInternal
{
constexpr int kMidiEditMinSliceLength = kMinSliceLengthSamples;

inline PreviewStretchParams makePreviewStretchParams (const SampleParams& sampleLevel,
                                               float dawBpm,
                                               double sampleRate,
                                               const SampleData* sample)
{
    PreviewStretchParams params;
    params.stretchEnabled = sampleLevel.stretchEnabled;
    params.algorithm = sampleLevel.algorithm;
    params.repitchMode = sampleLevel.repitchMode;
    params.bpm = sampleLevel.bpm;
    params.pitch = sampleLevel.pitchSemitones;
    params.dawBpm = dawBpm;
    params.tonality = sampleLevel.tonalityHz;
    params.formant = sampleLevel.formantSemitones;
    params.formantComp = sampleLevel.formantComp;
    params.grainMode = sampleLevel.grainMode;
    params.sampleRate = sampleRate;
    params.sample = sample;
    return params;
}
}
