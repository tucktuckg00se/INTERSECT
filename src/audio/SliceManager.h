#pragma once
#include "../Constants.h"
#include "Slice.h"
#include <array>
#include <atomic>
#include <juce_core/juce_core.h>

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
    int  nextMidiNote() const;
    void repackMidiNotes (bool sortByPosition);

    float resolveParam (int sliceIdx, LockBit lockBit, float sliceValue, float globalDefault) const;

    Slice& getSlice (int idx)
    {
        jassert (juce::isPositiveAndBelow (idx, kMaxSlices));
        return slices[(size_t) idx];
    }

    const Slice& getSlice (int idx) const
    {
        jassert (juce::isPositiveAndBelow (idx, kMaxSlices));
        return slices[(size_t) idx];
    }
    int getNumSlices() const { return numSlices; }
    void setNumSlices (int n) { numSlices = juce::jlimit (0, kMaxSlices, n); }

    std::atomic<int> selectedSlice { -1 };
    std::atomic<int> rootNote { kDefaultRootNote };

    void setSlicePalette (const juce::Colour* p) { palette.store (p, std::memory_order_relaxed); }

    void recolourFromPalette()
    {
        const auto* p = palette.load (std::memory_order_relaxed);
        if (! p) return;
        for (int i = 0; i < numSlices; ++i)
            slices[i].colour = p[i % 16];
    }

private:
    std::atomic<const juce::Colour*> palette { nullptr };

    std::array<Slice, kMaxSlices> slices;
    int numSlices = 0;
    std::array<int, kMidiNoteCount> midiMap;               // first slice for note (legacy compat)
    std::array<std::vector<int>, kMidiNoteCount> midiMapMulti;  // all slices for note
};
