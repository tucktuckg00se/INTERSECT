#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstddef>
#include <utility>

/** A fixed-size queue between exactly one producer thread and one consumer thread, for handing
    objects across without locks or allocation. Items are moved into and out of preallocated
    slots, and the consumer resets each slot after taking it, so the producer only ever writes
    into empty items. That is what lets the audio thread push an item that owns heap memory
    (a sample buffer, a hand-off object) without freeing anything: the consumer frees it later.

    Used audio thread -> message thread (objects to free, loads to start) and message thread ->
    audio thread (prepared sample removals).
*/
template <typename Item, int Capacity>
class SpscQueue
{
public:
    /** Producer thread. Moves the item in; returns false (leaving it with the caller) when full. */
    bool push (Item&& item) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
            slots[(size_t) scope.startIndex1] = std::move (item);
        else if (scope.blockSize2 > 0)
            slots[(size_t) scope.startIndex2] = std::move (item);
        else
            return false;
        return true;
    }

    /** Consumer thread. Moves the oldest item into `out`; returns false when empty. */
    bool pop (Item& out)
    {
        const auto scope = fifo.read (1);
        const int index = scope.blockSize1 > 0 ? scope.startIndex1
                        : scope.blockSize2 > 0 ? scope.startIndex2 : -1;
        if (index < 0)
            return false;
        out = std::move (slots[(size_t) index]);
        slots[(size_t) index] = Item {};
        return true;
    }

    /** Consumer thread. Drops every waiting item (frees what they own here). */
    void clear()
    {
        Item item;
        while (pop (item))
            item = Item {};
    }

    int getNumWaiting() const noexcept { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo { Capacity + 1 };   // an AbstractFifo holds one item less than its size
    std::array<Item, (size_t) Capacity + 1> slots {};
};
