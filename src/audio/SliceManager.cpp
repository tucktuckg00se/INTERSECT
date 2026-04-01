#include "SliceManager.h"
#include "../Constants.h"
#include <algorithm>
#include <cmath>

SliceManager::SliceManager()
{
    midiMap.fill (-1);
    for (auto& v : midiMapMulti)
        v.reserve ((size_t) kMaxSlices);
}

int SliceManager::createSlice (int start, int end)
{
    if (numSlices >= kMaxSlices)
        return -1;

    // Enforce minimum slice length
    if (std::abs (end - start) < kMinSliceLengthSamples)
        end = start + kMinSliceLengthSamples;

    // Ensure start < end
    if (start > end)
        std::swap (start, end);

    int idx = numSlices;
    slices[(size_t) idx] = Slice {};
    auto& s = slices[(size_t) idx];

    s.active      = true;
    s.startSample = start;
    s.endSample   = end;
    s.midiNote    = nextMidiNote();
    s.lockMask    = 0;

    // Default override values
    s.bpm            = 120.0f;
    s.pitchSemitones = 0.0f;
    s.algorithm      = 0;
    s.repitchMode    = (int) RepitchMode::Linear;
    s.attackSec      = 0.005f;
    s.decaySec       = 0.1f;
    s.sustainLevel   = 1.0f;
    s.releaseSec     = 0.02f;
    s.muteGroup      = 1;
    s.loopMode       = 0;
    s.filterEnabled  = false;
    s.filterType     = 0;
    s.filterSlope    = 0;
    s.filterCutoff   = 8200.0f;
    s.filterReso     = 0.0f;
    s.filterDrive    = 0.0f;
    s.filterKeyTrack = 0.0f;
    s.filterEnvAttackSec  = 0.0f;
    s.filterEnvDecaySec   = 0.0f;
    s.filterEnvSustain    = 1.0f;
    s.filterEnvReleaseSec = 0.0f;
    s.filterEnvAmount     = 0.0f;

    // Assign colour from palette
    const auto* p = palette.load (std::memory_order_relaxed);
    s.colour = p ? p[idx % 16] : juce::Colour (0xFF4D8C99);

    numSlices++;
    rebuildMidiMap();
    return idx;
}

void SliceManager::deleteSlice (int idx)
{
    if (idx < 0 || idx >= numSlices)
        return;

    // Shift all slices after idx down by one, preserving each slice's MIDI note
    for (int i = idx; i < numSlices - 1; ++i)
        slices[i] = slices[i + 1];

    // Deactivate last
    slices[numSlices - 1].active = false;
    numSlices--;

    // Fix selected slice
    if (selectedSlice >= numSlices)
        selectedSlice = std::max (0, numSlices - 1);
    if (numSlices == 0)
        selectedSlice = -1;

    rebuildMidiMap();
}

void SliceManager::clearAll()
{
    numSlices = 0;
    for (auto& s : slices)
        s.active = false;
    selectedSlice = -1;
    rebuildMidiMap();
}

void SliceManager::rebuildMidiMap()
{
    midiMap.fill (-1);
    for (auto& v : midiMapMulti)
        v.clear();

    for (int i = 0; i < numSlices; ++i)
    {
        if (slices[i].active)
        {
            int note = slices[i].midiNote;
            if (note >= 0 && note < kMidiNoteCount)
            {
                if (midiMap[note] < 0)
                    midiMap[note] = i;
                midiMapMulti[note].push_back (i);
            }
        }
    }
}

int SliceManager::midiNoteToSlice (int note) const
{
    if (note < 0 || note >= kMidiNoteCount)
        return -1;
    return midiMap[note];
}

const std::vector<int>& SliceManager::midiNoteToSlices (int note) const
{
    static const std::vector<int> empty;
    if (note < 0 || note >= kMidiNoteCount)
        return empty;
    return midiMapMulti[note];
}

float SliceManager::resolveParam (int sliceIdx, LockBit lockBit, float sliceValue, float globalDefault) const
{
    if (sliceIdx < 0 || sliceIdx >= numSlices)
        return globalDefault;

    return (slices[sliceIdx].lockMask & lockBit) ? sliceValue : globalDefault;
}

int SliceManager::nextMidiNote() const
{
    int highest = rootNote.load() - 1;
    for (int i = 0; i < numSlices; ++i)
        if (slices[i].active && slices[i].midiNote > highest)
            highest = slices[i].midiNote;
    return std::min (highest + 1, kMaxMidiNote);
}

void SliceManager::repackMidiNotes (bool sortByPosition)
{
    if (sortByPosition && numSlices > 1)
    {
        // Track the selected slice across the sort
        int selStart = -1, selEnd = -1;
        int sel = selectedSlice.load();
        if (sel >= 0 && sel < numSlices)
        {
            selStart = slices[sel].startSample;
            selEnd   = slices[sel].endSample;
        }

        std::sort (slices.begin(), slices.begin() + numSlices,
                   [] (const Slice& a, const Slice& b) { return a.startSample < b.startSample; });

        // Restore selectedSlice index after sort
        if (selStart >= 0)
        {
            for (int i = 0; i < numSlices; ++i)
            {
                if (slices[i].startSample == selStart && slices[i].endSample == selEnd)
                {
                    selectedSlice = i;
                    break;
                }
            }
        }
    }

    int root = rootNote.load();
    for (int i = 0; i < numSlices; ++i)
        slices[i].midiNote = std::min (root + i, kMaxMidiNote);
    rebuildMidiMap();
}
