#include "GroupLanes.h"
#include "GroupCallouts.h"
#include "GroupUi.h"
#include "IntersectLookAndFeel.h"
#include "UIHelpers.h"

namespace
{
// Logical pixels (the editor scales them).
constexpr int kHeaderH = 13;
constexpr int kBadge = 11;
constexpr int kMuteW = 14;
constexpr int kNameW = 54;
constexpr int kLevelW = 40;
constexpr int kDbW = 34;
constexpr int kIconW = 11;
constexpr int kGap = 3;
constexpr int kDenseBelow = 20;   // lanes shorter than this get a strip instead of a box
constexpr int kFullFrom = 40;     // lanes this tall show level and remove when open
constexpr int kSliceMInset = 7;   // past the slice edge's drag zone (6 px)
constexpr int kSliceMW = 14;
constexpr int kSliceMH = 11;

const juce::Colour kMutedBg { 0xff6b5a1e };
const juce::Colour kMutedText { 0xfff5d77a };

juce::Rectangle<int> slotBounds (juce::Rectangle<int> bounds, const TimelineMap& map, const TimelineMap::View& view,
                                 const TimelineMap::Segment& segment)
{
    const int x1 = juce::jlimit (bounds.getX(), bounds.getRight(), map.timelineToPixel (view, segment.timelineStart));
    const int x2 = juce::jlimit (bounds.getX(), bounds.getRight(), map.timelineToPixel (view, segment.timelineStart + segment.numFrames));
    return { x1, bounds.getY(), juce::jmax (0, x2 - x1), bounds.getHeight() };
}

void paintBadge (juce::Graphics& g, juce::Rectangle<int> area, int memberIndex)
{
    g.setColour (GroupUi::memberColour (memberIndex));
    g.fillEllipse (area.toFloat());
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (IntersectLookAndFeel::makeFont (7.2f, true));
    g.drawText (juce::String (GroupUi::memberNumber (memberIndex)), area, juce::Justification::centred, false);
}

void paintMute (juce::Graphics& g, juce::Rectangle<int> area, bool muted, float alpha = 1.0f)
{
    g.setColour ((muted ? kMutedBg : getTheme().surface4).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (area.toFloat(), 2.0f);
    g.setColour ((muted ? kMutedText : getTheme().text1).withMultipliedAlpha (alpha));
    g.setFont (IntersectLookAndFeel::makeFont (7.2f, true));
    g.drawText ("M", area, juce::Justification::centred, false);
}

void paintHatch (juce::Graphics& g, juce::Rectangle<int> area)
{
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRect (area);
    g.setColour (getTheme().surface5.withAlpha (0.55f));
    for (int x = area.getX() - area.getHeight(); x < area.getRight(); x += 6)
        g.drawLine ((float) x, (float) area.getBottom(), (float) (x + area.getHeight()), (float) area.getY(), 1.2f);
}

void paintChip (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, bool enabled)
{
    const float alpha = enabled ? 1.0f : 0.45f;
    g.setColour (getTheme().surface0.withAlpha (0.88f));
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    juce::Path outline;
    outline.addRoundedRectangle (area.toFloat().reduced (0.5f), 3.0f);
    juce::Path dashed;
    const float dashes[] = { 3.0f, 2.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (getTheme().text0.withMultipliedAlpha (alpha));
    g.fillPath (dashed);
    g.setColour (getTheme().text1.withMultipliedAlpha (alpha));
    g.setFont (IntersectLookAndFeel::makeFont (7.2f, true));
    g.drawText (text, area, juce::Justification::centred, false);
}
}

// --- GroupLanePainter ------------------------------------------------------------------------

juce::Rectangle<int> GroupLanePainter::laneBounds (juce::Rectangle<int> slot, int memberIndex, int numMembers)
{
    const int n = juce::jmax (1, numMembers);
    const int top = slot.getY() + slot.getHeight() * memberIndex / n;
    const int bottom = slot.getY() + slot.getHeight() * (memberIndex + 1) / n;
    return { slot.getX(), top, slot.getWidth(), bottom - top };
}

void GroupLanePainter::paintLanes (juce::Graphics& g, juce::Rectangle<int> bounds, const IntersectProcessor::UiSliceSnapshot& ui,
                                   const SampleData::DecodedSample& sample, const TimelineMap& map, const TimelineMap::View& view)
{
    for (const auto& segment : map.getSegments())
    {
        if (! segment.isGroup)
            continue;
        const auto* group = ui.sampleGroups.groupAnchoredBy (segment.sampleId);
        const auto slot = slotBounds (bounds, map, view, segment);
        if (group == nullptr || slot.isEmpty())
            continue;

        for (int m = 0; m < group->numMembers; ++m)
        {
            const int id = group->memberIds[(size_t) m];
            const auto lane = laneBounds (slot, m, group->numMembers);
            const auto* member = GroupUi::findSample (ui, id);
            if (member == nullptr)
                continue;

            if (m > 0)
            {
                g.setColour (getTheme().surface4.withAlpha (0.6f));
                g.drawHorizontalLine (lane.getY(), (float) slot.getX(), (float) slot.getRight());
            }
            if (member->isPlaceholder())
            {
                g.setColour (getTheme().color5.withAlpha (0.75f));
                g.setFont (IntersectLookAndFeel::makeFont (7.5f));
                g.drawText (member->fileName.toString() + " is missing", lane.withTrimmedLeft (40).reduced (4, 0),
                            juce::Justification::centredLeft, true);
                continue;
            }

            auto& cached = lanes[id];
            if (cached.viewStart != view.start || cached.viewLength != view.length || cached.width != view.width
                || cached.timelineStart != segment.timelineStart || cached.memberStart != member->startSample
                || cached.samplePtr != &sample)
            {
                const int segStart = segment.timelineStart;
                const int segFrames = juce::jmin (segment.numFrames, member->numFrames);
                const int memberStart = member->startSample;
                cached.cache.rebuild (sample.buffer, sample.peakMipmaps, view, [segStart, segFrames, memberStart] (int t)
                {
                    return (t >= segStart && t < segStart + segFrames) ? memberStart + (t - segStart) : -1;
                });
                cached.viewStart = view.start;
                cached.viewLength = view.length;
                cached.width = view.width;
                cached.timelineStart = segment.timelineStart;
                cached.memberStart = member->startSample;
                cached.samplePtr = &sample;
            }

            g.saveState();
            g.reduceClipRegion (lane);
            cached.cache.paint (g, { 0, lane.getY(), bounds.getRight(), lane.getHeight() },
                                GroupUi::memberColour (m).withAlpha (0.85f), view, UILayout::waveformVerticalScale);
            g.restoreState();
        }
    }
}

void GroupLanePainter::paintOverlays (juce::Graphics& g, juce::Rectangle<int> bounds, const IntersectProcessor::UiSliceSnapshot& ui,
                                      const TimelineMap& map, const TimelineMap::View& view) const
{
    for (const auto& segment : map.getSegments())
    {
        if (! segment.isGroup)
            continue;
        const auto* group = ui.sampleGroups.groupAnchoredBy (segment.sampleId);
        const auto slot = slotBounds (bounds, map, view, segment);
        if (group == nullptr || slot.isEmpty())
            continue;

        // Slices with their own members decide for themselves; everywhere else the group mix does.
        std::vector<std::pair<juce::Range<int>, uint32_t>> own;
        for (int i = 0; i < ui.numSlices; ++i)
        {
            const auto& s = ui.slices[(size_t) i];
            if (s.active && s.sampleId == segment.sampleId && GroupUi::sliceHasOwnMembers (s))
                own.push_back ({ { map.kitToPixel (view, s.startSample), map.kitToPixel (view, s.endSample) },
                                 GroupUi::membersPlayed (*group, s) });
        }
        std::sort (own.begin(), own.end(), [] (const auto& a, const auto& b) { return a.first.getStart() < b.first.getStart(); });

        for (int m = 0; m < group->numMembers; ++m)
        {
            const auto lane = laneBounds (slot, m, group->numMembers).withTrimmedTop (m > 0 ? 1 : 0);
            if ((group->mutedMask & (1u << m)) != 0)
            {
                int x = slot.getX();
                for (const auto& [range, mask] : own)
                {
                    if (range.getStart() > x)
                        g.setColour (juce::Colours::black.withAlpha (0.6f)),
                        g.fillRect (juce::Rectangle<int> (x, lane.getY(), range.getStart() - x, lane.getHeight()).getIntersection (slot));
                    x = juce::jmax (x, range.getEnd());
                }
                if (x < slot.getRight())
                {
                    g.setColour (juce::Colours::black.withAlpha (0.6f));
                    g.fillRect (juce::Rectangle<int> (x, lane.getY(), slot.getRight() - x, lane.getHeight()));
                }
            }
            for (const auto& [range, mask] : own)
                if ((mask & (1u << m)) == 0)
                    paintHatch (g, juce::Rectangle<int> (range.getStart(), lane.getY(), range.getLength(), lane.getHeight()).getIntersection (slot));
        }
    }
}

// --- GroupLaneControls -----------------------------------------------------------------------

GroupLaneControls::GroupLaneControls (IntersectProcessor& p) : processor (p)
{
    setRepaintsOnMouseActivity (false);
}

void GroupLaneControls::setPointer (juce::Point<int> position)
{
    if (position == pointer)
        return;
    pointer = position;
    refresh (lastMap, lastView);
    repaint();
}

void GroupLaneControls::layoutHeader (Header& h) const
{
    h.dense = h.lane.getHeight() < kDenseBelow;
    h.full = h.lane.getHeight() >= kFullFrom;
    const int rowH = h.dense ? juce::jmax (8, h.lane.getHeight() - 1) : kHeaderH;
    auto row = juce::Rectangle<int> (h.lane.getX() + (h.dense ? 0 : 3), h.lane.getY() + (h.dense ? 0 : 3), 0, rowH);

    // Badge and M keep their places when the header opens, so M never moves out from under the pointer.
    int x = row.getX() + 2;
    h.badge = { x, row.getCentreY() - kBadge / 2, kBadge, kBadge };
    x = h.badge.getRight() + kGap;
    h.mute = { x, row.getCentreY() - 5, kMuteW, 10 };
    x = h.mute.getRight() + kGap;
    h.nameArea = {};
    if (h.open)
    {
        h.nameArea = { x, row.getY(), kNameW, rowH };
        x = h.nameArea.getRight() + kGap;
    }
    h.level = h.dbArea = h.remove = h.dots = {};
    if (h.open && ! h.dense)
    {
        if (h.full)
        {
            h.level = { x, row.getY(), kLevelW, rowH };
            x = h.level.getRight() + 1;
            h.dbArea = { x, row.getY(), kDbW, rowH };
            x = h.dbArea.getRight() + kGap;
            h.remove = { x, row.getY(), kIconW, rowH };
            x = h.remove.getRight() + 1;
        }
        h.dots = { x, row.getY(), kIconW, rowH };
        x = h.dots.getRight() + 1;
    }
    h.box = juce::Rectangle<int> (row.getX(), row.getY(), x + 2 - row.getX(), rowH).getIntersection (h.lane);
}

void GroupLaneControls::refresh (const TimelineMap& map, const TimelineMap::View& view)
{
    lastMap = map;
    lastView = view;
    headers.clear();
    sliceMutes.clear();
    addChips.clear();
    groupChips.clear();

    const auto& ui = processor.getUiSliceSnapshot();
    const auto bounds = getLocalBounds();
    const double rate = processor.sampleData.getDecodedSampleRate();

    for (const auto& segment : map.getSegments())
    {
        const auto slot = slotBounds (bounds, map, view, segment);
        if (slot.getWidth() < 4)
            continue;

        if (! segment.isGroup)
        {
            // + GROUP on the standalone sample under the pointer, when another sample could join it.
            if (slot.contains (pointer) && slot.getWidth() >= 60)
            {
                const auto candidates = GroupUi::joinCandidates (ui, segment.sampleId, rate);
                if (std::any_of (candidates.begin(), candidates.end(), [] (const auto& c) { return c.canJoin; }))
                    groupChips.push_back ({ segment.sampleId, { slot.getX() + 4, slot.getY() + 3, 44, 12 }, true,
                                            "Group this sample with others of the same length" });
            }
            continue;
        }

        const auto* group = ui.sampleGroups.groupAnchoredBy (segment.sampleId);
        if (group == nullptr)
            continue;

        for (int m = 0; m < group->numMembers; ++m)
        {
            Header h;
            h.sampleId = group->memberIds[(size_t) m];
            h.memberIndex = m;
            h.muted = (group->mutedMask & (1u << m)) != 0;
            h.gainDb = group->gainDb[(size_t) m];
            h.name = GroupUi::memberName (ui, h.sampleId);
            h.lane = GroupLanePainter::laneBounds (slot, m, group->numMembers);
            h.open = h.sampleId == openHeaderId;
            layoutHeader (h);
            headers.push_back (h);
        }

        // + ADD SAMPLE sits under the last lane's header; where it would cover it (dense lanes), the
        // group menu's Add sample... is the way in.
        const juce::Rectangle<int> addArea { slot.getX() + 4, slot.getBottom() - 15, 62, 12 };
        if (slot.getWidth() >= 70 && (headers.empty() || headers.back().box.getBottom() + 2 <= addArea.getY()))
        {
            const bool full = group->numMembers >= SampleGroup::kMaxMembers;
            const auto candidates = GroupUi::joinCandidates (ui, segment.sampleId, rate);
            const bool any = std::any_of (candidates.begin(), candidates.end(), [] (const auto& c) { return c.canJoin; });
            addChips.push_back ({ segment.sampleId, addArea, ! full && any,
                                  full ? "The group is full (16)"
                                       : any ? "Add a sample the same length as the group"
                                             : "No other sample in the kit is the same length" });
        }

        // The selected slice's own mutes, inside it, when they fit.
        const int sel = ui.selectedSlice;
        if (sel >= 0 && sel < ui.numSlices && ui.slices[(size_t) sel].sampleId == segment.sampleId)
        {
            const auto& s = ui.slices[(size_t) sel];
            const int x1 = map.kitToPixel (view, s.startSample);
            const int x2 = map.kitToPixel (view, s.endSample);
            const int laneH = slot.getHeight() / juce::jmax (1, group->numMembers);
            selectedSliceIdx = sel;
            sliceHasOwnMembers = GroupUi::sliceHasOwnMembers (s);
            slicePlayed = GroupUi::membersPlayed (*group, s);
            if (x2 - x1 >= kSliceMInset + kSliceMW + 3 && laneH >= kSliceMH + 4)
            {
                for (int m = 0; m < group->numMembers; ++m)
                {
                    const auto lane = GroupLanePainter::laneBounds (slot, m, group->numMembers);
                    auto area = juce::Rectangle<int> (x1 + kSliceMInset, lane.getY() + 3, kSliceMW, kSliceMH);
                    const auto& header = headers[headers.size() - (size_t) group->numMembers + (size_t) m];
                    if (area.intersects (header.box))
                        area.setY (header.box.getBottom() + 2);
                    if (area.getBottom() > lane.getBottom() - 1 || area.getRight() > slot.getRight())
                        continue;
                    sliceMutes.push_back ({ group->memberIds[(size_t) m], m, (slicePlayed & (1u << m)) != 0, area });
                }
            }
        }
    }
}

bool GroupLaneControls::hitTest (int x, int y)
{
    const juce::Point<int> p { x, y };
    for (const auto& h : headers)
        if (h.box.contains (p))
            return true;
    for (const auto& m : sliceMutes)
        if (m.area.contains (p))
            return true;
    for (const auto* chips : { &addChips, &groupChips })
        for (const auto& c : *chips)
            if (c.area.contains (p))
                return true;
    return false;
}

void GroupLaneControls::paint (juce::Graphics& g)
{
    for (const auto& h : headers)
    {
        g.setColour (getTheme().surface0.withAlpha (h.dense ? 0.82f : 0.9f));
        if (h.dense)
            g.fillRect (h.box);
        else
            g.fillRoundedRectangle (h.box.toFloat(), (float) h.box.getHeight() * 0.5f);
        if (! h.dense)
        {
            g.setColour ((h.open ? getTheme().surface5 : getTheme().surface4).withAlpha (0.8f));
            g.drawRoundedRectangle (h.box.toFloat().reduced (0.5f), (float) h.box.getHeight() * 0.5f, 1.0f);
        }

        paintBadge (g, h.badge, h.memberIndex);
        paintMute (g, h.mute, h.muted);
        if (! h.nameArea.isEmpty())
        {
            g.setColour (GroupUi::memberColour (h.memberIndex));
            g.setFont (IntersectLookAndFeel::makeFont (7.5f, true));
            g.drawText (h.name, h.nameArea, juce::Justification::centredLeft, true);
        }
        if (! h.level.isEmpty())
        {
            const auto track = h.level.toFloat().withSizeKeepingCentre ((float) h.level.getWidth() - 2.0f, 2.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (juce::jmax (-60.0f, h.gainDb) + 60.0f) / 66.0f);
            g.setColour (getTheme().surface5);
            g.fillRoundedRectangle (track, 1.0f);
            g.setColour (GroupUi::memberColour (h.memberIndex));
            g.fillRoundedRectangle (track.withWidth (track.getWidth() * frac), 1.0f);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ track.getX() + track.getWidth() * frac, track.getCentreY() }));
            g.setColour (getTheme().text1);
            g.setFont (IntersectLookAndFeel::makeFont (7.0f));
            g.drawText (GroupUi::formatDb (h.gainDb), h.dbArea, juce::Justification::centredRight, false);
        }
        g.setColour (getTheme().text1);
        g.setFont (IntersectLookAndFeel::makeFont (8.5f, true));
        if (! h.remove.isEmpty())
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")), h.remove, juce::Justification::centred, false);
        if (! h.dots.isEmpty())
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x8b\xaf")), h.dots, juce::Justification::centred, false);
    }

    for (const auto& m : sliceMutes)
        paintMute (g, m.area, ! m.played, sliceHasOwnMembers ? 1.0f : 0.55f);

    for (const auto& c : addChips)
        paintChip (g, c.area, "+ ADD SAMPLE", c.enabled);
    for (const auto& c : groupChips)
        paintChip (g, c.area, "+ GROUP", c.enabled);
}

GroupLaneControls::Header* GroupLaneControls::headerAt (juce::Point<int> p)
{
    for (auto& h : headers)
        if (h.box.contains (p))
            return &h;
    return nullptr;
}

void GroupLaneControls::mouseMove (const juce::MouseEvent& e)
{
    setPointer (e.getPosition());   // same coordinates as the waveform view, which covers it exactly
    const auto* h = headerAt (e.getPosition());
    const int id = h != nullptr ? h->sampleId : -1;
    juce::String tip;
    for (const auto& c : addChips)
        if (c.area.contains (e.getPosition()))
            tip = c.tooltip;
    for (const auto& c : groupChips)
        if (c.area.contains (e.getPosition()))
            tip = c.tooltip;
    for (const auto& m : sliceMutes)
        if (m.area.contains (e.getPosition()))
            tip = "Mute in this slice only";
    setTooltip (tip);

    if (id != openHeaderId)
    {
        openHeaderId = id;
        refresh (lastMap, lastView);
        repaint();
    }
}

void GroupLaneControls::mouseExit (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))   // left the waveform from a control
        setPointer ({ -1, -1 });
    if (openHeaderId >= 0 && draggingLevelId < 0)
    {
        openHeaderId = -1;
        refresh (lastMap, lastView);
        repaint();
    }
}

void GroupLaneControls::toggleGroupMute (const Header& header)
{
    const bool muting = ! header.muted;
    GroupUi::push (processor, IntersectProcessor::CmdSetMemberMute, header.sampleId, {}, muting ? 1 : 0);
    if (! muting)
        return;

    // Muting doesn't reach slices with their own members: say so when it matters.
    const auto& ui = processor.getUiSliceSnapshot();
    const auto* group = ui.sampleGroups.groupWith (header.sampleId);
    int stillPlays = 0;
    for (int i = 0; group != nullptr && i < ui.numSlices; ++i)
    {
        const auto& s = ui.slices[(size_t) i];
        if (s.sampleId == group->anchorId() && GroupUi::sliceHasOwnMembers (s) && (s.memberMask & (1u << header.memberIndex)) != 0)
            ++stillPlays;
    }
    if (stillPlays > 0)
        processor.showTransientStatusMessage (header.name + " is muted, but still plays in " + juce::String (stillPlays)
                                                  + (stillPlays == 1 ? " slice" : " slices") + " with their own members",
                                              false);
}

void GroupLaneControls::setGainFromX (Header& header, int x)
{
    const float frac = juce::jlimit (0.0f, 1.0f, (float) (x - header.level.getX()) / (float) juce::jmax (1, header.level.getWidth()));
    float db = -60.0f + frac * 66.0f;
    if (db <= -59.5f)
        db = -100.0f;   // the left end is off
    header.gainDb = db;
    GroupUi::push (processor, IntersectProcessor::CmdSetMemberGain, header.sampleId, {}, 0, db);
    repaint();
}

void GroupLaneControls::showMemberMenu (const Header& header)
{
    const auto& ui = processor.getUiSliceSnapshot();
    const auto* group = ui.sampleGroups.groupWith (header.sampleId);
    const int currentOutput = group != nullptr ? group->outputBus[(size_t) header.memberIndex] : -1;

    juce::PopupMenu outputs;
    outputs.addItem (100, "Slice OUT", true, currentOutput < 0);
    for (int bus = 0; bus < 16; ++bus)
        outputs.addItem (101 + bus, "Out " + juce::String (bus + 1), true, currentOutput == bus);

    juce::PopupMenu menu;
    menu.addSectionHeader (header.name);
    menu.addItem (1, header.muted ? "Unmute" : "Mute");
    menu.addItem (2, juce::String (juce::CharPointer_UTF8 ("Mix\xe2\x80\xa6")));
    menu.addSubMenu ("Output", outputs);
    menu.addItem (3, "Duplicate as separate sample", onDuplicateRequested != nullptr);
    menu.addSeparator();
    menu.addItem (4, "Remove from group");

    const auto anchorArea = header.box;
    const int id = header.sampleId;
    const auto copy = header;
    juce::Component::SafePointer<GroupLaneControls> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (anchorArea)),
                        [safe, id, copy, anchorArea] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& p = safe->processor;
        if (result == 1)
            safe->toggleGroupMute (copy);
        else if (result == 2)
            GroupCallouts::showMix (*safe, anchorArea, p, id);
        else if (result == 3 && safe->onDuplicateRequested)
            safe->onDuplicateRequested (id);
        else if (result == 4)
            GroupUi::push (p, IntersectProcessor::CmdGroupRemoveMember, id);
        else if (result >= 100)
            GroupUi::push (p, IntersectProcessor::CmdSetMemberOutput, id, {}, result == 100 ? -1 : result - 101);
    });
}

void GroupLaneControls::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (auto* h = headerAt (p))
    {
        if (e.mods.isPopupMenu() || h->dots.contains (p))
            showMemberMenu (*h);
        else if (h->mute.contains (p))
            toggleGroupMute (*h);
        else if (h->level.contains (p))
        {
            IntersectProcessor::Command begin;
            begin.type = IntersectProcessor::CmdBeginGesture;
            processor.pushCommand (begin);
            draggingLevelId = h->sampleId;
            setGainFromX (*h, p.x);
        }
        else if (h->remove.contains (p))
            GroupUi::push (processor, IntersectProcessor::CmdGroupRemoveMember, h->sampleId);
        return;
    }

    for (const auto& m : sliceMutes)
    {
        if (! m.area.contains (p))
            continue;
        const uint32_t next = slicePlayed ^ (1u << m.memberIndex);
        if (next != 0)
            GroupUi::setSliceMembers (processor, selectedSliceIdx, next);
        return;
    }

    for (const auto& c : addChips)
        if (c.area.contains (p) && c.enabled)
            return GroupCallouts::showPicker (*this, c.area, processor, c.sampleId);
    for (const auto& c : groupChips)
        if (c.area.contains (p))
            return GroupCallouts::showPicker (*this, c.area, processor, c.sampleId);
}

void GroupLaneControls::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingLevelId < 0)
        return;
    for (auto& h : headers)
        if (h.sampleId == draggingLevelId && ! h.level.isEmpty())
            setGainFromX (h, e.getPosition().x);
}

void GroupLaneControls::mouseUp (const juce::MouseEvent&)
{
    draggingLevelId = -1;
}

void GroupLaneControls::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (auto* h = headerAt (e.getPosition()); h != nullptr && h->level.contains (e.getPosition()))
        GroupUi::push (processor, IntersectProcessor::CmdSetMemberGain, h->sampleId, {}, 0, 0.0f);
}
