#pragma once
#include "Voice.h"
#include "SliceManager.h"
#include "SampleData.h"
#include <array>
#include <atomic>

class VoicePool
{
public:
    static constexpr int kMaxVoices = 16;

    VoicePool();

    int  allocate();
    void startVoice (int voiceIdx, int sliceIdx, float velocity, int note,
                     SliceManager& sliceMgr,
                     float globalBpm, float globalPitch, int globalAlgorithm,
                     float globalAttack, float globalDecay, float globalSustain, float globalRelease,
                     int globalMuteGroup, bool globalPingPong,
                     bool globalStretchEnabled, float dawBpm,
                     float globalTonality, float globalFormant, bool globalFormantComp,
                     const SampleData& sample);

    void releaseNote (int note);
    void muteGroup (int group, int exceptVoice);

    void processSample (const SampleData& sample, double sampleRate,
                        float& outL, float& outR);

    void setSampleRate (double sr) { sampleRate = sr; }

    Voice& getVoice (int idx) { return voices[idx]; }

    // Atomic voice positions for UI cursor display
    std::array<std::atomic<float>, kMaxVoices> voicePositions;

private:
    std::array<Voice, kMaxVoices> voices;
    double sampleRate = 44100.0;
};
