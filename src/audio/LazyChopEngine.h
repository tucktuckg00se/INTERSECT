#pragma once
#include "GroupMix.h"
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

    /** Plays [rangeStart, rangeEnd) of the kit buffer from its start; a group's mix when `mix` is active. */
    void start (int rangeStart, int rangeEnd, SliceManager& sliceMgr, const PreviewStretchParams& params,
                bool snap = false, const juce::AudioBuffer<float>* buf = nullptr, const GroupMix& mix = {});
    void stop (VoicePool& voicePool, SliceManager& sliceMgr);
    int  onNote (int note, VoicePool& voicePool, SliceManager& sliceMgr);

    static int getPreviewVoiceIndex() { return VoicePool::kPreviewVoiceIndex; }

private:
    void startPreview (VoicePool& voicePool, int fromPos);

    bool active       = false;
    bool playing      = false;
    int  chopPos      = 0;
    int  nextMidiNote = kDefaultRootNote;
    int  sampleLength = 0;      // the end of the range played
    int  rangeStart   = 0;
    GroupMix groupMix;
    int  lastNote     = -1;

    PreviewStretchParams cachedParams;

    bool snapEnabled = false;
    const juce::AudioBuffer<float>* sampleBuffer = nullptr;
};
