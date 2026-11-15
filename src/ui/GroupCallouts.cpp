#include "GroupCallouts.h"
#include "Callout.h"
#include "GroupUi.h"
#include "IntersectLookAndFeel.h"
#include "settings/SettingsControls.h"
#include <list>

namespace
{
constexpr int kRowH = 22;
constexpr int kMaxVisibleRows = 10;   // more rows scroll, so a 16-member group still fits the editor

const juce::Colour kMutedBg { 0xff6b5a1e };
const juce::Colour kMutedText { 0xfff5d77a };

void paintBadge (juce::Graphics& g, juce::Rectangle<float> area, int memberIndex)
{
    g.setColour (GroupUi::memberColour (memberIndex));
    g.fillEllipse (area);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (IntersectLookAndFeel::makeFont (8.0f, true));
    g.drawText (juce::String (GroupUi::memberNumber (memberIndex)), area, juce::Justification::centred, false);
}

void styleMuteButton (juce::TextButton& button)
{
    button.setClickingTogglesState (true);
    button.setColour (juce::TextButton::buttonColourId, getTheme().surface4);
    button.setColour (juce::TextButton::buttonOnColourId, kMutedBg);
    button.setColour (juce::TextButton::textColourOffId, getTheme().text1);
    button.setColour (juce::TextButton::textColourOnId, kMutedText);
}

/** A tick box and its text in the popovers' own small font (juce::ToggleButton's default look sizes
    the text from the row height, which comes out far larger than the rest of a popover). */
class SmallToggle : public juce::ToggleButton
{
public:
    using juce::ToggleButton::ToggleButton;

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto& theme = getTheme();
        auto area = getLocalBounds();
        const auto box = area.removeFromLeft (16).withSizeKeepingCentre (11, 11).toFloat();
        const float alpha = isEnabled() ? 1.0f : 0.4f;
        g.setColour ((getToggleState() ? theme.accent : theme.surface3).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (box, 2.0f);
        g.setColour ((highlighted ? theme.text1 : theme.surface5).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (box.reduced (0.5f), 2.0f, 1.0f);
        if (getToggleState())
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 2.5f, box.getCentreY());
            tick.lineTo (box.getX() + 4.6f, box.getBottom() - 2.8f);
            tick.lineTo (box.getRight() - 2.3f, box.getY() + 2.6f);
            g.setColour (theme.surface0);
            g.strokePath (tick, juce::PathStrokeType (1.6f));
        }
        g.setColour ((isEnabled() ? theme.text1 : theme.text0).withMultipliedAlpha (isEnabled() ? 1.0f : 0.7f));
        g.setFont (IntersectLookAndFeel::makeFont (8.5f));
        g.drawText (getButtonText(), area.withTrimmedLeft (4), juce::Justification::centredLeft, true);
    }
};

/** A badge, a name and whatever controls the popover adds, laid out left to right. */
class MemberRow : public juce::Component
{
public:
    MemberRow (int memberIndexIn, const juce::String& nameIn) : memberIndex (memberIndexIn), name (nameIn) {}

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        paintBadge (g, area.removeFromLeft (18.0f).withSizeKeepingCentre (16.0f, 16.0f), memberIndex);
        g.setColour (GroupUi::memberColour (memberIndex));
        g.setFont (IntersectLookAndFeel::makeFont (8.8f, true));
        g.drawText (name, area.withTrimmedLeft (6.0f).withWidth ((float) nameWidth), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().withTrimmedLeft (24 + nameWidth + 4);
        for (auto* control : controls)
        {
            const int w = control->getProperties().getWithDefault ("rowWidth", 24);
            if (w < 0)
            {
                control->setBounds (area.removeFromLeft (juce::jmax (0, area.getWidth() + w)).reduced (2, 3));
                continue;
            }
            control->setBounds (area.removeFromLeft (w).reduced (2, 3));
        }
    }

    void add (juce::Component& control, int width)
    {
        control.getProperties().set ("rowWidth", width);   // < 0: fill, leaving -width for what follows
        controls.add (&control);
        addAndMakeVisible (control);
    }

    int nameWidth = 84;

private:
    int memberIndex;
    juce::String name;
    juce::Array<juce::Component*> controls;
};

/** Rows in a viewport: at most kMaxVisibleRows tall, scrolling beyond. */
class RowList : public juce::Component
{
public:
    RowList()
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }

    void addRow (std::unique_ptr<juce::Component> row) { content.addAndMakeVisible (*row); rows.push_back (std::move (row)); }
    int getPreferredHeight() const { return kRowH * juce::jmin ((int) rows.size(), kMaxVisibleRows); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        const int w = getWidth() - (rows.size() > (size_t) kMaxVisibleRows ? viewport.getScrollBarThickness() : 0);
        content.setSize (w, kRowH * (int) rows.size());
        for (size_t i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, (int) i * kRowH, w, kRowH);
    }

private:
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Component>> rows;
};

// --- Group with / Add to group -----------------------------------------------------------------

class PickerPopover final : public Callout::Popover
{
public:
    PickerPopover (IntersectProcessor& p, int targetIn) : processor (p), target (targetIn)
    {
        const auto& ui = processor.getUiSliceSnapshot();
        const auto* group = ui.sampleGroups.groupWith (target);
        adding = group != nullptr;
        const double rate = processor.sampleData.getDecodedSampleRate();
        candidates = GroupUi::joinCandidates (ui, target, rate);
        freePlaces = SampleGroup::kMaxMembers - (group != nullptr ? group->numMembers : 1);

        if (adding)
            heading = "ADD TO " + GroupUi::groupName (ui, *group);
        else if (const auto* sample = GroupUi::findSample (ui, target))
            heading = "GROUP " + sample->fileName.toString() + juce::String (juce::CharPointer_UTF8 (" WITH\xe2\x80\xa6"));

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const auto& c = candidates[i];
            auto toggle = std::make_unique<SmallToggle> (c.canJoin ? c.name + "   " + c.meta : c.name + "   " + c.reason);
            toggle->setEnabled (c.canJoin);
            toggle->onClick = [this] { update(); };
            toggles.push_back (toggle.get());
            list.addRow (std::move (toggle));
        }
        addAndMakeVisible (list);

        for (auto* button : { &cancelButton, &confirmButton })
            addAndMakeVisible (*button);
        SettingsUi::styleButton (cancelButton);
        SettingsUi::styleButton (confirmButton, true);
        confirmButton.setButtonText (adding ? "ADD" : "GROUP");
        cancelButton.onClick = [this] { dismiss(); };
        confirmButton.onClick = [this] { returnKeyPressed(); };

        const int listH = juce::jmax (kRowH, list.getPreferredHeight());
        setSize (330, 2 * Callout::kPadding + Callout::kHeadingH + 4 + 13 + 6 + listH + 6 + 26 + 8
                      + Callout::kButtonH + Callout::kShadow);
        update();
    }

    void paint (juce::Graphics& g) override
    {
        Popover::paint (g);
        Callout::paintHeading (g, heading, headingArea);
        g.setFont (IntersectLookAndFeel::makeFont (8.5f));
        g.setColour (getTheme().text1);
        g.drawText (subtitle, subtitleArea, juce::Justification::centredLeft, true);
        if (candidates.empty())
            g.drawText ("No other samples in the kit", list.getBounds(), juce::Justification::centredLeft, true);
        if (warning.isNotEmpty())
        {
            g.setColour (juce::Colour (0xfff0c070));
            g.drawFittedText (warning, warningArea, juce::Justification::centredLeft, 2);
        }
    }

    void resized() override
    {
        auto area = getContentArea();
        headingArea = area.removeFromTop (Callout::kHeadingH);
        area.removeFromTop (4);
        subtitleArea = area.removeFromTop (13);
        area.removeFromTop (6);
        list.setBounds (area.removeFromTop (juce::jmax (kRowH, list.getPreferredHeight())));
        area.removeFromTop (6);
        warningArea = area.removeFromTop (26);
        auto buttons = area.removeFromBottom (Callout::kButtonH);
        confirmButton.setBounds (buttons.removeFromRight (70));
        buttons.removeFromRight (6);
        cancelButton.setBounds (buttons.removeFromRight (70));
    }

protected:
    void returnKeyPressed() override
    {
        std::vector<int> ids;
        for (size_t i = 0; i < candidates.size(); ++i)
            if (toggles[i]->getToggleState())
                ids.push_back (candidates[i].sampleId);
        if (ids.empty())
            return;

        auto& p = processor;
        const int targetId = target;
        const bool add = adding;
        dismissAndRun ([&p, targetId, add, ids]
        {
            GroupUi::push (p, add ? IntersectProcessor::CmdGroupAddMembers : IntersectProcessor::CmdGroupCreate, targetId, ids);
        });
    }

private:
    void update()
    {
        int ticked = 0;
        juce::StringArray losing;
        for (size_t i = 0; i < candidates.size(); ++i)
            if (toggles[i]->getToggleState())
            {
                ++ticked;
                if (candidates[i].numSlices > 0)
                    losing.add (candidates[i].name);
            }

        // A group holds 16: stop ticking at the free places.
        for (size_t i = 0; i < candidates.size(); ++i)
            toggles[i]->setEnabled (candidates[i].canJoin && (toggles[i]->getToggleState() || ticked < freePlaces));

        subtitle = adding ? "Only samples the same length as the group can join."
                          : "Grouped samples share these slices and play together.";
        if (freePlaces < (int) candidates.size())
            subtitle = juce::String (freePlaces - ticked) + " of " + juce::String (freePlaces) + " places left";
        warning = losing.isEmpty() ? juce::String()
                                   : losing.joinIntoString (", ") + (losing.size() == 1 ? "'s own slices will be removed."
                                                                                       : ": their own slices will be removed.");
        confirmButton.setEnabled (ticked > 0);
        repaint();
    }

    IntersectProcessor& processor;
    int target;
    bool adding = false;
    int freePlaces = 0;
    std::vector<GroupUi::Candidate> candidates;
    std::vector<juce::ToggleButton*> toggles;
    RowList list;
    juce::TextButton cancelButton { "CANCEL" }, confirmButton { "GROUP" };
    juce::String heading, subtitle, warning;
    juce::Rectangle<int> headingArea, subtitleArea, warningArea;
};

// --- MIX ---------------------------------------------------------------------------------------

class MixPopover final : public Callout::Popover
{
public:
    MixPopover (IntersectProcessor& p, int groupMemberId) : processor (p)
    {
        const auto& ui = processor.getUiSliceSnapshot();
        const auto* group = ui.sampleGroups.groupWith (groupMemberId);
        if (group != nullptr)
        {
            heading = "MIX  " + GroupUi::groupName (ui, *group);
            for (int m = 0; m < group->numMembers; ++m)
                addMemberRow (ui, *group, m);
        }
        addAndMakeVisible (list);
        setSize (440, 2 * Callout::kPadding + Callout::kHeadingH + 6 + juce::jmax (kRowH, list.getPreferredHeight())
                      + 6 + 24 + Callout::kShadow);
    }

    void paint (juce::Graphics& g) override
    {
        Popover::paint (g);
        Callout::paintHeading (g, heading, headingArea);
        g.setColour (getTheme().text0);
        g.setFont (IntersectLookAndFeel::makeFont (8.0f));
        g.drawFittedText ("Slices play this mix unless they have their own members. A member on its own output "
                          "adds a voice per note.", footArea, juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto area = getContentArea();
        headingArea = area.removeFromTop (Callout::kHeadingH);
        area.removeFromTop (6);
        list.setBounds (area.removeFromTop (juce::jmax (kRowH, list.getPreferredHeight())));
        area.removeFromTop (6);
        footArea = area;
    }

private:
    void addMemberRow (const GroupUi::Snapshot& ui, const SampleGroup& group, int m)
    {
        const int id = group.memberIds[(size_t) m];
        auto row = std::make_unique<MemberRow> (m, GroupUi::memberName (ui, id));
        auto& controls = rowControls.emplace_back();

        styleMuteButton (controls.mute);
        controls.mute.setButtonText ("M");
        controls.mute.setToggleState ((group.mutedMask & (1u << m)) != 0, juce::dontSendNotification);
        controls.mute.onClick = [this, id, &controls]
        {
            GroupUi::push (processor, IntersectProcessor::CmdSetMemberMute, id, {}, controls.mute.getToggleState() ? 1 : 0);
        };

        controls.level.setSliderStyle (juce::Slider::LinearHorizontal);
        controls.level.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        controls.level.setRange (-60.0, 6.0, 0.1);
        controls.level.setDoubleClickReturnValue (true, 0.0);
        controls.level.setValue (juce::jmax (-60.0f, group.gainDb[(size_t) m]), juce::dontSendNotification);
        controls.level.setColour (juce::Slider::trackColourId, GroupUi::memberColour (m));
        controls.level.onDragStart = [this]
        {
            IntersectProcessor::Command begin;
            begin.type = IntersectProcessor::CmdBeginGesture;
            processor.pushCommand (begin);
        };
        controls.level.onValueChange = [this, id, &controls]
        {
            const float db = (float) controls.level.getValue();
            const float sent = db <= -59.95f ? -100.0f : db;   // the bottom of the slider is off
            controls.db.setText (GroupUi::formatDb (sent), juce::dontSendNotification);
            GroupUi::push (processor, IntersectProcessor::CmdSetMemberGain, id, {}, 0, sent);
        };
        controls.db.setText (GroupUi::formatDb (group.gainDb[(size_t) m]), juce::dontSendNotification);
        controls.db.setFont (IntersectLookAndFeel::makeFont (8.5f));
        controls.db.setJustificationType (juce::Justification::centredRight);

        juce::StringArray outputs { "Slice OUT" };
        for (int bus = 0; bus < 16; ++bus)
            outputs.add ("Out " + juce::String (bus + 1));
        controls.output.setOptions (outputs);
        controls.output.setSelectedIndex (group.outputBus[(size_t) m] + 1);   // -1 (slice OUT) is the first item
        controls.output.onChange = [this, id] (int index)
        {
            GroupUi::push (processor, IntersectProcessor::CmdSetMemberOutput, id, {}, index - 1);
        };

        SettingsUi::styleButton (controls.remove);
        controls.remove.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")));
        controls.remove.setTooltip ("Remove from the group (it keeps a copy of the slices)");
        controls.remove.onClick = [this, id]
        {
            auto& p = processor;
            dismissAndRun ([&p, id] { GroupUi::push (p, IntersectProcessor::CmdGroupRemoveMember, id); });
        };

        row->add (controls.mute, 26);
        row->add (controls.level, -(58 + 88 + 26));
        row->add (controls.db, 58);
        row->add (controls.output, 88);
        row->add (controls.remove, 26);
        list.addRow (std::move (row));
    }

    struct RowControls
    {
        juce::TextButton mute, remove;
        juce::Slider level;
        juce::Label db;
        DropdownField output;
    };

    IntersectProcessor& processor;
    std::list<RowControls> rowControls;   // stable addresses: the rows hold references
    RowList list;
    juce::String heading;
    juce::Rectangle<int> headingArea, footArea;
};

// --- A slice's own members -----------------------------------------------------------------------

class SliceMembersPopover final : public Callout::Popover
{
public:
    SliceMembersPopover (IntersectProcessor& p, int sliceIdxIn) : processor (p), sliceIdx (sliceIdxIn)
    {
        const auto& ui = processor.getUiSliceSnapshot();
        if (sliceIdx >= 0 && sliceIdx < ui.numSlices)
        {
            const auto& slice = ui.slices[(size_t) sliceIdx];
            if (const auto* g = ui.sampleGroups.groupAnchoredBy (slice.sampleId))
            {
                group = *g;
                played = GroupUi::membersPlayed (group, slice);
                own = GroupUi::sliceHasOwnMembers (slice);
                for (int m = 0; m < group.numMembers; ++m)
                {
                    auto row = std::make_unique<MemberRow> (m, GroupUi::memberName (ui, group.memberIds[(size_t) m]));
                    auto& mute = mutes.emplace_back();
                    styleMuteButton (mute);
                    mute.setButtonText ("M");
                    mute.setTooltip ("Mute in this slice only");
                    mute.onClick = [this, m] { toggleMember (m); };
                    row->add (mute, 26);
                    list.addRow (std::move (row));
                }
            }
        }
        heading = "SLICE " + juce::String (sliceIdx + 1);

        groupMix.setButtonText ("Group mix");
        groupMix.setTooltip ("On: the slice follows the group's mutes. Off: it keeps its own members.");
        groupMix.onClick = [this]
        {
            if (groupMix.getToggleState())
            {
                if (own)
                    GroupUi::followGroupMix (processor, sliceIdx);
                own = false;
                played = group.unmutedMask();
            }
            else if (! own)
            {
                // Its own members, starting from what it plays now (a slice plays at least one member).
                played = group.unmutedMask() != 0 ? group.unmutedMask() : group.allMembersMask();
                own = true;
                GroupUi::setSliceMembers (processor, sliceIdx, played);
            }
            refresh();
        };
        SettingsUi::styleButton (unmuteAll);
        unmuteAll.setButtonText ("UNMUTE ALL");
        unmuteAll.onClick = [this]
        {
            played = group.allMembersMask();
            own = true;
            GroupUi::setSliceMembers (processor, sliceIdx, played);
            refresh();
        };
        for (auto* c : std::initializer_list<juce::Component*> { &groupMix, &unmuteAll, &list })
            addAndMakeVisible (*c);

        setSize (300, 2 * Callout::kPadding + Callout::kHeadingH + 6 + kRowH + 4 + juce::jmax (kRowH, list.getPreferredHeight())
                      + Callout::kShadow);
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        Popover::paint (g);
        Callout::paintHeading (g, heading, headingArea);
    }

    void resized() override
    {
        auto area = getContentArea();
        auto top = area.removeFromTop (Callout::kHeadingH);
        unmuteAll.setBounds (top.removeFromRight (80));
        headingArea = top;
        area.removeFromTop (6);
        groupMix.setBounds (area.removeFromTop (kRowH));
        area.removeFromTop (4);
        list.setBounds (area.removeFromTop (juce::jmax (kRowH, list.getPreferredHeight())));
    }

private:
    void toggleMember (int m)
    {
        const uint32_t next = played ^ (1u << m);
        if ((next & group.allMembersMask()) == 0)
        {
            refresh();   // a slice plays at least one member
            return;
        }
        played = next;
        own = true;
        GroupUi::setSliceMembers (processor, sliceIdx, played);
        refresh();
    }

    void refresh()
    {
        groupMix.setToggleState (! own, juce::dontSendNotification);
        int m = 0;
        for (auto& mute : mutes)
        {
            mute.setToggleState ((played & (1u << m)) == 0, juce::dontSendNotification);
            mute.setAlpha (own ? 1.0f : 0.55f);
            ++m;
        }
    }

    IntersectProcessor& processor;
    int sliceIdx;
    SampleGroup group;
    uint32_t played = 0;
    bool own = false;
    std::list<juce::TextButton> mutes;
    SmallToggle groupMix;
    juce::TextButton unmuteAll;
    RowList list;
    juce::String heading;
    juce::Rectangle<int> headingArea;
};
}

void GroupCallouts::showPicker (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                                IntersectProcessor& processor, int targetSampleId)
{
    (new PickerPopover (processor, targetSampleId))->show (anchor, anchorArea);
}

void GroupCallouts::showMix (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                             IntersectProcessor& processor, int groupMemberId)
{
    (new MixPopover (processor, groupMemberId))->show (anchor, anchorArea);
}

void GroupCallouts::showSliceMembers (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                                      IntersectProcessor& processor, int sliceIdx)
{
    (new SliceMembersPopover (processor, sliceIdx))->show (anchor, anchorArea);
}
