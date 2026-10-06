#pragma once
#include "Voice.h"
#include "SliceManager.h"
#include "SampleData.h"
#include "../Constants.h"
#include <array>
#include <atomic>
#include <juce_core/juce_core.h>

// The sample-level parameter values (of the slice's own session sample) needed to start a voice;
// slices fall back to these unless they lock their own value.
// Units match slice storage: seconds for ADSR, 0-1 for sustain, dB for volume.
struct VoiceStartParams
{
    int   sliceIdx         = 0;
    float velocity         = 0.0f;   // raw MIDI 0-127
    int   note             = 0;
    float sampleBpm        = 120.0f;
    float samplePitch      = 0.0f;
    int   sampleAlgorithm  = 0;
    int   sampleRepitchMode = (int) RepitchMode::Linear;
    float sampleAttackSec  = 0.005f;
    float sampleDecaySec   = 0.1f;
    float sampleSustain    = 1.0f;   // 0-1
    float sampleReleaseSec = 0.02f;
    int   sampleMuteGroup  = 1;
    bool  sampleStretch    = false;
    float dawBpm           = 120.0f;
    float sampleTonality   = 0.0f;
    float sampleFormant    = 0.0f;
    bool  sampleFormantComp = false;
    int   sampleGrainMode  = 0;
    float sampleVolume     = 0.0f;   // dB
    bool  sampleReleaseTail = false;
    bool  sampleReverse    = false;
    int   sampleLoopMode   = 0;
    bool  sampleOneShot    = false;
    float sampleCentsDetune = 0.0f;
    bool  sampleFilterEnabled = false;
    int   sampleFilterType    = 0;
    int   sampleFilterSlope   = 0;
    float sampleFilterCutoff  = 8200.0f;
    float sampleFilterReso    = 0.0f;
    float sampleFilterDrive   = 0.0f;
    float sampleFilterAsym    = 0.0f;
    float sampleFilterKeyTrack = 0.0f;
    float sampleFilterEnvAttackSec  = 0.0f;
    float sampleFilterEnvDecaySec   = 0.0f;
    float sampleFilterEnvSustain    = 1.0f;
    float sampleFilterEnvReleaseSec = 0.0f;
    float sampleFilterEnvAmount     = 0.0f;
    float sampleCrossfadePct        = 0.0f;
    int   rootNote = kDefaultRootNote;
    int   sliceRootNote = kDefaultRootNote;  // per-slice root for range transpose
};

struct PreviewStretchParams
{
    bool   stretchEnabled = false;
    int    algorithm      = 0;
    int    repitchMode    = (int) RepitchMode::Linear;
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

    // Range-based render APIs — preferred entry points from processBlock().
    // Accumulate into dest[startSample + s]; the destination is never cleared here,
    // so a block can be rendered as several consecutive ranges split at MIDI events.
    void renderMainBusRange (const SampleData& sample,
                             float* destL, float* destR,
                             int startSample, int numSamples);
    void renderRoutedRange (const SampleData& sample,
                            float* busL[], float* busR[], int numBuses,
                            int startSample, int numSamples);

    void prepareToPlay (double sampleRate, int maxBlockSize);
    void setSampleRate (double sr);
    double getSampleRate() const { return sampleRate; }

    void setMaxActiveVoices (int n);
    int  getMaxActiveVoices() const { return maxActive; }

    Voice& getVoice (int idx)
    {
        jassert (juce::isPositiveAndBelow (idx, kMaxVoices));
        return voices[(size_t) idx];
    }

    const Voice& getVoice (int idx) const
    {
        jassert (juce::isPositiveAndBelow (idx, kMaxVoices));
        return voices[(size_t) idx];
    }

    void startShiftPreview (int startSample, int bufferSize, const PreviewStretchParams& p);
    void stopShiftPreview();

    // Public helpers so LazyChopEngine can initialise stretch on preview voice
    static void initPreviewVoiceCommon (Voice& v,
                                        int playheadSample,
                                        int startSample,
                                        int endSample,
                                        bool looping,
                                        float velocity);
    static void initPreviewVoiceStretch (Voice& v, int sourceStartSample, const PreviewStretchParams& params);
    static void initStretcher (Voice& v, float pitchSemis, double sr,
                               float tonalityHz, float formantSemis, bool formantComp,
                               const SampleData& sample);
    static void initBungee (Voice& v, float pitchSemis, double sr, int grainMode);

    // Atomic voice positions for UI cursor display
    std::array<std::atomic<float>, kMaxVoices> voicePositions;
    std::array<std::atomic<float>, kMaxVoices> xfadeSourcePositions;

    void processVoiceSample (int i, const SampleData& sample, double sampleRate,
                             float& outL, float& outR);

private:
    std::array<Voice, kMaxVoices> voices;
    int maxActive = 16; // playable voices, excluding preview voice
    double sampleRate = 44100.0;

    // Preallocated scratch buffers for block rendering (sized to maxBlockSize)
    std::vector<float> scratchL;
    std::vector<float> scratchR;
};
