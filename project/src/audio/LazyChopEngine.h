#pragma once
#include "VoicePool.h"
#include "SliceManager.h"

struct PreviewStretchParams
{
    bool  stretchEnabled = false;
    int   algorithm      = 0;
    float bpm            = 120.0f;
    float pitch          = 0.0f;
    float dawBpm         = 120.0f;
    float tonality       = 0.0f;
    float formant        = 0.0f;
    bool  formantComp    = false;
    int   grainMode      = 0;
    double sampleRate    = 44100.0;
    const SampleData* sample = nullptr;
};

class LazyChopEngine
{
public:
    bool isActive() const { return active; }
    bool isPlaying() const { return playing; }
    int  getChopPos() const { return chopPos; }

    void start (int sampleLen, SliceManager& sliceMgr, const PreviewStretchParams& params);
    void stop (VoicePool& voicePool, SliceManager& sliceMgr);
    void onNote (int note, VoicePool& voicePool, SliceManager& sliceMgr);

    static int getPreviewVoiceIndex() { return VoicePool::kMaxVoices - 1; }

private:
    void startPreview (VoicePool& voicePool, int fromPos);

    bool active       = false;
    bool playing      = false;
    int  chopPos      = 0;
    int  nextMidiNote = 36;
    int  sampleLength = 0;
    int  lastNote     = -1;

    PreviewStretchParams cachedParams;
};
