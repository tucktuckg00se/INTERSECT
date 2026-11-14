#pragma once

#include "../PluginProcessor.h"
#include <initializer_list>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

/** What every group control shows the same way: member numbers and colours, names, what a slice
    plays, and the commands that change a group. Message thread. */
namespace GroupUi
{
    using Snapshot = IntersectProcessor::UiSliceSnapshot;

    /** Member `index` (lane order, 0 = top) in the theme's slice colours. */
    juce::Colour memberColour (int index);

    /** The number the UI shows for member `index`: lane order, 1-based. Labels only. */
    inline int memberNumber (int index) { return index + 1; }

    const Snapshot::UiSessionSample* findSample (const Snapshot& ui, int sampleId);

    /** A stem's stem name ("DRUMS"), any other sample's file name. */
    juce::String memberName (const Snapshot& ui, int sampleId);

    /** "STEMS · song.wav" when every member is a stem of one source, else "GROUP · <first file>". */
    juce::String groupName (const Snapshot& ui, const SampleGroup& group);

    /** The members a slice plays (bit i = member i). */
    uint32_t membersPlayed (const SampleGroup& group, const Slice& slice);

    bool sliceHasOwnMembers (const Slice& slice);

    /** Same-length samples that could join `group` (or form one with `anchorId` when group is null),
        with why the others can't. */
    struct Candidate
    {
        int sampleId = -1;
        juce::String name;
        juce::String meta;       // "0:03.0 · 8 slices"
        int numSlices = 0;
        bool canJoin = false;
        juce::String reason;     // why not, when !canJoin
    };
    std::vector<Candidate> joinCandidates (const Snapshot& ui, int anchorId, double sampleRate);

    void push (IntersectProcessor& processor, IntersectProcessor::CommandType type, int sampleId,
               const std::vector<int>& ids = {}, int value = 0, float level = 0.0f);

    /** Gives a slice its own members (sets kLockMembers through FieldMemberMask). */
    void setSliceMembers (IntersectProcessor& processor, int sliceIdx, uint32_t mask);

    /** Back to the group mix (clears kLockMembers). */
    void followGroupMix (IntersectProcessor& processor, int sliceIdx);

    juce::String formatDb (float db);
    juce::String formatLength (int numFrames, double sampleRate);
}
