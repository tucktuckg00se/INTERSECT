#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;

/** The group popovers, on the shared Callout box (they scale with the editor and stay inside it).
    Each pushes its commands to the processor as you click; Esc or a click outside closes it. */
namespace GroupCallouts
{
    /** Group with… (targetSampleId not in a group: it becomes the anchor) or Add to group (target is
        in a group): tick same-length samples; the others are listed greyed with a reason. */
    void showPicker (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                     IntersectProcessor& processor, int targetSampleId);

    /** MIX: every member's number, name, mute, level, output and remove, for any group size. */
    void showMix (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                  IntersectProcessor& processor, int groupMemberId);

    /** A group slice's own members, as mutes: Group mix, UNMUTE ALL and one M per member. */
    void showSliceMembers (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                           IntersectProcessor& processor, int sliceIdx);
}
