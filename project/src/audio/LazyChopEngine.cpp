#include "LazyChopEngine.h"
#include <cmath>

void LazyChopEngine::start (VoicePool& voicePool, int sampleLen)
{
    active = true;
    waitingForFirstNote = true;
    chopPos = 0;
    nextMidiNote = 36;
    sampleLength = sampleLen;

    // Start preview voice immediately — sample plays from the beginning
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    v.active      = true;
    v.sliceIdx    = -1;
    v.position    = 0.0;
    v.speed       = 1.0;
    v.direction   = 1;
    v.velocity    = 1.0f;
    v.midiNote    = -1;
    v.startSample = 0;
    v.endSample   = sampleLen;
    v.pingPong    = false;
    v.muteGroup   = 0;
    v.wsolaActive = false;
    v.stretchActive = false;
    v.pitchRatio  = 1.0f;
    v.age         = 0;
    v.looping     = false;

    // Sustain at half volume
    v.envelope.noteOn (0.0f, 0.0f, 0.5f, 0.02f);
}

void LazyChopEngine::stop (VoicePool& voicePool, SliceManager& sliceMgr)
{
    // Close pending slice — set end to full sample length
    if (! waitingForFirstNote && chopPos < sampleLength)
    {
        int newIdx = sliceMgr.createSlice (chopPos, sampleLength);
        if (newIdx >= 0)
        {
            auto& s = sliceMgr.getSlice (newIdx);
            s.midiNote = nextMidiNote;
            sliceMgr.rebuildMidiMap();
        }
    }

    // Stop preview voice
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    v.active = false;

    active = false;
    waitingForFirstNote = true;
}

void LazyChopEngine::onNote (int note, VoicePool& voicePool, SliceManager& sliceMgr)
{
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    int previewPos = (int) std::floor (v.position);

    if (waitingForFirstNote)
    {
        // First note: set chopPos to current playhead (start of first slice)
        chopPos = previewPos;
        nextMidiNote = note;
        waitingForFirstNote = false;
        return;
    }

    // Close current slice at playhead position (min 64 samples)
    if (previewPos > chopPos + 64)
    {
        int newIdx = sliceMgr.createSlice (chopPos, previewPos);
        if (newIdx >= 0)
        {
            auto& s = sliceMgr.getSlice (newIdx);
            s.midiNote = nextMidiNote;
            nextMidiNote = std::min (nextMidiNote + 1, 127);
            sliceMgr.rebuildMidiMap();
        }
    }

    // Start new slice at current playhead
    chopPos = previewPos;
}
