#include "GroupActions.h"
#include "Callout.h"
#include "GroupCallouts.h"
#include "GroupLanes.h"
#include "GroupUi.h"
#include "SampleLane.h"
#include "../audio/GroupMix.h"

namespace
{
juce::String ellipsis (const char* text)
{
    return juce::String (text) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
}

juce::PopupMenu::Options optionsFor (juce::Component& anchor, juce::Rectangle<int> area)
{
    return juce::PopupMenu::Options().withTargetComponent (&anchor).withTargetScreenArea (anchor.localAreaToGlobal (area));
}
}

void GroupActions::showSampleMenu (juce::Component& anchor, juce::Rectangle<int> area, int sampleId,
                                   std::function<void()> removeSample)
{
    const auto& ui = processor.getUiSliceSnapshot();
    const double rate = processor.sampleData.getDecodedSampleRate();
    const auto candidates = GroupUi::joinCandidates (ui, sampleId, rate);
    const bool canGroup = std::any_of (candidates.begin(), candidates.end(), [] (const auto& c) { return c.canJoin; });

    // Groups this sample could join: same length, not full.
    juce::PopupMenu groups;
    std::vector<int> groupAnchors;
    const auto* sample = GroupUi::findSample (ui, sampleId);
    for (int g = 0; g < ui.sampleGroups.count && sample != nullptr; ++g)
    {
        const auto& group = ui.sampleGroups.groups[(size_t) g];
        const auto* anchorSample = GroupUi::findSample (ui, group.anchorId());
        const bool fits = anchorSample != nullptr && group.numMembers < SampleGroup::kMaxMembers
                          && SampleGroups::lengthsMatch (anchorSample->numFrames, sample->numFrames, rate);
        groups.addItem (200 + (int) groupAnchors.size(), GroupUi::groupName (ui, group), fits);
        groupAnchors.push_back (group.anchorId());
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (sample != nullptr ? sample->fileName.toString() : juce::String());
    menu.addItem (1, ellipsis ("Group with"), canGroup);
    if (! groupAnchors.empty())
        menu.addSubMenu ("Add to group", groups);
    menu.addItem (2, "Duplicate sample", duplicateSample != nullptr);
    menu.addSeparator();
    menu.addItem (3, ellipsis ("Remove"));

    juce::Component::SafePointer<juce::Component> safe (&anchor);
    menu.showMenuAsync (optionsFor (anchor, area), [this, safe, area, sampleId, groupAnchors, removeSample] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result == 1)
            GroupCallouts::showPicker (*safe, area, processor, sampleId);
        else if (result == 2 && duplicateSample)
            duplicateSample (sampleId);
        else if (result == 3 && removeSample)
            removeSample();
        else if (result >= 200 && result - 200 < (int) groupAnchors.size())
        {
            const int anchorId = groupAnchors[(size_t) (result - 200)];
            const auto& snapshot = processor.getUiSliceSnapshot();
            int ownSlices = 0;
            for (int i = 0; i < snapshot.numSlices; ++i)
                if (snapshot.slices[(size_t) i].sampleId == sampleId)
                    ++ownSlices;

            auto& p = processor;
            const auto add = [&p, anchorId, sampleId] { GroupUi::push (p, IntersectProcessor::CmdGroupAddMembers, anchorId, { sampleId }); };
            if (ownSlices == 0)
                add();
            else
                showConfirmCallout (*safe, area, "Add to " + GroupUi::groupName (snapshot, *snapshot.sampleGroups.groupWith (anchorId))
                                                     + "? Its " + juce::String (ownSlices) + (ownSlices == 1 ? " slice" : " slices")
                                                     + " will be removed.",
                                    "ADD", add);
        }
    });
}

void GroupActions::showGroupMenu (juce::Component& anchor, juce::Rectangle<int> area, int anchorId)
{
    const auto& ui = processor.getUiSliceSnapshot();
    const auto* group = ui.sampleGroups.groupWith (anchorId);
    if (group == nullptr)
        return;

    const auto candidates = GroupUi::joinCandidates (ui, anchorId, processor.sampleData.getDecodedSampleRate());
    const bool canAdd = group->numMembers < SampleGroup::kMaxMembers
                        && std::any_of (candidates.begin(), candidates.end(), [] (const auto& c) { return c.canJoin; });

    juce::PopupMenu removeMenu, duplicateMenu;
    std::vector<int> members;
    for (int m = 0; m < group->numMembers; ++m)
    {
        const int id = group->memberIds[(size_t) m];
        const auto label = juce::String (GroupUi::memberNumber (m)) + "  " + GroupUi::memberName (ui, id);
        removeMenu.addItem (100 + m, label);
        duplicateMenu.addItem (200 + m, label, duplicateSample != nullptr);
        members.push_back (id);
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (GroupUi::groupName (ui, *group));
    menu.addItem (1, ellipsis ("Add sample"), canAdd);
    menu.addItem (2, ellipsis ("Mix"));
    menu.addSubMenu ("Remove from group", removeMenu);
    menu.addSubMenu ("Duplicate as separate sample", duplicateMenu);
    menu.addItem (3, "Ungroup");
    menu.addSeparator();
    menu.addItem (4, ellipsis ("Remove group"));

    juce::Component::SafePointer<juce::Component> safe (&anchor);
    menu.showMenuAsync (optionsFor (anchor, area), [this, safe, area, anchorId, members] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result == 1)
            GroupCallouts::showPicker (*safe, area, processor, anchorId);
        else if (result == 2)
            GroupCallouts::showMix (*safe, area, processor, anchorId);
        else if (result == 3)
            GroupUi::push (processor, IntersectProcessor::CmdUngroup, anchorId);
        else if (result == 4)
            removeGroup (*safe, area, anchorId, false);
        else if (result >= 100 && result < 100 + (int) members.size())
            GroupUi::push (processor, IntersectProcessor::CmdGroupRemoveMember, members[(size_t) (result - 100)]);
        else if (result >= 200 && result < 200 + (int) members.size() && duplicateSample)
            duplicateSample (members[(size_t) (result - 200)]);
    });
}

void GroupActions::removeGroup (juce::Component& anchor, juce::Rectangle<int> area, int anchorId, bool skipConfirm)
{
    const auto& ui = processor.getUiSliceSnapshot();
    const auto* group = ui.sampleGroups.groupWith (anchorId);
    if (group == nullptr)
        return;

    std::vector<int> ids (group->memberIds.begin(), group->memberIds.begin() + group->numMembers);
    auto& p = processor;
    const auto remove = [&p, ids] { p.deleteSessionSamplesAsync (ids); };
    if (skipConfirm)
        return remove();

    showConfirmCallout (anchor, area,
                        "Remove " + GroupUi::groupName (ui, *group) + " and its " + juce::String ((int) ids.size()) + " samples?",
                        "REMOVE", remove);
}

void GroupActions::attach (SampleLane& lane, GroupLaneControls& controls)
{
    duplicateSample = [this] (int sampleId) { processor.duplicateSessionSampleAsync (sampleId); };
    lane.onSampleMenuRequested = [this, &lane] (int sampleId, juce::Rectangle<int> area)
    {
        showSampleMenu (lane, area, sampleId, [&lane, sampleId] { lane.requestRemoveSample (sampleId, false); });
    };
    lane.onGroupMenuRequested = [this, &lane] (int anchorId, juce::Rectangle<int> area) { showGroupMenu (lane, area, anchorId); };
    lane.onGroupMixRequested = [this, &lane] (int anchorId, juce::Rectangle<int> area)
    {
        GroupCallouts::showMix (lane, area, processor, anchorId);
    };
    lane.onGroupRemoveRequested = [this, &lane] (int anchorId, juce::Rectangle<int> area, bool skipConfirm)
    {
        removeGroup (lane, area, anchorId, skipConfirm);
    };
    controls.onDuplicateRequested = [this] (int sampleId)
    {
        if (duplicateSample)
            duplicateSample (sampleId);
    };
}

juce::File GroupActions::renderGroupMix (int anchorId, const juce::File& destination) const
{
    const auto snapshot = processor.sampleData.getSnapshot();
    const auto* group = processor.getUiSliceSnapshot().sampleGroups.groupAnchoredBy (anchorId);
    if (snapshot == nullptr || group == nullptr || destination == juce::File() || snapshot->decodedSampleRate <= 0.0)
        return {};

    const auto mix = GroupMix::forGroup (*group, snapshot->sessionSamples, snapshot->buffer);
    if (! mix.isActive())
    {
        processor.showTransientStatusMessage ("Every member of this group is muted or missing", true);
        return {};
    }

    const auto audio = mix.render (mix.anchorStart, mix.anchorStart + mix.anchorFrames);
    destination.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (destination);
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (snapshot->decodedSampleRate)
                                                         .withNumChannels (2).withBitsPerSample (24);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
    {
        processor.showTransientStatusMessage ("Couldn't write the group's audio", true);
        return {};
    }
    return destination;
}
