#pragma once

#include <array>
#include <cmath>
#include <cstdint>

/** One sample group: same-length session samples that share one set of slices.

    The first member, the anchor, owns the group's slices (slice.sampleId == anchor) and its
    SAMPLE-tab settings apply to the whole group. A group slice plays its members together, each at
    its own level and output: the slice's own member mask when it has kLockMembers, otherwise the
    group's unmuted members. Member order is lane order (member 1 is the top lane); the UI numbers
    members by it. Fixed size and allocation-free. */
struct SampleGroup
{
    static constexpr int kMaxMembers = 16;

    int numMembers = 0;
    std::array<int, kMaxMembers> memberIds {};
    std::array<float, kMaxMembers> gainDb {};
    std::array<int, kMaxMembers> outputBus {};   // -1: the slice's OUT
    uint32_t mutedMask = 0;                      // bit i: member i is muted for the whole group

    int anchorId() const noexcept { return numMembers > 0 ? memberIds[0] : -1; }

    int indexOf (int sampleId) const noexcept
    {
        for (int i = 0; i < numMembers; ++i)
            if (memberIds[(size_t) i] == sampleId)
                return i;
        return -1;
    }

    uint32_t allMembersMask() const noexcept
    {
        return numMembers <= 0 ? 0u : (numMembers >= 32 ? 0xffffffffu : ((1u << numMembers) - 1u));
    }

    uint32_t unmutedMask() const noexcept { return allMembersMask() & ~mutedMask; }

    /** Appends a member at 0 dB on the slice's output. False when full or already a member. */
    bool addMember (int sampleId) noexcept;

    /** Removes member `index`, keeping the others' order, levels, outputs and mutes. */
    void removeMemberAt (int index) noexcept;
};

/** Every group in the kit, keyed by member sample ids. The audio thread owns it (like
    SampleParamTable); undo snapshots, saves and the UI snapshot copy it. */
struct SampleGroupTable
{
    static constexpr int kMaxGroups = 32;

    std::array<SampleGroup, kMaxGroups> groups {};
    int count = 0;

    /** Index of the group that has `sampleId` as a member, or -1. */
    int indexOfGroupWith (int sampleId) const noexcept;

    const SampleGroup* groupWith (int sampleId) const noexcept
    {
        const int index = indexOfGroupWith (sampleId);
        return index >= 0 ? &groups[(size_t) index] : nullptr;
    }

    SampleGroup* groupWith (int sampleId) noexcept
    {
        const int index = indexOfGroupWith (sampleId);
        return index >= 0 ? &groups[(size_t) index] : nullptr;
    }

    /** The group `anchorId` anchors (owns the slices of), or nullptr. */
    const SampleGroup* groupAnchoredBy (int anchorId) const noexcept
    {
        const auto* group = groupWith (anchorId);
        return group != nullptr && group->anchorId() == anchorId ? group : nullptr;
    }

    /** In a group but not its anchor: the sample has no slot or slices of its own while grouped. */
    bool isHiddenMember (int sampleId) const noexcept
    {
        const auto* group = groupWith (sampleId);
        return group != nullptr && group->anchorId() != sampleId;
    }

    /** Adds a group holding only `anchorId`; returns its index, or -1 when the table is full. */
    int addGroup (int anchorId) noexcept;

    void removeGroupAt (int index) noexcept;

    /** Keeps only members that `exists(sampleId)` and groups that still have two of them. Members
        must be unique across groups; later duplicates are dropped. For loaded or restored state. */
    template <typename Exists>
    void prune (Exists&& exists) noexcept
    {
        for (int g = count - 1; g >= 0; --g)
        {
            auto& group = groups[(size_t) g];
            for (int m = group.numMembers - 1; m >= 0; --m)
            {
                const int id = group.memberIds[(size_t) m];
                bool duplicate = false;
                for (int other = 0; other < g && ! duplicate; ++other)
                    duplicate = groups[(size_t) other].indexOf (id) >= 0;
                for (int earlier = 0; earlier < m && ! duplicate; ++earlier)
                    duplicate = group.memberIds[(size_t) earlier] == id;
                if (duplicate || ! exists (id))
                    group.removeMemberAt (m);
            }
            if (group.numMembers < 2)
                removeGroupAt (g);
        }
    }
};

namespace SampleGroups
{
    /** True when a member's length fits the anchor's: within 10 ms. Stems can come back a frame
        longer than their source (the stem job resamples to the model's rate and back). */
    bool lengthsMatch (int anchorFrames, int memberFrames, double sampleRate) noexcept;

    /** A slice's member mask after member `removedIndex` left the group: higher members move down. */
    uint32_t maskAfterRemoval (uint32_t mask, int removedIndex) noexcept;

    /** The members a group slice plays: its own mask when it overrides, else the unmuted members. */
    inline uint32_t membersPlayed (const SampleGroup& group, bool sliceOverrides, uint32_t sliceMask) noexcept
    {
        return (sliceOverrides ? sliceMask : group.unmutedMask()) & group.allMembersMask();
    }

    inline float gainFromDb (float db) noexcept
    {
        return db <= -100.0f ? 0.0f : std::pow (10.0f, db / 20.0f);
    }

    /** One member in a voice: frames [startFrame, startFrame + numFrames) of the kit buffer. */
    struct MemberTap
    {
        int startFrame = 0;
        int numFrames = 0;
        float gain = 0.0f;
    };

    /** The voices a group slice starts: one per distinct output, each mixing its members. */
    struct VoicePlan
    {
        struct Voice
        {
            int outputBus = 0;
            int numTaps = 0;
            std::array<MemberTap, SampleGroup::kMaxMembers> taps {};
        };

        int numVoices = 0;
        std::array<Voice, SampleGroup::kMaxMembers> voices {};
    };

    /** Groups the `played` members by output. `memberFrames (sampleId, startFrame&, numFrames&)`
        returns false for a member without audio (a missing file), which then contributes nothing.
        Members on the slice's output (-1) go to `sliceOutputBus`. Allocation-free. */
    template <typename MemberFrames>
    void planVoices (const SampleGroup& group, uint32_t played, int sliceOutputBus,
                     MemberFrames&& memberFrames, VoicePlan& plan) noexcept
    {
        plan.numVoices = 0;
        for (int i = 0; i < group.numMembers; ++i)
        {
            if ((played & (1u << i)) == 0)
                continue;

            int startFrame = 0;
            int numFrames = 0;
            if (! memberFrames (group.memberIds[(size_t) i], startFrame, numFrames) || numFrames <= 0)
                continue;

            const int bus = group.outputBus[(size_t) i] >= 0 ? group.outputBus[(size_t) i] : sliceOutputBus;
            int v = 0;
            while (v < plan.numVoices && plan.voices[(size_t) v].outputBus != bus)
                ++v;
            if (v == plan.numVoices)
            {
                plan.voices[(size_t) v].outputBus = bus;
                plan.voices[(size_t) v].numTaps = 0;
                ++plan.numVoices;
            }

            auto& voice = plan.voices[(size_t) v];
            voice.taps[(size_t) voice.numTaps++] = { startFrame, numFrames, gainFromDb (group.gainDb[(size_t) i]) };
        }
    }
}
