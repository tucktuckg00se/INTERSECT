#include "LazyChopEngine.h"
#include <cmath>

void LazyChopEngine::start (int sampleLen, SliceManager& sliceMgr)
{
    active = true;
    playing = false;
    chopPos = 0;
    sampleLength = sampleLen;
    lastNote = -1;

    nextMidiNote = sliceMgr.rootNote.load();
    int num = sliceMgr.getNumSlices();
    for (int i = 0; i < num; ++i)
    {
        const auto& s = sliceMgr.getSlice (i);
        if (s.active && s.midiNote >= nextMidiNote)
            nextMidiNote = s.midiNote + 1;
    }
    nextMidiNote = std::min (nextMidiNote, 127);
}

void LazyChopEngine::startPreview (VoicePool& voicePool, int fromPos)
{
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    v.active      = true;
    v.sliceIdx    = -1;
    v.position    = (double) fromPos;
    v.speed       = 1.0;
    v.direction   = 1;
    v.velocity    = 1.0f;
    v.midiNote    = -1;
    v.startSample = 0;
    v.endSample   = sampleLength;
    v.pingPong    = false;
    v.muteGroup   = 0;
    v.wsolaActive = false;
    v.stretchActive = false;
    v.pitchRatio  = 1.0f;
    v.age         = 0;
    v.looping     = true;

    // Sustain at half volume
    v.envelope.noteOn (0.0f, 0.0f, 0.5f, 0.02f);
}

void LazyChopEngine::stop (VoicePool& voicePool, SliceManager& /*sliceMgr*/)
{
    // Stop preview voice
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    v.active = false;

    active = false;
    playing = false;
}

void LazyChopEngine::onNote (int note, VoicePool& voicePool, SliceManager& sliceMgr)
{
    // If this MIDI note is already assigned to an existing slice, audition it
    int existingSlice = sliceMgr.midiNoteToSlice (note);
    if (existingSlice >= 0)
    {
        const auto& s = sliceMgr.getSlice (existingSlice);
        startPreview (voicePool, s.startSample);
        playing = true;
        chopPos = -1;  // reset so next unassigned note only sets a new start
        return;
    }

    // First unassigned note — start playback from beginning
    if (! playing)
    {
        startPreview (voicePool, 0);
        chopPos = 0;
        lastNote = note;
        playing = true;
        return;
    }

    // Re-press same note: re-audition from current start point
    if (note == lastNote && chopPos >= 0)
    {
        startPreview (voicePool, chopPos);
        return;
    }

    // Subsequent unassigned note — place slice boundary at playhead
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    int playhead = (int) std::floor (v.position);

    // After audition, first unassigned note just sets a new start point
    if (chopPos < 0)
    {
        chopPos = playhead;
        lastNote = note;
        return;
    }

    // Handle wrap-around: if playhead wrapped past chopPos, close slice to end of sample
    if (playhead < chopPos)
    {
        if (sampleLength - chopPos >= 64)
        {
            int idx = sliceMgr.createSlice (chopPos, sampleLength);
            if (idx >= 0)
            {
                auto& s = sliceMgr.getSlice (idx);
                s.midiNote = nextMidiNote;
                nextMidiNote = std::min (nextMidiNote + 1, 127);
                sliceMgr.rebuildMidiMap();
            }
        }
        chopPos = 0;
    }

    // Create slice from chopPos to playhead (min 64 samples)
    if (playhead - chopPos >= 64)
    {
        int newIdx = sliceMgr.createSlice (chopPos, playhead);
        if (newIdx >= 0)
        {
            auto& s = sliceMgr.getSlice (newIdx);
            s.midiNote = nextMidiNote;
            nextMidiNote = std::min (nextMidiNote + 1, 127);
            sliceMgr.rebuildMidiMap();
        }
    }

    chopPos = playhead;
    lastNote = note;
}
