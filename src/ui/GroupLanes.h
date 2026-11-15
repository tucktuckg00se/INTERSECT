#pragma once

#include "TimelineMap.h"
#include "WaveformCache.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

/** Draws group slots in the waveform view: one lane per member with its own peaks (under the
    slices), then the dims and hatching that show what each slice plays (above them). */
class GroupLanePainter
{
public:
    void paintLanes (juce::Graphics& g, juce::Rectangle<int> bounds, const IntersectProcessor::UiSliceSnapshot& ui,
                     const SampleData::DecodedSample& sample, const TimelineMap& map, const TimelineMap::View& view);

    void paintOverlays (juce::Graphics& g, juce::Rectangle<int> bounds, const IntersectProcessor::UiSliceSnapshot& ui,
                        const TimelineMap& map, const TimelineMap::View& view) const;

    /** Lane `memberIndex` of `numMembers` in a slot `height` tall. */
    static juce::Rectangle<int> laneBounds (juce::Rectangle<int> slot, int memberIndex, int numMembers);

private:
    struct CachedLane
    {
        WaveformCache cache;
        int viewStart = -1, viewLength = 0, width = 0, timelineStart = 0, memberStart = 0;
        const void* samplePtr = nullptr;
    };
    std::map<int, CachedLane> lanes;   // by member sampleId
};

/** The controls drawn over group lanes. Takes the mouse only on its own controls, so drags on the
    waveform underneath keep working:
    - lane headers that rest as number + M and open (name, level, remove, dots) when pointed at;
    - the selected slice's own M per lane, inside the slice, only when it fits;
    - + ADD SAMPLE under a group's lanes, + GROUP on a standalone sample you point at. */
class GroupLaneControls : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GroupLaneControls (IntersectProcessor& p);

    /** Duplicate as separate sample (dots menu): the editor adds the file again with the slices. */
    std::function<void (int sampleId)> onDuplicateRequested;

    /** Called by the waveform view with where the mouse is over it (-1 when it left), so + GROUP
        shows on the slot under the pointer. */
    void setPointer (juce::Point<int> position);

    /** Re-reads the kit and lays out the controls (the waveform view calls this when it paints). */
    void refresh (const TimelineMap& map, const TimelineMap::View& view);

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    struct Header
    {
        int sampleId = -1;
        int memberIndex = 0;
        bool muted = false;
        float gainDb = 0.0f;
        juce::String name;
        juce::Rectangle<int> lane, box, badge, mute, nameArea, level, dbArea, remove, dots;
        bool open = false;     // pointed at: shows name and the rest
        bool full = false;     // lanes tall enough for level and remove
        bool dense = false;    // lanes too short for a box: a strip at the left edge
    };
    struct SliceMute { int sampleId = -1; int memberIndex = 0; bool played = true; juce::Rectangle<int> area; };
    struct Chip { int sampleId = -1; juce::Rectangle<int> area; bool enabled = true; juce::String tooltip; };

    void layoutHeader (Header& header) const;
    Header* headerAt (juce::Point<int> p);
    void showMemberMenu (const Header& header);
    void toggleGroupMute (const Header& header);
    void setGainFromX (Header& header, int x);

    IntersectProcessor& processor;
    std::vector<Header> headers;
    std::vector<SliceMute> sliceMutes;
    int selectedSliceIdx = -1;
    bool sliceHasOwnMembers = false;
    uint32_t slicePlayed = 0;
    std::vector<Chip> addChips;      // + ADD SAMPLE, one per visible group
    std::vector<Chip> groupChips;    // + GROUP on standalone slots
    juce::Point<int> pointer { -1, -1 };
    int openHeaderId = -1;
    int draggingLevelId = -1;
    TimelineMap lastMap;
    TimelineMap::View lastView;
};
