#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;

/** MEMBERS on the signal chain's bottom row, for a group slice: the members it plays as round
    numbered badges, then "own" (orange: the slice has its own members) or "group mix". A click
    opens the MEMBERS list (GroupCallouts::showSliceMembers). Works for any group size. */
class SliceMembersButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SliceMembersButton (IntersectProcessor& p);

    /** Re-reads the slice from the UI snapshot. False when it isn't a group slice (hide the button). */
    bool setSlice (int sliceIdx);

    int getPreferredWidth() const;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    IntersectProcessor& processor;
    int sliceIdx = -1;
    bool own = false;
    std::vector<std::pair<int, int>> badges;   // (member index, number) of members the slice plays
    int hiddenCount = 0;                       // playing members past the seventh badge
};
