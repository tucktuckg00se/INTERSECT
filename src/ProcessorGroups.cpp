// Sample groups, the processor's side (audio thread): the group commands, what they do to the
// slices and the kit, and the voices a group slice starts. The model and its pure logic are in
// audio/SampleGroups.

#include "PluginProcessor.h"

namespace
{
bool containsId (const int* ids, int count, int id)
{
    for (int i = 0; i < count; ++i)
        if (ids[i] == id)
            return true;
    return false;
}

// Status text built without allocating (the group commands run on the audio thread).
IntersectProcessor::StatusText statusText (const char* before, const juce::String& name = {}, const char* after = nullptr)
{
    IntersectProcessor::StatusText text;
    text.appendAscii (before);
    text.appendAscii (name.toRawUTF8());
    text.appendAscii (after);
    return text;
}
}

const SampleData::SessionSample* IntersectProcessor::findActiveSessionSample (int sampleId) const
{
    for (const auto& sample : sampleData.getSessionSamples())
        if (sample.sampleId == sampleId)
            return &sample;
    return nullptr;
}

bool IntersectProcessor::groupKeepsSlicesPlaying (int anchorId) const
{
    const auto* group = sampleGroups.groupAnchoredBy (anchorId);
    if (group == nullptr)
        return false;

    for (int m = 0; m < group->numMembers; ++m)
        if (const auto* member = findActiveSessionSample (group->memberIds[(size_t) m]))
            if (! member->isPlaceholder())
                return true;
    return false;
}

void IntersectProcessor::handleGroupCommand (const Command& cmd)
{
    const int numIds = juce::jlimit (0, (int) cmd.positions.size(), cmd.numPositions);

    switch (cmd.type)
    {
        case CmdGroupCreate:       createGroup (cmd.intParam1, cmd.positions.data(), numIds); break;
        case CmdGroupAddMembers:   addGroupMembers (cmd.intParam1, cmd.positions.data(), numIds); break;
        case CmdGroupRemoveMember: removeGroupMember (cmd.intParam1); break;
        case CmdUngroup:           ungroup (cmd.intParam1); break;

        case CmdSetMemberGain:
        case CmdSetMemberMute:
        case CmdSetMemberOutput:
        {
            auto* group = sampleGroups.groupWith (cmd.intParam1);
            const int member = group != nullptr ? group->indexOf (cmd.intParam1) : -1;
            if (member < 0)
                break;

            if (cmd.type == CmdSetMemberGain)
                group->gainDb[(size_t) member] = juce::jlimit (-100.0f, 24.0f, cmd.floatParam1);
            else if (cmd.type == CmdSetMemberOutput)
                group->outputBus[(size_t) member] = juce::jlimit (-1, 31, cmd.intParam2);
            else if (cmd.intParam2 != 0)
                group->mutedMask |= (1u << member);
            else
                group->mutedMask &= ~(1u << member);
            break;
        }

        default:
            break;
    }

    uiSnapshotDirty.store (true, std::memory_order_release);
}

// Group with: the anchor keeps its slices, which become the group's; the members' own slices go.
// Nothing changes unless every sample can join.
void IntersectProcessor::createGroup (int anchorId, const int* memberIds, int numMemberIds)
{
    const auto* anchor = findActiveSessionSample (anchorId);
    if (anchor == nullptr || sampleGroups.groupWith (anchorId) != nullptr)
    {
        setUiStatusMessage (statusText ("That sample is already in a group"), true);
        return;
    }
    if (sampleGroups.count >= SampleGroupTable::kMaxGroups)
    {
        setUiStatusMessage (statusText ("The kit already has the most groups it can hold (32)"), true);
        return;
    }

    addGroupMembers (anchorId, memberIds, numMemberIds);   // makes the group once a member is valid
}

void IntersectProcessor::addGroupMembers (int groupMemberId, const int* memberIds, int numMemberIds)
{
    // CmdGroupCreate arrives here with an anchor that is not in a group yet.
    const int existing = sampleGroups.indexOfGroupWith (groupMemberId);
    const int anchorId = existing >= 0 ? sampleGroups.groups[(size_t) existing].anchorId() : groupMemberId;
    const auto* anchor = findActiveSessionSample (anchorId);
    if (anchor == nullptr)
        return;

    const int alreadyIn = existing >= 0 ? sampleGroups.groups[(size_t) existing].numMembers : 1;
    const double rate = sampleData.getDecodedSampleRate() > 0.0 ? sampleData.getDecodedSampleRate() : currentSampleRate;

    std::array<int, SampleGroup::kMaxMembers> joining {};
    int numJoining = 0;
    for (int i = 0; i < numMemberIds; ++i)
    {
        const int id = memberIds[i];
        if (id == anchorId || containsId (joining.data(), numJoining, id))
            continue;

        const auto* member = findActiveSessionSample (id);
        if (member == nullptr)
            continue;
        if (sampleGroups.groupWith (id) != nullptr)
        {
            setUiStatusMessage (statusText ("A sample can only be in one group: ", member->fileName, " already is"), true);
            return;
        }
        if (! SampleGroups::lengthsMatch (anchor->numFrames, member->numFrames, rate))
        {
            setUiStatusMessage (statusText ("", member->fileName, " is a different length, so it can't join the group"), true);
            return;
        }
        if (alreadyIn + numJoining >= SampleGroup::kMaxMembers)
        {
            setUiStatusMessage (statusText ("A group holds up to 16 samples"), true);
            return;
        }
        joining[(size_t) numJoining++] = id;
    }

    if (numJoining == 0)
        return;

    int groupIndex = existing;
    if (groupIndex < 0)
    {
        groupIndex = sampleGroups.addGroup (anchorId);
        if (groupIndex < 0)
        {
            setUiStatusMessage (statusText ("The kit already has the most groups it can hold (32)"), true);
            return;
        }
        // The anchor's slices become the group's, following the group mix.
        for (int i = 0; i < sliceManager.getNumSlices(); ++i)
        {
            auto& s = sliceManager.getSlice (i);
            if (s.sampleId == anchorId)
            {
                s.lockMask &= ~kLockMembers;
                s.memberMask = 0;
            }
        }
    }

    auto& group = sampleGroups.groups[(size_t) groupIndex];
    for (int i = 0; i < numJoining; ++i)
        group.addMember (joining[(size_t) i]);

    // Members play the group's slices from now on; their own go (the UI asked first).
    for (int i = sliceManager.getNumSlices() - 1; i >= 0; --i)
        if (containsId (joining.data(), numJoining, sliceManager.getSlice (i).sampleId))
            sliceManager.deleteSlice (i);
    sliceManager.rebuildMidiMap();
}

int IntersectProcessor::copySlicesToSample (int fromSampleId, int toSampleId)
{
    if (findActiveSessionSample (toSampleId) == nullptr)
        return 0;

    int copied = 0;
    const int numOriginals = sliceManager.getNumSlices();
    for (int i = 0; i < numOriginals; ++i)
    {
        const Slice src = sliceManager.getSlice (i);
        if (src.sampleId != fromSampleId)
            continue;

        const int idx = sliceManager.createSlice (src.startSample, src.endSample);
        if (idx < 0)
            break;

        auto& dst = sliceManager.getSlice (idx);
        const int note = dst.midiNote;   // a new key, as COPY gives
        dst = src;
        dst.midiNote = note;
        dst.highNote = note;
        dst.sliceRootNote = note;
        dst.sampleId = toSampleId;
        dst.lockMask &= ~kLockMembers;
        dst.memberMask = 0;
        dst.parked = false;
        syncSliceAbsoluteToCurrentSession (dst);
        ++copied;
    }
    sliceManager.rebuildMidiMap();
    return copied;
}

// The anchor is leaving (removed or deleted): member 1 takes over its slices and the group's
// SAMPLE settings, so the group sounds the same.
void IntersectProcessor::handOverGroupSlices (SampleGroup& group, int fromIndex)
{
    if (fromIndex != 0 || group.numMembers < 2)
        return;

    const int oldAnchor = group.memberIds[0];
    const int newAnchor = group.memberIds[1];
    for (int i = 0; i < sliceManager.getNumSlices(); ++i)
    {
        auto& s = sliceManager.getSlice (i);
        if (s.sampleId == oldAnchor)
        {
            s.sampleId = newAnchor;
            syncSliceAbsoluteToCurrentSession (s);
        }
    }
    sampleParams.set (newAnchor, sampleParams.get (oldAnchor));
    if (selectedSessionSampleId.load (std::memory_order_relaxed) == oldAnchor)
        selectedSessionSampleId.store (newAnchor, std::memory_order_relaxed);
}

// Takes member `memberIndex` out of group `groupIndex` (after any hand-over). Slice member masks
// count members by order, so they shift down; a slice left with nobody follows the group again.
// A group of one ends, and that sample keeps the slices as an ordinary sample.
void IntersectProcessor::dropGroupMember (int groupIndex, int memberIndex)
{
    auto& group = sampleGroups.groups[(size_t) groupIndex];
    const int sliceOwner = (memberIndex == 0 && group.numMembers > 1) ? group.memberIds[1] : group.anchorId();
    group.removeMemberAt (memberIndex);

    const bool ends = group.numMembers < 2;
    for (int i = 0; i < sliceManager.getNumSlices(); ++i)
    {
        auto& s = sliceManager.getSlice (i);
        if (s.sampleId != sliceOwner || (s.lockMask & kLockMembers) == 0)
            continue;

        s.memberMask = SampleGroups::maskAfterRemoval (s.memberMask, memberIndex) & group.allMembersMask();
        if (ends || s.memberMask == 0)
        {
            s.lockMask &= ~kLockMembers;
            s.memberMask = 0;
        }
    }

    if (ends)
        sampleGroups.removeGroupAt (groupIndex);
}

void IntersectProcessor::removeGroupMember (int sampleId)
{
    const int groupIndex = sampleGroups.indexOfGroupWith (sampleId);
    if (groupIndex < 0)
        return;

    auto& group = sampleGroups.groups[(size_t) groupIndex];
    const int memberIndex = group.indexOf (sampleId);
    handOverGroupSlices (group, memberIndex);

    // The leaving sample gets its own copy of the group's slices, on new keys, if they fit.
    const int groupAnchor = memberIndex == 0 ? group.memberIds[1] : group.anchorId();
    int groupSlices = 0;
    for (int i = 0; i < sliceManager.getNumSlices(); ++i)
        if (sliceManager.getSlice (i).sampleId == groupAnchor)
            ++groupSlices;

    if (groupSlices <= SliceManager::kMaxSlices - sliceManager.getNumSlices())
        copySlicesToSample (groupAnchor, sampleId);
    else
        setUiStatusMessage (statusText ("No room for a copy of the group's slices (128 at most): the sample left without them"), true);

    dropGroupMember (groupIndex, memberIndex);
}

void IntersectProcessor::ungroup (int groupMemberId)
{
    const int groupIndex = sampleGroups.indexOfGroupWith (groupMemberId);
    if (groupIndex < 0)
        return;

    const auto group = sampleGroups.groups[(size_t) groupIndex];
    const int anchorId = group.anchorId();
    int groupSlices = 0;
    for (int i = 0; i < sliceManager.getNumSlices(); ++i)
        if (sliceManager.getSlice (i).sampleId == anchorId)
            ++groupSlices;

    // Every member keeps a copy of the slices if they all fit; otherwise only the anchor keeps them.
    const int needed = groupSlices * (group.numMembers - 1);
    if (needed <= SliceManager::kMaxSlices - sliceManager.getNumSlices())
    {
        for (int m = 1; m < group.numMembers; ++m)
            copySlicesToSample (anchorId, group.memberIds[(size_t) m]);
    }
    else
    {
        setUiStatusMessage (statusText ("No room for every sample to keep a copy of the slices (128 at most): the first one keeps them"), true);
    }

    for (int i = 0; i < sliceManager.getNumSlices(); ++i)
    {
        auto& s = sliceManager.getSlice (i);
        if (s.sampleId == anchorId)
        {
            s.lockMask &= ~kLockMembers;
            s.memberMask = 0;
        }
    }
    sampleGroups.removeGroupAt (groupIndex);
}

void IntersectProcessor::prepareGroupsForSampleRemoval (int sampleId)
{
    const int groupIndex = sampleGroups.indexOfGroupWith (sampleId);
    if (groupIndex < 0)
        return;

    auto& group = sampleGroups.groups[(size_t) groupIndex];
    const int memberIndex = group.indexOf (sampleId);
    handOverGroupSlices (group, memberIndex);
    dropGroupMember (groupIndex, memberIndex);
}

void IntersectProcessor::pruneGroupsToSession()
{
    const auto& samples = sampleData.getSessionSamples();
    sampleGroups.prune ([&samples] (int sampleId)
    {
        for (const auto& sample : samples)
            if (sample.sampleId == sampleId)
                return true;
        return false;
    });
}

// A group slice starts one voice per distinct output among the members it plays; each voice mixes
// its members as it reads (VoicePool's member taps). Allocation-free.
void IntersectProcessor::startGroupSliceVoices (const SampleGroup& group, int sliceIdx, VoiceStartParams p)
{
    const auto* anchor = findActiveSessionSample (group.anchorId());
    if (anchor == nullptr)
        return;

    const auto& s = sliceManager.getSlice (sliceIdx);
    const uint32_t played = SampleGroups::membersPlayed (group, (s.lockMask & kLockMembers) != 0, s.memberMask);
    const int sliceOutput = (int) sliceManager.resolveParam (sliceIdx, kLockOutputBus, (float) s.outputBus, 0.0f);

    SampleGroups::planVoices (group, played, sliceOutput,
                              [this] (int sampleId, int& startFrame, int& numFrames)
                              {
                                  const auto* member = findActiveSessionSample (sampleId);
                                  if (member == nullptr || member->isPlaceholder())
                                      return false;
                                  startFrame = member->startFrame;
                                  numFrames = member->numFrames;
                                  return true;
                              },
                              groupVoicePlan);

    p.groupAnchorStart = anchor->startFrame;
    for (int v = 0; v < groupVoicePlan.numVoices; ++v)
    {
        const auto& planned = groupVoicePlan.voices[(size_t) v];
        p.numMemberTaps = planned.numTaps;
        for (int t = 0; t < planned.numTaps; ++t)
            p.memberTaps[(size_t) t] = planned.taps[(size_t) t];
        p.outputBusOverride = planned.outputBus;

        const int voiceIdx = voicePool.allocate();
        voicePool.startVoice (voiceIdx, p, sliceManager, sampleData);
    }
}
