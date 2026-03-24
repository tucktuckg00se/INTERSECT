#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Constants.h"
#include "VoicePool.h"
#include "SliceManager.h"


class LazyChopEngine
{
public:
    bool isActive() const { return active; }
    bool isPlaying() const { return playing; }
    int  getChopPos() const { return chopPos; }

    void start (int sampleLen, SliceManager& sliceMgr, const PreviewStretchParams& params,
                bool snap = false, const juce::AudioBuffer<float>* buf = nullptr);
    void stop (VoicePool& voicePool, SliceManager& sliceMgr);
    int  onNote (int note, VoicePool& voicePool, SliceManager& sliceMgr);

    static int getPreviewVoiceIndex() { return VoicePool::kPreviewVoiceIndex; }

private:
    void startPreview (VoicePool& voicePool, int fromPos);

    bool active       = false;
    bool playing      = false;
    int  chopPos      = 0;
    int  nextMidiNote = kDefaultRootNote;
    int  sampleLength = 0;
    int  lastNote     = -1;

    PreviewStretchParams cachedParams;

    bool snapEnabled = false;
    const juce::AudioBuffer<float>* sampleBuffer = nullptr;
};
