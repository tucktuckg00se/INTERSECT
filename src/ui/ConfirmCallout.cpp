#include "ConfirmCallout.h"
#include "FullViewBar.h"
#include "IntersectLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace
{
constexpr int kWidth = 248;
constexpr int kHeight = 78;
constexpr int kPadding = 10;
constexpr int kButtonH = 22;
constexpr int kGap = 4;   // between the anchor and the popover

/**
    The popover itself: added to the editor, modal (a click outside cancels), deleted by the modal
    manager when dismissed. Positioned by hand rather than with juce::CallOutBox, which can't point
    at something this close to the window edge (the sample X sits at the far right).
*/
class ConfirmPopover final : public juce::Component
{
public:
    ConfirmPopover (const juce::String& messageIn, const juce::String& confirmText, std::function<void()> onConfirmIn)
        : message (messageIn), onConfirm (std::move (onConfirmIn))
    {
        setWantsKeyboardFocus (true);

        confirmButton.setButtonText (confirmText);
        for (auto* button : { &cancelButton, &confirmButton })
            addAndMakeVisible (*button);

        // CANCEL looks like every other button; the destructive action stands out in the warning colour.
        FullViewBar::styleButton (cancelButton);
        FullViewBar::styleButton (confirmButton);
        confirmButton.setColour (juce::TextButton::buttonColourId, getTheme().color5);
        confirmButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        confirmButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        confirmButton.setTooltip ("Return");
        cancelButton.setTooltip ("Esc");

        cancelButton.onClick = [this] { dismiss(); };
        confirmButton.onClick = [this] { confirm(); };

        setSize (kWidth, kHeight);
    }

    /** Below the anchor with right edges aligned, flipped above when there's no room, always
        inside the parent. Takes keyboard focus and gives it back when dismissed. */
    void show (juce::Component& parent, juce::Rectangle<int> anchor)
    {
        previousFocus = juce::Component::getCurrentlyFocusedComponent();

        auto bounds = getBounds().withPosition (anchor.getRight() - getWidth(), anchor.getBottom() + kGap);
        if (bounds.getBottom() > parent.getHeight() - kGap)
            bounds.setY (anchor.getY() - kGap - getHeight());
        setBounds (bounds.constrainedWithin (parent.getLocalBounds().reduced (kGap)));

        parent.addAndMakeVisible (this);
        toFront (true);
        shown = true;
        enterModalState (true, nullptr, true);   // deleteWhenDismissed: the modal manager owns us now
    }

    void paint (juce::Graphics& g) override
    {
        // Same treatment as the popup menus: a soft drop below, surface2 body, thin outline.
        const auto box = getLocalBounds().toFloat().withTrimmedBottom (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.24f));
        g.fillRoundedRectangle (box.translated (0.0f, 2.0f), 4.0f);
        g.setColour (getTheme().surface2.brighter (0.03f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (getTheme().surface4);
        g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);

        g.setColour (getTheme().text2);
        g.setFont (IntersectLookAndFeel::makeFont (9.5f));
        g.drawFittedText (message, messageArea, juce::Justification::topLeft, 2, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().withTrimmedBottom (2).reduced (kPadding);
        auto buttons = area.removeFromBottom (kButtonH);
        confirmButton.setBounds (buttons.removeFromRight (78));
        buttons.removeFromRight (6);
        cancelButton.setBounds (buttons.removeFromRight (70));
        area.removeFromBottom (8);
        messageArea = area;
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey)
        {
            confirm();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            dismiss();
            return true;
        }
        return false;
    }

    void inputAttemptWhenModal() override { dismiss(); }   // a click anywhere else cancels

    void parentHierarchyChanged() override
    {
        if (shown && getParentComponent() == nullptr)
            dismiss();   // the editor went away (window closed) while asking
    }

private:
    void confirm()
    {
        const auto callback = onConfirm;   // this popover is deleted once it leaves modal state
        dismiss();
        if (callback != nullptr)
            callback();
    }

    void dismiss()
    {
        if (! isCurrentlyModal())
            return;

        auto focus = previousFocus;
        exitModalState (0);
        if (focus != nullptr && focus->isShowing())
            focus->grabKeyboardFocus();
    }

    juce::String message;
    std::function<void()> onConfirm;
    juce::TextButton cancelButton { "CANCEL" };
    juce::TextButton confirmButton;
    juce::Rectangle<int> messageArea;
    juce::Component::SafePointer<juce::Component> previousFocus;
    bool shown = false;
};
} // namespace

void showConfirmCallout (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                         const juce::String& message, const juce::String& confirmText,
                         std::function<void()> onConfirm)
{
    // Inside the editor, like the menus: it inherits the UI scale and stays in the plugin window.
    juce::Component* parent = anchor.findParentComponentOfClass<juce::AudioProcessorEditor>();
    if (parent == nullptr)
        parent = anchor.getTopLevelComponent();
    if (parent == nullptr)
        return;

    auto* popover = new ConfirmPopover (message, confirmText, std::move (onConfirm));
    popover->show (*parent, parent->getLocalArea (&anchor, anchorArea));
}
