#pragma once

#include "AudioAnalysis.h"
#include "SampleData.h"
#include "SampleGroups.h"
#include <juce_audio_basics/juce_audio_basics.h>

/** Reads a group's mix (the members a slice following the group plays, at their levels) straight
    from the kit buffer, so zero-crossing snap, transient detection and the AUDIO export hear what
    the group plays without keeping a mixed copy. Header-only and allocation-free; any thread that
    holds the buffer can use it (the audio thread with the active sample, others with a snapshot). */
struct GroupMix
{
    const juce::AudioBuffer<float>* buffer = nullptr;
    int anchorStart = 0;    // the anchor's first frame: group positions are relative to it
    int anchorFrames = 0;
    int numTaps = 0;
    std::array<SampleGroups::MemberTap, SampleGroup::kMaxMembers> taps {};

    bool isActive() const noexcept { return buffer != nullptr && numTaps > 0; }

    /** True when `frame` (absolute) lies in this group's slot. */
    bool covers (int frame) const noexcept
    {
        return isActive() && frame >= anchorStart && frame < anchorStart + anchorFrames;
    }

    float sample (int channel, int frame) const noexcept
    {
        if (buffer == nullptr || channel < 0 || channel >= buffer->getNumChannels())
            return 0.0f;

        const int rel = frame - anchorStart;
        const auto* data = buffer->getReadPointer (channel);
        float sum = 0.0f;
        for (int t = 0; t < numTaps; ++t)
        {
            const auto& tap = taps[(size_t) t];
            const int at = tap.startFrame + rel;
            if (rel >= 0 && rel < tap.numFrames && at >= 0 && at < buffer->getNumSamples())
                sum += tap.gain * data[at];
        }
        return sum;
    }

    float mono (int frame) const noexcept
    {
        const int right = buffer != nullptr && buffer->getNumChannels() > 1 ? 1 : 0;
        return (sample (0, frame) + sample (right, frame)) * 0.5f;
    }

    /** The mix for the group whose slot contains `frame`, or an inactive one (read the kit as is). */
    static GroupMix at (const SampleGroupTable& groups,
                        const std::vector<SampleData::SessionSample>& sessionSamples,
                        const juce::AudioBuffer<float>& buffer, int frame) noexcept
    {
        GroupMix mix;
        mix.buffer = &buffer;   // inactive (no taps) outside groups: reads fall back to the kit as is
        for (const auto& sample : sessionSamples)
        {
            if (frame < sample.startFrame || frame >= sample.startFrame + sample.numFrames)
                continue;
            if (const auto* group = groups.groupAnchoredBy (sample.sampleId))
                mix = forGroup (*group, sessionSamples, buffer);
            break;
        }
        return mix;
    }

    static GroupMix forGroup (const SampleGroup& group,
                              const std::vector<SampleData::SessionSample>& sessionSamples,
                              const juce::AudioBuffer<float>& buffer) noexcept
    {
        GroupMix mix;
        const auto find = [&sessionSamples] (int id) -> const SampleData::SessionSample*
        {
            for (const auto& s : sessionSamples)
                if (s.sampleId == id)
                    return &s;
            return nullptr;
        };

        const auto* anchor = find (group.anchorId());
        if (anchor == nullptr)
            return mix;

        mix.buffer = &buffer;
        mix.anchorStart = anchor->startFrame;
        mix.anchorFrames = anchor->numFrames;
        const uint32_t played = group.unmutedMask();
        for (int m = 0; m < group.numMembers; ++m)
        {
            if ((played & (1u << m)) == 0)
                continue;
            const auto* member = find (group.memberIds[(size_t) m]);
            if (member == nullptr || member->isPlaceholder())
                continue;
            mix.taps[(size_t) mix.numTaps++] = { member->startFrame, member->numFrames,
                                                 SampleGroups::gainFromDb (group.gainDb[(size_t) m]) };
        }
        return mix;
    }

    /** AudioAnalysis::findNearestZeroCrossing, hearing the group's mix inside its slot. */
    int findNearestZeroCrossing (int pos, int searchRange = 512) const noexcept
    {
        if (! covers (pos))
            return buffer != nullptr ? AudioAnalysis::findNearestZeroCrossing (*buffer, pos, searchRange) : pos;

        int bestPos = pos;
        int bestDist = searchRange + 1;
        const int lo = std::max (anchorStart + 1, pos - searchRange);
        const int hi = std::min (anchorStart + anchorFrames - 1, pos + searchRange);
        float previous = mono (lo - 1);
        for (int i = lo; i <= hi; ++i)
        {
            const float current = mono (i);
            if ((previous >= 0.0f && current < 0.0f) || (previous < 0.0f && current >= 0.0f))
            {
                const int dist = std::abs (i - pos);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestPos = i;
                }
            }
            previous = current;
        }
        return bestPos;
    }

    /** The mix of frames [start, end) as a stereo buffer starting at `start` (message thread:
        allocates), for analysis that needs a contiguous buffer such as the transient ODF. */
    juce::AudioBuffer<float> render (int start, int end) const
    {
        const int numFrames = juce::jmax (0, end - start);
        juce::AudioBuffer<float> out (2, numFrames);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numFrames; ++i)
                out.setSample (ch, i, sample (ch, start + i));
        return out;
    }
};
