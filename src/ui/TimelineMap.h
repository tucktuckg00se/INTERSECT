#pragma once

#include "../PluginProcessor.h"
#include <vector>

/** The timeline the editor draws: the kit's samples side by side, except group members other than
    the anchor, so a group shows as one slot (its anchor's length) where the anchor sits. Maps kit
    frames (positions in the concatenated buffer, which slices and voices use) to timeline frames and
    pixels and back, for a view's zoom and scroll. With no groups the timeline is the kit buffer.

    Every lane builds one from the processor's UI snapshot (message thread), so the waveform, slice
    lane, sample lane and scroll bar always agree on where a frame is drawn. */
class TimelineMap
{
public:
    struct Segment
    {
        int sampleId = -1;
        int kitStart = 0;        // first frame in the kit buffer
        int timelineStart = 0;   // first frame on the timeline
        int numFrames = 0;
        bool isGroup = false;    // the slot of a group (sampleId is its anchor)
    };

    /** The part of the timeline a view shows, in timeline frames. */
    struct View
    {
        int start = 0;
        int length = 1;
        int width = 1;           // pixels

        double framesPerPixel() const noexcept { return (double) length / (double) juce::jmax (1, width); }
    };

    TimelineMap() = default;
    TimelineMap (const IntersectProcessor::UiSliceSnapshot& ui, int totalKitFrames) { build (ui, totalKitFrames); }

    /** The editor's timeline right now (UI snapshot and the active kit's length). */
    static TimelineMap fromProcessor (const IntersectProcessor& processor);

    void build (const IntersectProcessor::UiSliceSnapshot& ui, int kitFrames);

    int getTotalFrames() const noexcept { return totalFrames; }
    const std::vector<Segment>& getSegments() const noexcept { return segments; }
    bool hasHiddenFrames() const noexcept { return hidden; }

    /** Where a kit frame is drawn. A hidden member's frame lands at the same offset in its group's
        slot (members line up with the anchor). */
    int kitToTimeline (int kitFrame) const noexcept;

    /** The kit frame drawn at a timeline frame (in a group slot: the anchor's frame). */
    int timelineToKit (int timelineFrame) const noexcept;

    /** The visible segment holding a timeline frame, or nullptr past the end. */
    const Segment* segmentAt (int timelineFrame) const noexcept;

    /** The visible segment showing `sampleId` (a member's: its group's slot), or nullptr. */
    const Segment* segmentFor (int sampleId) const noexcept;

    /** zoom 1..16384 and scroll 0..1, as the processor stores them, for a lane `width` pixels wide. */
    View viewFor (float zoom, float scroll, int width) const noexcept;
    View viewFor (const IntersectProcessor& processor, int width) const noexcept
    {
        return viewFor (processor.zoom.load(), processor.scroll.load(), width);
    }

    int timelineToPixel (const View& view, int timelineFrame) const noexcept
    {
        return (int) ((float) (timelineFrame - view.start) / (float) view.length * (float) view.width);
    }

    int pixelToTimeline (const View& view, int px) const noexcept
    {
        return view.start + (int) ((float) px / (float) juce::jmax (1, view.width) * (float) view.length);
    }

    int kitToPixel (const View& view, int kitFrame) const noexcept { return timelineToPixel (view, kitToTimeline (kitFrame)); }
    int pixelToKit (const View& view, int px) const noexcept { return timelineToKit (pixelToTimeline (view, px)); }

private:
    std::vector<Segment> segments;      // visible slots, in timeline order
    struct Member { int sampleId; int kitStart; int numFrames; int segment; };
    std::vector<Member> members;        // every session sample and the segment that shows it
    int totalFrames = 0;
    int kitFrames = 0;
    bool hidden = false;
};
