#include "TimelineMap.h"

TimelineMap TimelineMap::fromProcessor (const IntersectProcessor& processor)
{
    const auto snapshot = processor.sampleData.getSnapshot();
    return TimelineMap (processor.getUiSliceSnapshot(), snapshot != nullptr ? snapshot->buffer.getNumSamples() : 0);
}

void TimelineMap::build (const IntersectProcessor::UiSliceSnapshot& ui, int numKitFrames)
{
    segments.clear();
    members.clear();
    kitFrames = juce::jmax (0, numKitFrames);
    totalFrames = 0;
    hidden = false;

    const int numSamples = juce::jmin (ui.numSessionSamples, (int) ui.sessionSamples.size());
    if (numSamples <= 0)
    {
        // A single file from before multi-sample kits, or nothing: the buffer as is.
        if (kitFrames > 0)
            segments.push_back ({ -1, 0, 0, kitFrames, false });
        totalFrames = kitFrames;
        return;
    }

    segments.reserve ((size_t) numSamples);
    members.reserve ((size_t) numSamples);

    // Visible slots first, in kit order: every sample that isn't a hidden group member.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto& sample = ui.sessionSamples[(size_t) i];
        if (ui.sampleGroups.isHiddenMember (sample.sampleId))
        {
            hidden = true;
            continue;
        }
        Segment segment;
        segment.sampleId = sample.sampleId;
        segment.kitStart = sample.startSample;
        segment.timelineStart = totalFrames;
        segment.numFrames = juce::jmax (0, sample.numFrames);
        segment.isGroup = ui.sampleGroups.groupAnchoredBy (sample.sampleId) != nullptr;
        totalFrames += segment.numFrames;
        segments.push_back (segment);
    }

    // Then which slot shows each sample: its own, or its group's.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto& sample = ui.sessionSamples[(size_t) i];
        const auto* group = ui.sampleGroups.groupWith (sample.sampleId);
        const int shownId = group != nullptr ? group->anchorId() : sample.sampleId;
        int index = -1;
        for (int s = 0; s < (int) segments.size() && index < 0; ++s)
            if (segments[(size_t) s].sampleId == shownId)
                index = s;
        members.push_back ({ sample.sampleId, sample.startSample, sample.numFrames, index });
    }
}

int TimelineMap::kitToTimeline (int kitFrame) const noexcept
{
    if (members.empty())
        return kitFrame;

    for (const auto& member : members)
    {
        if (kitFrame < member.kitStart || kitFrame >= member.kitStart + member.numFrames || member.segment < 0)
            continue;
        const auto& segment = segments[(size_t) member.segment];
        const int offset = juce::jlimit (0, juce::jmax (0, segment.numFrames), kitFrame - member.kitStart);
        return segment.timelineStart + offset;
    }

    // Past the last sample (an end position): the end of the timeline.
    return kitFrame >= kitFrames ? totalFrames : juce::jlimit (0, totalFrames, kitFrame);
}

int TimelineMap::timelineToKit (int timelineFrame) const noexcept
{
    if (members.empty())
        return timelineFrame;
    if (const auto* segment = segmentAt (timelineFrame))
        return segment->kitStart + (timelineFrame - segment->timelineStart);
    if (segments.empty())
        return timelineFrame;
    const auto& last = segments.back();
    return timelineFrame <= 0 ? segments.front().kitStart : last.kitStart + last.numFrames;
}

const TimelineMap::Segment* TimelineMap::segmentAt (int timelineFrame) const noexcept
{
    for (const auto& segment : segments)
        if (timelineFrame >= segment.timelineStart && timelineFrame < segment.timelineStart + segment.numFrames)
            return &segment;
    return nullptr;
}

const TimelineMap::Segment* TimelineMap::segmentFor (int sampleId) const noexcept
{
    for (const auto& member : members)
        if (member.sampleId == sampleId && member.segment >= 0)
            return &segments[(size_t) member.segment];
    for (const auto& segment : segments)
        if (segment.sampleId == sampleId)
            return &segment;
    return nullptr;
}

TimelineMap::View TimelineMap::viewFor (float zoom, float scroll, int width) const noexcept
{
    View view;
    view.width = juce::jmax (1, width);
    const int numFrames = juce::jmax (1, totalFrames);
    const float z = juce::jmax (1.0f, zoom);
    view.length = juce::jlimit (1, numFrames, (int) ((float) numFrames / z));
    const int maxStart = numFrames - view.length;
    view.start = juce::jlimit (0, juce::jmax (0, maxStart), (int) (juce::jlimit (0.0f, 1.0f, scroll) * (float) maxStart));
    return view;
}
