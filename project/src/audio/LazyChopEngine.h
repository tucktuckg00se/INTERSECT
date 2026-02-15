#pragma once
#include "VoicePool.h"
#include "SliceManager.h"

class LazyChopEngine
{
public:
    bool isActive() const { return active; }

    void start (int sampleLen, SliceManager& sliceMgr);
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
};
