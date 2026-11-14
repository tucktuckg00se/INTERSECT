#include "GroupUi.h"
#include "IntersectLookAndFeel.h"

juce::Colour GroupUi::memberColour (int index)
{
    return getTheme().slicePalette[(size_t) (juce::jmax (0, index) % 16)].brighter (0.35f);
}

const GroupUi::Snapshot::UiSessionSample* GroupUi::findSample (const Snapshot& ui, int sampleId)
{
    for (int i = 0; i < ui.numSessionSamples; ++i)
        if (ui.sessionSamples[(size_t) i].sampleId == sampleId)
            return &ui.sessionSamples[(size_t) i];
    return nullptr;
}

juce::String GroupUi::memberName (const Snapshot& ui, int sampleId)
{
    const auto* sample = findSample (ui, sampleId);
    if (sample == nullptr)
        return {};
    if (! sample->stemName.isEmpty())
        return sample->stemName.toString().toUpperCase();
    return sample->fileName.toString();
}

juce::String GroupUi::groupName (const Snapshot& ui, const SampleGroup& group)
{
    int source = -1;
    bool allStems = group.numMembers > 0;
    for (int m = 0; m < group.numMembers && allStems; ++m)
    {
        const auto* sample = findSample (ui, group.memberIds[(size_t) m]);
        allStems = sample != nullptr && sample->stemParentId >= 0
                   && (source < 0 || source == sample->stemParentId);
        if (allStems)
            source = sample->stemParentId;
    }

    if (allStems)
        if (const auto* parent = findSample (ui, source))
            return juce::String (juce::CharPointer_UTF8 ("STEMS \xc2\xb7 ")) + parent->fileName.toString();

    const auto* anchor = findSample (ui, group.anchorId());
    return juce::String (juce::CharPointer_UTF8 ("GROUP \xc2\xb7 ")) + (anchor != nullptr ? anchor->fileName.toString() : juce::String());
}

uint32_t GroupUi::membersPlayed (const SampleGroup& group, const Slice& slice)
{
    return SampleGroups::membersPlayed (group, sliceHasOwnMembers (slice), slice.memberMask);
}

bool GroupUi::sliceHasOwnMembers (const Slice& slice)
{
    return (slice.lockMask & kLockMembers) != 0;
}

std::vector<GroupUi::Candidate> GroupUi::joinCandidates (const Snapshot& ui, int anchorId, double sampleRate)
{
    std::vector<Candidate> out;
    const auto* group = ui.sampleGroups.groupWith (anchorId);
    const int realAnchor = group != nullptr ? group->anchorId() : anchorId;
    const auto* anchor = findSample (ui, realAnchor);
    if (anchor == nullptr)
        return out;

    for (int i = 0; i < ui.numSessionSamples; ++i)
    {
        const auto& sample = ui.sessionSamples[(size_t) i];
        if (sample.sampleId == realAnchor || (group != nullptr && group->indexOf (sample.sampleId) >= 0))
            continue;

        Candidate candidate;
        candidate.sampleId = sample.sampleId;
        candidate.name = sample.fileName.toString();
        for (int s = 0; s < ui.numSlices; ++s)
            if (ui.slices[(size_t) s].sampleId == sample.sampleId)
                ++candidate.numSlices;
        candidate.meta = formatLength (sample.numFrames, sampleRate);
        if (candidate.numSlices > 0)
            candidate.meta += juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (candidate.numSlices)
                              + (candidate.numSlices == 1 ? " slice" : " slices");

        if (ui.sampleGroups.groupWith (sample.sampleId) != nullptr)
            candidate.reason = "in another group";
        else if (! SampleGroups::lengthsMatch (anchor->numFrames, sample.numFrames, sampleRate))
            candidate.reason = "different length";
        candidate.canJoin = candidate.reason.isEmpty();
        out.push_back (std::move (candidate));
    }
    return out;
}

void GroupUi::push (IntersectProcessor& processor, IntersectProcessor::CommandType type, int sampleId,
                    const std::vector<int>& ids, int value, float level)
{
    IntersectProcessor::Command cmd;
    cmd.type = type;
    cmd.intParam1 = sampleId;
    cmd.intParam2 = value;
    cmd.floatParam1 = level;
    for (int id : ids)
        if (cmd.numPositions < (int) cmd.positions.size())
            cmd.positions[(size_t) cmd.numPositions++] = id;
    processor.pushCommand (cmd);
}

void GroupUi::setSliceMembers (IntersectProcessor& processor, int sliceIdx, uint32_t mask)
{
    IntersectProcessor::Command cmd;
    cmd.type = IntersectProcessor::CmdSetSliceParam;
    cmd.sliceIdx = sliceIdx;
    cmd.intParam1 = IntersectProcessor::FieldMemberMask;
    cmd.floatParam1 = (float) (mask & 0xffffu);
    processor.pushCommand (cmd);
}

void GroupUi::followGroupMix (IntersectProcessor& processor, int sliceIdx)
{
    const auto& ui = processor.getUiSliceSnapshot();
    if (sliceIdx < 0 || sliceIdx >= ui.numSlices || ! sliceHasOwnMembers (ui.slices[(size_t) sliceIdx]))
        return;

    IntersectProcessor::Command cmd;
    cmd.type = IntersectProcessor::CmdToggleLock;
    cmd.sliceIdx = sliceIdx;
    cmd.lockBitParam = kLockMembers;
    processor.pushCommand (cmd);
}

juce::String GroupUi::formatDb (float db)
{
    if (db <= -100.0f)
        return juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e dB"));
    if (std::abs (db) < 0.05f)
        return "0.0 dB";
    return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
}

juce::String GroupUi::formatLength (int numFrames, double sampleRate)
{
    const double seconds = sampleRate > 0.0 ? numFrames / sampleRate : 0.0;
    const int minutes = (int) (seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    return juce::String (minutes) + ":" + (rest < 10.0 ? "0" : "") + juce::String (rest, 1);
}
