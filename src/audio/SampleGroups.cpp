#include "SampleGroups.h"

#include <cstdlib>

bool SampleGroup::addMember (int sampleId) noexcept
{
    if (numMembers >= kMaxMembers || indexOf (sampleId) >= 0)
        return false;

    const auto i = (size_t) numMembers++;
    memberIds[i] = sampleId;
    gainDb[i] = 0.0f;
    outputBus[i] = -1;
    mutedMask &= ~(1u << i);
    return true;
}

void SampleGroup::removeMemberAt (int index) noexcept
{
    if (index < 0 || index >= numMembers)
        return;

    for (int i = index; i + 1 < numMembers; ++i)
    {
        memberIds[(size_t) i] = memberIds[(size_t) i + 1];
        gainDb[(size_t) i] = gainDb[(size_t) i + 1];
        outputBus[(size_t) i] = outputBus[(size_t) i + 1];
    }
    --numMembers;
    mutedMask = SampleGroups::maskAfterRemoval (mutedMask, index) & allMembersMask();
}

int SampleGroupTable::indexOfGroupWith (int sampleId) const noexcept
{
    for (int g = 0; g < count; ++g)
        if (groups[(size_t) g].indexOf (sampleId) >= 0)
            return g;
    return -1;
}

int SampleGroupTable::addGroup (int anchorId) noexcept
{
    if (count >= kMaxGroups || indexOfGroupWith (anchorId) >= 0)
        return -1;

    auto& group = groups[(size_t) count];
    group = SampleGroup {};
    group.addMember (anchorId);
    return count++;
}

void SampleGroupTable::removeGroupAt (int index) noexcept
{
    if (index < 0 || index >= count)
        return;

    for (int g = index; g + 1 < count; ++g)
        groups[(size_t) g] = groups[(size_t) g + 1];
    --count;
    groups[(size_t) count] = SampleGroup {};
}

bool SampleGroups::lengthsMatch (int anchorFrames, int memberFrames, double sampleRate) noexcept
{
    if (anchorFrames <= 0 || memberFrames <= 0)
        return false;

    const int tolerance = sampleRate > 0.0 ? (int) (sampleRate * 0.010) : 0;
    return std::abs (anchorFrames - memberFrames) <= tolerance;
}

uint32_t SampleGroups::maskAfterRemoval (uint32_t mask, int removedIndex) noexcept
{
    if (removedIndex < 0 || removedIndex >= 32)
        return mask;

    const uint32_t below = removedIndex == 0 ? 0u : (mask & ((1u << removedIndex) - 1u));
    const uint32_t above = removedIndex >= 31 ? 0u : (mask >> (removedIndex + 1)) << removedIndex;
    return below | above;
}
