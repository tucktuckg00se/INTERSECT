#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class IntersectProcessor;
class SampleLane;
class GroupLaneControls;

/** The sample lane's group menus (right-click a sample or a group) and removing a whole group.
    Owned by the editor, which wires the sample lane to it; the menus open callouts and push
    commands, so the editor itself stays out of group work. Message thread. */
class GroupActions
{
public:
    explicit GroupActions (IntersectProcessor& p) : processor (p) {}

    /** Wires the sample lane's group callbacks and the lane controls' Duplicate to these actions. */
    void attach (SampleLane& lane, GroupLaneControls& controls);

    /** AUDIO on a group: writes the group mix (unmuted members at their levels) to `destination`
        as a WAV at the kit's rate. Returns the file, or {} if it couldn't. */
    juce::File renderGroupMix (int anchorId, const juce::File& destination) const;

    /** Duplicate sample / Duplicate as separate sample: adds the file again with a copy of its slices. */
    std::function<void (int sampleId)> duplicateSample;

    /** Right-click on a standalone sample: Group with…, Add to group ›, Duplicate sample, Remove…. */
    void showSampleMenu (juce::Component& anchor, juce::Rectangle<int> area, int sampleId,
                         std::function<void()> removeSample);

    /** Right-click on a group: Add sample…, Remove from group ›, Duplicate as separate sample ›,
        Ungroup, Remove group…. */
    void showGroupMenu (juce::Component& anchor, juce::Rectangle<int> area, int anchorId);

    /** Removes the group and all its samples (one undo step), asking first unless skipConfirm. */
    void removeGroup (juce::Component& anchor, juce::Rectangle<int> area, int anchorId, bool skipConfirm);

private:
    IntersectProcessor& processor;
};
