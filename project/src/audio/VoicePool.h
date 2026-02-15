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
                     int globalGrainMode, float globalVolume,
                     const SampleData& sample);

    void releaseNote (int note);
    void muteGroup (int group, int exceptVoice);

    void processSample (const SampleData& sample, double sampleRate,
                        float& outL, float& outR);

    void setSampleRate (double sr) { sampleRate = sr; }
    double getSampleRate() const { return sampleRate; }

    Voice& getVoice (int idx) { return voices[idx]; }

    // Public helpers so LazyChopEngine can initialise stretch on preview voice
    static void initStretcher (Voice& v, float pitchSemis, double sr,
                               float tonalityHz, float formantSemis, bool formantComp,
                               const SampleData& sample);
    static void initBungee (Voice& v, float pitchSemis, double sr, int grainMode);

    // Atomic voice positions for UI cursor display
    std::array<std::atomic<float>, kMaxVoices> voicePositions;

private:
    std::array<Voice, kMaxVoices> voices;
    double sampleRate = 44100.0;
};
