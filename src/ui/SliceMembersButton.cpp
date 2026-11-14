#include "SliceMembersButton.h"
#include "GroupCallouts.h"
#include "GroupUi.h"
#include "IntersectLookAndFeel.h"

namespace
{
constexpr int kLabelW = 46;
constexpr int kBadge = 11;
constexpr int kMaxBadges = 7;
constexpr int kSuffixW = 50;
const juce::Colour kOwnColour { 0xfff0a030 };
}

SliceMembersButton::SliceMembersButton (IntersectProcessor& p) : processor (p)
{
    setTooltip ("Which members this slice plays. Click to mute members in this slice only.");
}

bool SliceMembersButton::setSlice (int newSliceIdx)
{
    const auto& ui = processor.getUiSliceSnapshot();
    badges.clear();
    hiddenCount = 0;
    sliceIdx = newSliceIdx;
    if (sliceIdx < 0 || sliceIdx >= ui.numSlices)
        return false;

    const auto& slice = ui.slices[(size_t) sliceIdx];
    const auto* group = ui.sampleGroups.groupAnchoredBy (slice.sampleId);
    if (group == nullptr)
        return false;

    own = GroupUi::sliceHasOwnMembers (slice);
    const uint32_t played = GroupUi::membersPlayed (*group, slice);
    for (int m = 0; m < group->numMembers; ++m)
    {
        if ((played & (1u << m)) == 0)
            continue;
        if ((int) badges.size() < kMaxBadges)
            badges.push_back ({ m, GroupUi::memberNumber (m) });
        else
            ++hiddenCount;
    }
    repaint();
    return true;
}

int SliceMembersButton::getPreferredWidth() const
{
    return kLabelW + 6 + (int) juce::jmax ((size_t) 1, badges.size()) * (kBadge + 2) + kSuffixW + (hiddenCount > 0 ? 18 : 0);
}

void SliceMembersButton::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.setColour (own ? kOwnColour : getTheme().text0);
    g.setFont (IntersectLookAndFeel::makeFont (8.0f, true));
    g.drawText ("MEMBERS", area.removeFromLeft (kLabelW), juce::Justification::centredLeft, false);

    auto box = area.reduced (0, 3);
    g.setColour (own ? kOwnColour.withAlpha (0.18f) : getTheme().surface3);
    g.fillRoundedRectangle (box.toFloat(), 3.0f);
    g.setColour (own ? kOwnColour.withAlpha (0.85f) : getTheme().surface5);
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 3.0f, 1.0f);

    auto inner = box.reduced (3, 0);
    for (const auto& [memberIndex, number] : badges)
    {
        const auto badge = inner.removeFromLeft (kBadge + 2).withSizeKeepingCentre (kBadge, kBadge).toFloat();
        g.setColour (GroupUi::memberColour (memberIndex));
        g.fillEllipse (badge);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (IntersectLookAndFeel::makeFont (7.0f, true));
        g.drawText (juce::String (number), badge, juce::Justification::centred, false);
    }
    g.setColour (own ? kOwnColour : getTheme().text1);
    g.setFont (IntersectLookAndFeel::makeFont (7.8f, true));
    if (hiddenCount > 0)
        g.drawText ("+" + juce::String (hiddenCount), inner.removeFromLeft (18), juce::Justification::centredLeft, false);
    g.drawText (juce::String (own ? "own" : "group mix") + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")),
                inner.withTrimmedLeft (3), juce::Justification::centredLeft, false);
}

void SliceMembersButton::mouseDown (const juce::MouseEvent&)
{
    if (sliceIdx >= 0)
        GroupCallouts::showSliceMembers (*this, getLocalBounds(), processor, sliceIdx);
}
