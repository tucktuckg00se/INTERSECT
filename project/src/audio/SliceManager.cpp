#include "SliceManager.h"
#include <algorithm>
#include <cmath>

const juce::Colour SliceManager::slicePalette[16] = {
    juce::Colour::fromFloatRGBA (0.30f, 0.55f, 0.60f, 1.0f),  // Cold Teal
    juce::Colour::fromFloatRGBA (0.55f, 0.28f, 0.28f, 1.0f),  // Muted Red
    juce::Colour::fromFloatRGBA (0.30f, 0.50f, 0.35f, 1.0f),  // Dark Green
    juce::Colour::fromFloatRGBA (0.55f, 0.45f, 0.25f, 1.0f),  // Rust
    juce::Colour::fromFloatRGBA (0.40f, 0.30f, 0.55f, 1.0f),  // Dusk Violet
    juce::Colour::fromFloatRGBA (0.50f, 0.50f, 0.30f, 1.0f),  // Olive
    juce::Colour::fromFloatRGBA (0.25f, 0.50f, 0.55f, 1.0f),  // Steel Cyan
    juce::Colour::fromFloatRGBA (0.50f, 0.30f, 0.42f, 1.0f),  // Dark Rose
    juce::Colour::fromFloatRGBA (0.35f, 0.48f, 0.28f, 1.0f),  // Moss
    juce::Colour::fromFloatRGBA (0.50f, 0.35f, 0.30f, 1.0f),  // Clay
    juce::Colour::fromFloatRGBA (0.32f, 0.35f, 0.55f, 1.0f),  // Slate Blue
    juce::Colour::fromFloatRGBA (0.45f, 0.45f, 0.35f, 1.0f),  // Concrete
    juce::Colour::fromFloatRGBA (0.42f, 0.28f, 0.45f, 1.0f),  // Plum
    juce::Colour::fromFloatRGBA (0.28f, 0.48f, 0.42f, 1.0f),  // Patina
    juce::Colour::fromFloatRGBA (0.48f, 0.35f, 0.45f, 1.0f),  // Mauve
    juce::Colour::fromFloatRGBA (0.38f, 0.48f, 0.40f, 1.0f),  // Lichen
};

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
    s.pingPong       = false;

    // Assign colour from palette
    s.colour = slicePalette[idx % 16];

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

void SliceManager::createDefaultSlice (int sampleLen)
{
    if (sampleLen <= 0)
        return;

    // Reset all slices
    numSlices = 0;
    for (auto& s : slices)
        s.active = false;

    createSlice (0, sampleLen);
    selectedSlice = 0;
}

void SliceManager::rebuildMidiMap()
{
    midiMap.fill (-1);
    for (int i = 0; i < numSlices; ++i)
    {
        if (slices[i].active)
        {
            int note = slices[i].midiNote;
            if (note >= 0 && note < 128)
                midiMap[note] = i;
        }
    }
}

int SliceManager::midiNoteToSlice (int note) const
{
    if (note < 0 || note >= 128)
        return -1;
    return midiMap[note];
}

float SliceManager::resolveParam (int sliceIdx, LockBit lockBit, float sliceValue, float globalDefault) const
{
    if (sliceIdx < 0 || sliceIdx >= numSlices)
        return globalDefault;

    return (slices[sliceIdx].lockMask & lockBit) ? sliceValue : globalDefault;
}
