#pragma once
#include <array>
#include "Constants.h"
#include "audio/Slice.h"
#include "audio/SliceManager.h"
#include "params/ParamUndoState.h"

class UndoManager
{
public:
    static constexpr int kMaxSnapshots = 32;

    struct Snapshot
    {
        std::array<Slice, SliceManager::kMaxSlices> slices;
        int numSlices = 0;
        int selectedSlice = -1;
        int rootNote = kDefaultRootNote;
        ParamUndoState params;
        bool midiSelectsSlice = false;
        bool snapToZeroCrossing = false;
    };

    void push (const Snapshot& snap)
    {
        // When the current state already lives inside the ring (after undo/redo),
        // keep that state as the undo baseline and drop only the redo tail.
        if (current < size)
        {
            size = current + 1;
            current = size;
            return;
        }

        if (size < kMaxSnapshots)
        {
            setSnapshot (size, snap);
            ++size;
            current = size;
            return;
        }

        // Full ring: overwrite the oldest entry and advance the logical start.
        setSnapshot (size, snap);
        start = (start + 1) % kMaxSnapshots;
        current = size;
    }

    bool canUndo() const
    {
        if (size == 0)
            return false;

        return current == size ? size > 0 : current > 0;
    }

    bool canRedo() const
    {
        return current < size - 1;
    }

    // Call with the current state before restoring; stores it so redo can reach it
    Snapshot undo (const Snapshot& currentState)
    {
        if (! canUndo())
            return currentState;

        // If the live current state is not yet in the history, append it so redo
        // can return to it after the undo step.
        if (current == size)
        {
            if (size < kMaxSnapshots)
                setSnapshot (size, currentState);
            else
            {
                setSnapshot (size, currentState);
                start = (start + 1) % kMaxSnapshots;
            }

            if (size < kMaxSnapshots)
                ++size;

            current = size - 1;
        }

        --current;
        return getSnapshot (current);
    }

    Snapshot redo()
    {
        if (size == 0)
            return {};
        if (current >= size - 1)
            return getSnapshot (size - 1);

        ++current;
        return getSnapshot (current);
    }

    void clear()
    {
        start = 0;
        size = 0;
        current = 0;
    }

private:
    int physicalIndexForLogical (int logicalIndex) const
    {
        jassert (logicalIndex >= 0 && logicalIndex < size);
        return (start + logicalIndex) % kMaxSnapshots;
    }

    const Snapshot& getSnapshot (int logicalIndex) const
    {
        return undoStack[(size_t) physicalIndexForLogical (logicalIndex)];
    }

    void setSnapshot (int logicalIndex, const Snapshot& snap)
    {
        undoStack[(size_t) ((start + logicalIndex) % kMaxSnapshots)] = snap;
    }

    std::array<Snapshot, kMaxSnapshots> undoStack {};
    int start = 0;
    int size = 0;
    int current = 0;  // [0, size): current state stored in ring, size: live state not yet stored
};
