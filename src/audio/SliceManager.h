#pragma once
#include "Slice.h"
#include <array>

class SliceManager
{
public:
    static constexpr int kMaxSlices = 128;

    SliceManager();

    int  createSlice (int start, int end);
    void deleteSlice (int idx);
    void clearAll();
    void rebuildMidiMap();
    int  midiNoteToSlice (int note) const;
    const std::vector<int>& midiNoteToSlices (int note) const;

    float resolveParam (int sliceIdx, LockBit lockBit, float sliceValue, float globalDefault) const;

    Slice& getSlice (int idx) { return slices[idx]; }
    const Slice& getSlice (int idx) const { return slices[idx]; }
    int getNumSlices() const { return numSlices; }
    void setNumSlices (int n) { numSlices = juce::jlimit (0, kMaxSlices, n); }

    int  selectedSlice = -1;
    std::atomic<int> rootNote { 36 };

    static const juce::Colour slicePalette[16];

private:
    std::array<Slice, kMaxSlices> slices;
    int numSlices = 0;
    std::array<int, 128> midiMap;               // first slice for note (legacy compat)
    std::array<std::vector<int>, 128> midiMapMulti;  // all slices for note
};
