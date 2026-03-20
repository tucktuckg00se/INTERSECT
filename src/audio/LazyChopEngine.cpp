#include "LazyChopEngine.h"
#include "../Constants.h"
#include "AudioAnalysis.h"
#include <cmath>

void LazyChopEngine::start (int sampleLen, SliceManager& sliceMgr,
                            const PreviewStretchParams& params,
                            bool snap, const juce::AudioBuffer<float>* buf)
{
    active = true;
    playing = false;
    chopPos = 0;
    sampleLength = sampleLen;
    lastNote = -1;
    cachedParams = params;
    snapEnabled = snap;
    sampleBuffer = buf;

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
    VoicePool::initPreviewVoiceCommon (v, fromPos, 0, sampleLength, true, 1.0f);

    // Apply stretch from cached sample-level params
    const auto& p = cachedParams;
    VoicePool::initPreviewVoiceStretch (v, fromPos, p);

    // Sustain at half volume
    v.envelope.noteOn (0.0f, 0.0f, 0.5f, 0.02f, cachedParams.sampleRate);
}

void LazyChopEngine::stop (VoicePool& voicePool, SliceManager& /*sliceMgr*/)
{
    // Stop preview voice
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    v.active = false;
    v.stretchActive = false;
    v.bungeeActive = false;

    active = false;
    playing = false;
}

int LazyChopEngine::onNote (int note, VoicePool& voicePool, SliceManager& sliceMgr)
{
    // If this MIDI note is already assigned to an existing slice, audition it
    int existingSlice = sliceMgr.midiNoteToSlice (note);
    if (existingSlice >= 0)
    {
        const auto& s = sliceMgr.getSlice (existingSlice);
        startPreview (voicePool, s.startSample);
        playing = true;
        chopPos = -1;  // reset so next unassigned note only sets a new start
        return -1;
    }

    // First unassigned note — start playback from beginning
    if (! playing)
    {
        startPreview (voicePool, 0);
        chopPos = 0;
        lastNote = note;
        playing = true;
        return -1;
    }

    // Re-press same note: re-audition from current start point
    if (note == lastNote && chopPos >= 0)
    {
        startPreview (voicePool, chopPos);
        return -1;
    }

    // Subsequent unassigned note — place slice boundary at playhead
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    int playhead = (int) std::floor (v.position);

    if (snapEnabled && sampleBuffer != nullptr)
        playhead = AudioAnalysis::findNearestZeroCrossing (*sampleBuffer, playhead);

    // After audition, first unassigned note just sets a new start point
    if (chopPos < 0)
    {
        chopPos = playhead;
        lastNote = note;
        return -1;
    }

    int resultIdx = -1;

    // Handle wrap-around: if playhead wrapped past chopPos, close slice to end of sample
    if (playhead < chopPos)
    {
        if (sampleLength - chopPos >= kMinSliceLengthSamples)
        {
            int idx = sliceMgr.createSlice (chopPos, sampleLength);
            if (idx >= 0)
            {
                auto& s = sliceMgr.getSlice (idx);
                s.midiNote = nextMidiNote;
                nextMidiNote = std::min (nextMidiNote + 1, 127);
                sliceMgr.rebuildMidiMap();
                resultIdx = idx;
            }
        }
        chopPos = 0;
    }

    // Create slice from chopPos to playhead (min slice length)
    if (playhead - chopPos >= kMinSliceLengthSamples)
    {
        int newIdx = sliceMgr.createSlice (chopPos, playhead);
        if (newIdx >= 0)
        {
            auto& s = sliceMgr.getSlice (newIdx);
            s.midiNote = nextMidiNote;
            nextMidiNote = std::min (nextMidiNote + 1, 127);
            sliceMgr.rebuildMidiMap();
            resultIdx = newIdx;
        }
    }

    chopPos = playhead;
    lastNote = note;
    return resultIdx;
}
