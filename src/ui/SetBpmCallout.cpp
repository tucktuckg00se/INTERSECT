#include "SetBpmCallout.h"
#include "Callout.h"
#include "IntersectLookAndFeel.h"
#include "settings/SettingsControls.h"
#include <cmath>

namespace
{
constexpr int kWidth = 248;
constexpr int kLineH = 13;
constexpr int kFieldH = 22;

class SetBpmPopover final : public Callout::Popover
{
public:
    SetBpmPopover (const juce::String& lengthLabelIn, const juce::StringArray& choices, int initialChoice,
                   std::function<float (int)> previewBpmIn, std::function<void (int)> onSetIn)
        : lengthLabel (lengthLabelIn), previewBpm (std::move (previewBpmIn)), onSet (std::move (onSetIn))
    {
        lengthField.setOptions (choices);
        lengthField.setSelectedIndex (juce::jlimit (0, choices.size() - 1, initialChoice));
        lengthField.onChange = [this] (int) { updatePreview(); };
        addAndMakeVisible (lengthField);

        for (auto* button : { &cancelButton, &setButton })
            addAndMakeVisible (*button);
        SettingsUi::styleButton (cancelButton);
        SettingsUi::styleButton (setButton, true);
        setButton.setTooltip ("Return");
        cancelButton.setTooltip ("Esc");
        cancelButton.onClick = [this] { dismiss(); };
        setButton.onClick = [this] { returnKeyPressed(); };

        const int height = 2 * Callout::kPadding + Callout::kHeadingH + 6 + kLineH + 4 + kFieldH + 6 + kLineH + 8
                           + Callout::kButtonH + Callout::kShadow;
        setSize (kWidth, height);
        updatePreview();
    }

    void paint (juce::Graphics& g) override
    {
        Popover::paint (g);
        Callout::paintHeading (g, "SET BPM", headingArea);

        g.setColour (getTheme().text1);
        g.setFont (IntersectLookAndFeel::makeFont (8.8f));
        g.drawText (lengthLabel, labelArea, juce::Justification::centredLeft, true);

        g.setColour (preview > 0.0f ? getTheme().text2 : getTheme().text0);
        g.setFont (IntersectLookAndFeel::makeFont (9.5f));
        g.drawText (preview > 0.0f ? "= " + juce::String (preview, 2) + " BPM" : juce::String ("Nothing to measure"),
                    previewArea, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getContentArea();
        headingArea = area.removeFromTop (Callout::kHeadingH);
        area.removeFromTop (6);
        labelArea = area.removeFromTop (kLineH);
        area.removeFromTop (4);
        lengthField.setBounds (area.removeFromTop (kFieldH));
        area.removeFromTop (6);
        previewArea = area.removeFromTop (kLineH);

        auto buttons = getContentArea().removeFromBottom (Callout::kButtonH);
        setButton.setBounds (buttons.removeFromRight (SettingsUi::buttonWidthFor (setButton.getButtonText(), 64)));
        buttons.removeFromRight (SettingsUi::kButtonGap + 2);
        cancelButton.setBounds (buttons.removeFromRight (SettingsUi::buttonWidthFor (cancelButton.getButtonText(), 64)));
    }

private:
    void returnKeyPressed() override
    {
        const int choice = lengthField.getSelectedIndex();
        if (choice < 0 || preview <= 0.0f)
            return;
        auto action = onSet;
        dismissAndRun ([action, choice] { if (action != nullptr) action (choice); });
    }

    void updatePreview()
    {
        const int choice = lengthField.getSelectedIndex();
        preview = (choice >= 0 && previewBpm != nullptr) ? previewBpm (choice) : 0.0f;
        if (! std::isfinite (preview))
            preview = 0.0f;
        SettingsUi::setButtonEnabled (setButton, preview > 0.0f);
        repaint();
    }

    juce::String lengthLabel;
    std::function<float (int)> previewBpm;
    std::function<void (int)> onSet;
    float preview = 0.0f;

    DropdownField lengthField;
    juce::TextButton cancelButton { "CANCEL" };
    juce::TextButton setButton { "SET" };
    juce::Rectangle<int> headingArea, labelArea, previewArea;
};
} // namespace

void showSetBpmCallout (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                        const juce::String& lengthLabel,
                        const juce::StringArray& choices, int initialChoice,
                        std::function<float (int)> previewBpm,
                        std::function<void (int)> onSet)
{
    if (choices.isEmpty())
        return;
    auto* popover = new SetBpmPopover (lengthLabel, choices, initialChoice, std::move (previewBpm), std::move (onSet));
    popover->show (anchor, anchorArea);
}
