#pragma once
#include "Voice.h"
#include "SliceManager.h"
#include "SampleData.h"
#include <array>
#include <atomic>

class VoicePool
{
public:
    static constexpr int kMaxVoices = 32;

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
                     bool globalReleaseTail,
                     bool globalReverse,
                     const SampleData& sample);

    void releaseNote (int note);
    void muteGroup (int group, int exceptVoice);

    void processSample (const SampleData& sample, double sampleRate,
                        float& outL, float& outR);

    static void processSampleMultiOut (const SampleData& sample, double sampleRate,
                                      float* outPtrs[], int numOuts);

    void setSampleRate (double sr) { sampleRate = sr; }
    double getSampleRate() const { return sampleRate; }

    void setMaxActiveVoices (int n);
    int  getMaxActiveVoices() const { return maxActive; }

    Voice& getVoice (int idx) { return voices[idx]; }

    // Public helpers so LazyChopEngine can initialise stretch on preview voice
    static void initStretcher (Voice& v, float pitchSemis, double sr,
                               float tonalityHz, float formantSemis, bool formantComp,
                               const SampleData& sample);
    static void initBungee (Voice& v, float pitchSemis, double sr, int grainMode);

    // Atomic voice positions for UI cursor display
    std::array<std::atomic<float>, kMaxVoices> voicePositions;

    void processVoiceSample (int i, const SampleData& sample, double sampleRate,
                             float& outL, float& outR);

private:

    std::array<Voice, kMaxVoices> voices;
    int maxActive = 16;
    double sampleRate = 44100.0;
};
