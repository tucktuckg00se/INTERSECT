#pragma once
#include <deque>
#include <juce_data_structures/juce_data_structures.h>
#include "audio/Slice.h"
#include "audio/SliceManager.h"

class UndoManager
{
public:
    static constexpr int kMaxSnapshots = 32;

    struct Snapshot
    {
        std::array<Slice, SliceManager::kMaxSlices> slices;
        int numSlices = 0;
        int selectedSlice = -1;
        int rootNote = 36;
        juce::ValueTree apvtsState;
        bool midiSelectsSlice = false;
        bool snapToZeroCrossing = false;
    };

    void push (const Snapshot& snap)
    {
        // Discard any redo history
        while (undoStack.size() > pos)
            undoStack.pop_back();

        undoStack.push_back (snap);

        // Enforce max size
        while ((int) undoStack.size() > kMaxSnapshots)
            undoStack.pop_front();

        pos = (int) undoStack.size();
    }

    bool canUndo() const { return pos > 0; }
    bool canRedo() const { return pos < (int) undoStack.size(); }

    // Returns the snapshot to restore (the state before the last change)
    Snapshot undo()
    {
        if (pos > 0)
            --pos;
        return undoStack[(size_t) pos];
    }

    Snapshot redo()
    {
        if (pos < (int) undoStack.size())
            ++pos;
        return undoStack[(size_t) pos - 1];
    }

    void clear()
    {
        undoStack.clear();
        pos = 0;
    }

private:
    std::deque<Snapshot> undoStack;
    int pos = 0;  // position after the last pushed snapshot
};
