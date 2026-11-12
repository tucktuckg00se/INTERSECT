#include "Callout.h"
#include "FullViewBar.h"
#include "IntersectLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace Callout
{
namespace
{
constexpr int kGap = 4;   // between the anchor and the popover
}

void paintBox (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto box = bounds.toFloat().withTrimmedBottom ((float) kShadow);
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRoundedRectangle (box.translated (0.0f, (float) kShadow), 4.0f);
    g.setColour (getTheme().surface2.brighter (0.03f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (getTheme().surface4);
    g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
}

void paintHeading (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
{
    g.setColour (getTheme().text1);
    g.setFont (IntersectLookAndFeel::makeFont (8.5f, true));
    g.drawText (text, area, juce::Justification::centredLeft, true);
}

//==============================================================================

Popover::Popover()
{
    setWantsKeyboardFocus (true);
}

void Popover::show (juce::Component& anchor, juce::Rectangle<int> anchorArea)
{
    // Inside the editor, like the menus: it inherits the UI scale and stays in the plugin window.
    juce::Component* parent = anchor.findParentComponentOfClass<juce::AudioProcessorEditor>();
    if (parent == nullptr)
        parent = anchor.getTopLevelComponent();
    if (parent == nullptr)
    {
        delete this;
        return;
    }

    previousFocus = juce::Component::getCurrentlyFocusedComponent();

    const auto area = parent->getLocalArea (&anchor, anchorArea);
    auto bounds = getBounds().withPosition (area.getRight() - getWidth(), area.getBottom() + kGap);
    if (bounds.getBottom() > parent->getHeight() - kGap)
        bounds.setY (area.getY() - kGap - getHeight());
    setBounds (bounds.constrainedWithin (parent->getLocalBounds().reduced (kGap)));

    parent->addAndMakeVisible (this);
    toFront (true);
    shown = true;
    enterModalState (true, nullptr, true);   // deleteWhenDismissed: the modal manager owns us now
}

void Popover::paint (juce::Graphics& g)
{
    paintBox (g, getLocalBounds());
}

bool Popover::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey)
    {
        returnKeyPressed();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        dismiss();
        return true;
    }
    return false;
}

void Popover::inputAttemptWhenModal()
{
    dismiss();   // a click anywhere else cancels
}

void Popover::parentHierarchyChanged()
{
    if (shown && getParentComponent() == nullptr)
        dismiss();   // the editor went away (window closed) while asking
}

void Popover::dismiss()
{
    if (! isCurrentlyModal())
        return;

    auto focus = previousFocus;
    exitModalState (0);
    if (focus != nullptr && focus->isShowing())
        focus->grabKeyboardFocus();
}

void Popover::dismissAndRun (std::function<void()> action)
{
    dismiss();
    if (action != nullptr)
        action();
}

juce::Rectangle<int> Popover::getContentArea() const
{
    return getLocalBounds().withTrimmedBottom (kShadow).reduced (kPadding);
}
} // namespace Callout

//==============================================================================

namespace
{
constexpr int kConfirmWidth = 248;
constexpr int kConfirmHeight = 78;

class ConfirmPopover final : public Callout::Popover
{
public:
    ConfirmPopover (const juce::String& messageIn, const juce::String& confirmText, std::function<void()> onConfirmIn)
        : message (messageIn), onConfirm (std::move (onConfirmIn))
    {
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
        confirmButton.onClick = [this] { returnKeyPressed(); };

        setSize (kConfirmWidth, kConfirmHeight);
    }

    void paint (juce::Graphics& g) override
    {
        Popover::paint (g);
        g.setColour (getTheme().text2);
        g.setFont (IntersectLookAndFeel::makeFont (9.5f));
        g.drawFittedText (message, messageArea, juce::Justification::topLeft, 2, 1.0f);
    }

    void resized() override
    {
        auto area = getContentArea();
        auto buttons = area.removeFromBottom (Callout::kButtonH);
        confirmButton.setBounds (buttons.removeFromRight (78));
        buttons.removeFromRight (6);
        cancelButton.setBounds (buttons.removeFromRight (70));
        area.removeFromBottom (8);
        messageArea = area;
    }

private:
    void returnKeyPressed() override
    {
        dismissAndRun (onConfirm);
    }

    juce::String message;
    std::function<void()> onConfirm;
    juce::TextButton cancelButton { "CANCEL" };
    juce::TextButton confirmButton;
    juce::Rectangle<int> messageArea;
};
} // namespace

void showConfirmCallout (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                         const juce::String& message, const juce::String& confirmText,
                         std::function<void()> onConfirm)
{
    auto* popover = new ConfirmPopover (message, confirmText, std::move (onConfirm));
    popover->show (anchor, anchorArea);
}
