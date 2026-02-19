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
        // Discard any redo history beyond current position
        while ((int) undoStack.size() > pos)
            undoStack.pop_back();

        undoStack.push_back (snap);

        // Enforce max size (remove from front)
        while ((int) undoStack.size() > kMaxSnapshots)
        {
            undoStack.pop_front();
            --pos;
        }

        pos = (int) undoStack.size();
    }

    bool canUndo() const { return pos > 0; }
    bool canRedo() const { return pos < (int) undoStack.size() - 1; }

    // Call with the current state before restoring; stores it so redo can reach it
    Snapshot undo (const Snapshot& currentState)
    {
        if (pos <= 0)
            return currentState;

        // If pos == stack size, the current state has never been saved — append it
        if (pos == (int) undoStack.size())
            undoStack.push_back (currentState);
        else
            undoStack[(size_t) pos] = currentState;

        --pos;
        return undoStack[(size_t) pos];
    }

    Snapshot redo()
    {
        if (pos >= (int) undoStack.size() - 1)
            return undoStack.back();

        ++pos;
        return undoStack[(size_t) pos];
    }

    void clear()
    {
        undoStack.clear();
        pos = 0;
    }

private:
    std::deque<Snapshot> undoStack;
    int pos = 0;  // points one past the last "before" snapshot, or at the current state
};
