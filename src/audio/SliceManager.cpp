#include "SliceManager.h"
#include <algorithm>
#include <cmath>

SliceManager::SliceManager()
{
    midiMap.fill (-1);
}

int SliceManager::createSlice (int start, int end)
{
    if (numSlices >= kMaxSlices)
        return -1;

    // Enforce minimum 64 samples
    if (std::abs (end - start) < 64)
        end = start + 64;

    // Ensure start < end
    if (start > end)
        std::swap (start, end);

    int idx = numSlices;
    auto& s = slices[idx];

    s.active      = true;
    s.startSample = start;
    s.endSample   = end;
    s.midiNote    = std::min (rootNote.load() + idx, 127);
    s.lockMask    = 0;

    // Default override values
    s.bpm            = 120.0f;
    s.pitchSemitones = 0.0f;
    s.algorithm      = 0;
    s.attackSec      = 0.005f;
    s.decaySec       = 0.1f;
    s.sustainLevel   = 1.0f;
    s.releaseSec     = 0.02f;
    s.muteGroup      = 1;
    s.loopMode       = 0;

    // Assign colour from palette
    s.colour = palette ? palette[idx % 16] : juce::Colour (0xFF4D8C99);

    numSlices++;
    rebuildMidiMap();
    return idx;
}

void SliceManager::deleteSlice (int idx)
{
    if (idx < 0 || idx >= numSlices)
        return;

    // Shift all slices after idx down by one
    for (int i = idx; i < numSlices - 1; ++i)
    {
        slices[i] = slices[i + 1];
        slices[i].midiNote = std::min (rootNote.load() + i, 127);
    }

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
            if (note >= 0 && note < 128)
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
    if (note < 0 || note >= 128)
        return -1;
    return midiMap[note];
}

const std::vector<int>& SliceManager::midiNoteToSlices (int note) const
{
    static const std::vector<int> empty;
    if (note < 0 || note >= 128)
        return empty;
    return midiMapMulti[note];
}

float SliceManager::resolveParam (int sliceIdx, LockBit lockBit, float sliceValue, float globalDefault) const
{
    if (sliceIdx < 0 || sliceIdx >= numSlices)
        return globalDefault;

    return (slices[sliceIdx].lockMask & lockBit) ? sliceValue : globalDefault;
}
