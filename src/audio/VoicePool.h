#pragma once
#include "Voice.h"
#include "SliceManager.h"
#include "SampleData.h"
#include <array>
#include <atomic>

// All global parameter values needed to start a voice, pre-loaded from APVTS on the UI thread.
// Units match slice storage: seconds for ADSR, 0-1 for sustain, dB for volume.
struct VoiceStartParams
{
    int   sliceIdx         = 0;
    float velocity         = 0.0f;   // raw MIDI 0-127
    int   note             = 0;
    float globalBpm        = 120.0f;
    float globalPitch      = 0.0f;
    int   globalAlgorithm  = 0;
    float globalAttackSec  = 0.005f;
    float globalDecaySec   = 0.1f;
    float globalSustain    = 1.0f;   // 0-1
    float globalReleaseSec = 0.02f;
    int   globalMuteGroup  = 1;
    bool  globalStretch    = false;
    float dawBpm           = 120.0f;
    float globalTonality   = 0.0f;
    float globalFormant    = 0.0f;
    bool  globalFormantComp = false;
    int   globalGrainMode  = 0;
    float globalVolume     = 0.0f;   // dB
    bool  globalReleaseTail = false;
    bool  globalReverse    = false;
    int   globalLoopMode   = 0;
    bool  globalOneShot    = false;
    float globalCentsDetune = 0.0f;
    bool  globalFilterEnabled = false;
    int   globalFilterType    = 0;
    int   globalFilterSlope   = 0;
    float globalFilterCutoff  = 8200.0f;
    float globalFilterReso    = 0.0f;
    float globalFilterDrive   = 0.0f;
    float globalFilterKeyTrack = 0.0f;
    float globalFilterEnvAttackSec  = 0.0f;
    float globalFilterEnvDecaySec   = 0.0f;
    float globalFilterEnvSustain    = 1.0f;
    float globalFilterEnvReleaseSec = 0.0f;
    float globalFilterEnvAmount     = 0.0f;
    int   rootNote = 36;
};

struct PreviewStretchParams
{
    bool   stretchEnabled = false;
    int    algorithm      = 0;
    float  bpm            = 120.0f;
    float  pitch          = 0.0f;
    float  dawBpm         = 120.0f;
    float  tonality       = 0.0f;
    float  formant        = 0.0f;
    bool   formantComp    = false;
    int    grainMode      = 0;
    double sampleRate     = 44100.0;
    const SampleData* sample = nullptr;
};

class VoicePool
{
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kPreviewVoiceIndex = kMaxVoices - 1;

    VoicePool();

    int  allocate();
    void startVoice (int voiceIdx, const VoiceStartParams& params,
                     SliceManager& sliceMgr, const SampleData& sample);

    static constexpr float kShortReleaseSec = 0.05f;  // All Notes Off (CC 123): 50ms fade
    static constexpr float kKillReleaseSec  = 0.005f; // All Sound Off (CC 120): 5ms hard kill

    void releaseNote (int note);
    void releaseNoteForced (int note);  // host-sweep note-off: forceRelease even on oneShot voices
    void releaseAll();                  // CC 123 — 50ms fade on all active voices
    void killAll();                     // CC 120 — 5ms hard kill on all active voices
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
    const Voice& getVoice (int idx) const { return voices[idx]; }

    void startShiftPreview (int startSample, int bufferSize, const PreviewStretchParams& p);
    void stopShiftPreview();

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
    int maxActive = 16; // playable voices, excluding preview voice
    double sampleRate = 44100.0;
};
