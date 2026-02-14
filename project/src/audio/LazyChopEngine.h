#pragma once
#include "VoicePool.h"
#include "SliceManager.h"

class LazyChopEngine
{
public:
    bool isActive() const { return active; }
    bool isWaitingForFirstNote() const { return waitingForFirstNote; }
    int  getNextMidiNote() const { return nextMidiNote; }

    void start (VoicePool& voicePool, int sampleLen);
    void stop (VoicePool& voicePool, SliceManager& sliceMgr);
    void onNote (int note, VoicePool& voicePool, SliceManager& sliceMgr);

    int getPreviewVoiceIndex() const { return VoicePool::kMaxVoices - 1; }

private:
    bool active              = false;
    bool waitingForFirstNote = true;
    int  chopPos             = 0;
    int  nextMidiNote        = 36;
    int  sampleLength        = 0;
};
