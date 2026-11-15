#include "GroupActions.h"
#include "Callout.h"
#include "CalloutMenu.h"
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

// Add to group: a sample's own slices go when it joins, so ask first when it has any.
void addToGroup (IntersectProcessor& processor, juce::Component& anchor, juce::Rectangle<int> area, int sampleId, int anchorId)
{
    const auto& ui = processor.getUiSliceSnapshot();
    const auto* group = ui.sampleGroups.groupWith (anchorId);
    if (group == nullptr)
        return;
    int ownSlices = 0;
    for (int i = 0; i < ui.numSlices; ++i)
        if (ui.slices[(size_t) i].sampleId == sampleId)
            ++ownSlices;

    auto& p = processor;
    const auto add = [&p, anchorId, sampleId] { GroupUi::push (p, IntersectProcessor::CmdGroupAddMembers, anchorId, { sampleId }); };
    if (ownSlices == 0)
        add();
    else
        showConfirmCallout (anchor, area, "Add to " + GroupUi::groupName (ui, *group) + "? Its " + juce::String (ownSlices)
                                              + (ownSlices == 1 ? " slice" : " slices") + " will be removed.",
                            "ADD", add);
}
}

void GroupActions::showSampleMenu (juce::Component& anchor, juce::Rectangle<int> area, int sampleId,
                                   std::function<void()> removeSample)
{
    const auto& ui = processor.getUiSliceSnapshot();
    const double rate = processor.sampleData.getDecodedSampleRate();
    const auto candidates = GroupUi::joinCandidates (ui, sampleId, rate);
    const bool canGroup = std::any_of (candidates.begin(), candidates.end(), [] (const auto& c) { return c.canJoin; });
    const auto* sample = GroupUi::findSample (ui, sampleId);
    juce::Component::SafePointer<juce::Component> safe (&anchor);
    auto& p = processor;

    // Groups this sample could join: same length, not full.
    std::vector<CalloutMenu::Item> groups;
    for (int g = 0; g < ui.sampleGroups.count && sample != nullptr; ++g)
    {
        const auto& group = ui.sampleGroups.groups[(size_t) g];
        const auto* anchorSample = GroupUi::findSample (ui, group.anchorId());
        const bool fits = anchorSample != nullptr && group.numMembers < SampleGroup::kMaxMembers
                          && SampleGroups::lengthsMatch (anchorSample->numFrames, sample->numFrames, rate);
        const int anchorId = group.anchorId();
        groups.push_back ({ GroupUi::groupName (ui, group),
                            [&p, safe, area, sampleId, anchorId] { if (safe != nullptr) addToGroup (p, *safe, area, sampleId, anchorId); },
                            fits });
    }

    std::vector<CalloutMenu::Item> items;
    items.push_back ({ ellipsis ("Group with"),
                       [&p, safe, area, sampleId] { if (safe != nullptr) GroupCallouts::showPicker (*safe, area, p, sampleId); },
                       canGroup });
    if (! groups.empty())
    {
        CalloutMenu::Item add;
        add.text = "Add to group";
        add.submenu = std::move (groups);
        items.push_back (std::move (add));
    }
    items.push_back ({ "Duplicate sample", [this, sampleId] { if (duplicateSample) duplicateSample (sampleId); }, duplicateSample != nullptr });
    CalloutMenu::Item remove { ellipsis ("Remove"), [removeSample] { if (removeSample) removeSample(); } };
    remove.destructive = true;
    remove.separatorBefore = true;
    items.push_back (std::move (remove));

    CalloutMenu::show (anchor, area, sample != nullptr ? sample->fileName.toString() : juce::String(), std::move (items));
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
    juce::Component::SafePointer<juce::Component> safe (&anchor);
    auto& p = processor;

    CalloutMenu::Item removeMember, duplicateMember;
    removeMember.text = "Remove from group";
    duplicateMember.text = "Duplicate as separate sample";
    for (int m = 0; m < group->numMembers; ++m)
    {
        const int id = group->memberIds[(size_t) m];
        const auto label = juce::String (GroupUi::memberNumber (m)) + "  " + GroupUi::memberName (ui, id);
        removeMember.submenu.push_back ({ label, [&p, id] { GroupUi::push (p, IntersectProcessor::CmdGroupRemoveMember, id); } });
        duplicateMember.submenu.push_back ({ label, [this, id] { if (duplicateSample) duplicateSample (id); }, duplicateSample != nullptr });
    }

    std::vector<CalloutMenu::Item> items;
    items.push_back ({ ellipsis ("Add sample"),
                       [&p, safe, area, anchorId] { if (safe != nullptr) GroupCallouts::showPicker (*safe, area, p, anchorId); },
                       canAdd });
    items.push_back ({ ellipsis ("Mix"), [&p, safe, area, anchorId] { if (safe != nullptr) GroupCallouts::showMix (*safe, area, p, anchorId); } });
    items.push_back (std::move (removeMember));
    items.push_back (std::move (duplicateMember));
    items.push_back ({ "Ungroup", [&p, anchorId] { GroupUi::push (p, IntersectProcessor::CmdUngroup, anchorId); } });
    CalloutMenu::Item removeAll { ellipsis ("Remove group"),
                                  [this, safe, area, anchorId] { if (safe != nullptr) removeGroup (*safe, area, anchorId, false); } };
    removeAll.destructive = true;
    removeAll.separatorBefore = true;
    items.push_back (std::move (removeAll));

    CalloutMenu::show (anchor, area, GroupUi::groupName (ui, *group), std::move (items));
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
