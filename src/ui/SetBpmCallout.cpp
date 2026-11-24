#include "SetBpmCallout.h"
#include "Callout.h"
#include "IntersectLookAndFeel.h"
#include "settings/SettingsControls.h"
#include <cmath>

namespace
{
bool inRange (int value, int max) noexcept { return value >= 1 && value <= max; }   // juce::isPositiveAnd* allow 0
}

float SetBpmLength::inFourFourBars() const noexcept
{
    if (mode == Mode::note)
        return inRange (noteDenominator, kMaxNoteDenominator) ? 1.0f / (float) noteDenominator : 0.0f;

    if (! inRange (sigNumerator, kMaxSignature) || ! inRange (sigDenominator, kMaxSignature) || ! inRange (bars, kMaxBars))
        return 0.0f;
    return (float) bars * (float) sigNumerator / (float) sigDenominator;
}

namespace
{
constexpr int kWidth = 248;
constexpr int kLineH = 13;
constexpr int kFieldH = 22;
constexpr int kNumberW = 34;
constexpr float kMinBpm = 20.0f;    // SampleParams' BPM range
constexpr float kMaxBpm = 999.0f;

/** A whole number from 1 to max: digits only, Up/Down and the wheel step it, a click selects it. */
class NumberField final : public juce::TextEditor
{
public:
    explicit NumberField (int maxValueIn) : maxValue (maxValueIn)
    {
        setInputRestrictions (juce::String (maxValue).length(), "0123456789");
        setSelectAllWhenFocused (true);
        setJustification (juce::Justification::centred);
        setFont (IntersectLookAndFeel::makeFont (10.5f));
        setBorder (juce::BorderSize<int> (1, 2, 1, 2));
        setIndents (0, 0);
        setColour (backgroundColourId, getTheme().surface2.brighter (0.12f).withAlpha (0.98f));
        setColour (textColourId, getTheme().text2);
        setColour (outlineColourId, getTheme().surface5.withAlpha (0.85f));
        setColour (focusedOutlineColourId, getTheme().accent.withAlpha (0.9f));
        setColour (highlightColourId, getTheme().accent.withAlpha (0.25f));
    }

    int getValue() const { return getText().getIntValue(); }   // 0 when empty

    void setValue (int value) { setText (juce::String (value), false); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            step (key == juce::KeyPress::upKey ? 1 : -1);
            return true;
        }
        return juce::TextEditor::keyPressed (key);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const float delta = (std::abs (wheel.deltaY) > 1.0e-6f ? wheel.deltaY : wheel.deltaX) * (wheel.isReversed ? -1.0f : 1.0f);
        if (std::abs (delta) > 1.0e-6f)
            step (delta > 0.0f ? 1 : -1);
    }

private:
    void step (int delta)
    {
        setText (juce::String (juce::jlimit (1, maxValue, getValue() + delta)), true);   // calls onTextChange
        selectAll();
    }

    const int maxValue;
};

class SetBpmPopover final : public Callout::Popover
{
public:
    SetBpmPopover (const juce::String& lengthLabelIn, const SetBpmLength& initial,
                   std::function<float (float)> previewBpmIn, std::function<void (const SetBpmLength&)> onSetIn)
        : lengthLabel (lengthLabelIn), mode (initial.mode), previewBpm (std::move (previewBpmIn)), onSet (std::move (onSetIn))
    {
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);   // Tab stays in the popover

        modeControl.setOptions ({ "BAR", "NOTE" });
        modeControl.setSelectedIndex (mode == SetBpmLength::Mode::bars ? 0 : 1);
        modeControl.onChange = [this] (int index)
        {
            mode = index == 0 ? SetBpmLength::Mode::bars : SetBpmLength::Mode::note;
            showModeFields();
            focusMainField();
            updatePreview();
        };
        addAndMakeVisible (modeControl);

        sigNumerator.setValue (initial.sigNumerator);
        sigDenominator.setValue (initial.sigDenominator);
        bars.setValue (initial.bars);
        noteDenominator.setValue (initial.noteDenominator);
        int order = 1;
        for (auto* field : { &sigNumerator, &sigDenominator, &bars, &noteDenominator })
        {
            field->setExplicitFocusOrder (order++);
            field->onTextChange = [this] { updatePreview(); };
            // Return/Esc arrive inside the TextEditor's own callback: never tear the popover down there.
            juce::Component::SafePointer<SetBpmPopover> safe (this);
            field->onReturnKey = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->returnKeyPressed(); }); };
            field->onEscapeKey = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->dismiss(); }); };
            addChildComponent (*field);
        }
        sigNumerator.setTooltip ("Beats in a bar");
        sigDenominator.setTooltip ("The note that gets one beat");
        bars.setTooltip ("How many bars the length is");
        noteDenominator.setTooltip ("1/4 = quarter note, 1/6 = quarter-note triplet, 1/16 = sixteenth");

        for (auto* button : { &cancelButton, &setButton })
            addAndMakeVisible (*button);
        SettingsUi::styleButton (cancelButton);
        SettingsUi::styleButton (setButton, true);
        setButton.setTooltip ("Return");
        cancelButton.setTooltip ("Esc");
        cancelButton.onClick = [this] { dismiss(); };
        setButton.onClick = [this] { returnKeyPressed(); };

        const int height = 2 * Callout::kPadding + Callout::kHeadingH + 6 + kLineH + 4 + SettingsUi::kRowH + 6 + kFieldH + 6
                           + kLineH + 8 + Callout::kButtonH + Callout::kShadow;
        setSize (kWidth, height);
        showModeFields();
        updatePreview();
    }

    void focusMainField()
    {
        auto& field = mode == SetBpmLength::Mode::bars ? bars : noteDenominator;
        field.grabKeyboardFocus();
        field.selectAll();
    }

    void paint (juce::Graphics& g) override
    {
        Popover::paint (g);
        Callout::paintHeading (g, "SET BPM", headingArea);

        g.setColour (getTheme().text1);
        g.setFont (IntersectLookAndFeel::makeFont (8.8f));
        g.drawText (lengthLabel, labelArea, juce::Justification::centredLeft, true);

        g.setFont (IntersectLookAndFeel::makeFont (9.5f));
        for (const auto& [text, area] : fieldLabels)
            g.drawText (text, area, juce::Justification::centred, false);

        g.setColour (previewOk ? getTheme().text2 : getTheme().text0);
        g.drawText (previewText, previewArea, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getContentArea();
        headingArea = area.removeFromTop (Callout::kHeadingH);
        area.removeFromTop (6);
        labelArea = area.removeFromTop (kLineH);
        area.removeFromTop (4);
        modeControl.setBounds (area.removeFromTop (SettingsUi::kRowH));
        area.removeFromTop (6);
        layoutFields (area.removeFromTop (kFieldH));
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
        if (! previewOk)
            return;
        auto action = onSet;
        const auto picked = currentLength();
        dismissAndRun ([action, picked] { if (action != nullptr) action (picked); });
    }

    SetBpmLength currentLength() const
    {
        SetBpmLength length;
        length.mode = mode;
        length.sigNumerator = sigNumerator.getValue();
        length.sigDenominator = sigDenominator.getValue();
        length.bars = bars.getValue();
        length.noteDenominator = noteDenominator.getValue();
        return length;
    }

    void showModeFields()
    {
        const bool barMode = mode == SetBpmLength::Mode::bars;
        for (auto* field : { &sigNumerator, &sigDenominator, &bars })
            field->setVisible (barMode);
        noteDenominator.setVisible (! barMode);
        resized();
        repaint();
    }

    // BAR:  TIME SIG [4] / [4]   [1] BARS        NOTE:  1 / [4] NOTE
    void layoutFields (juce::Rectangle<int> row)
    {
        fieldLabels.clear();
        const auto label = [this, &row] (const juce::String& text, int width)
        {
            fieldLabels.push_back ({ text, row.removeFromLeft (width) });
        };
        const auto place = [&row] (NumberField& field) { field.setBounds (row.removeFromLeft (kNumberW)); };

        if (mode == SetBpmLength::Mode::bars)
        {
            label ("TIME SIG", 52);
            place (sigNumerator);
            label ("/", 14);
            place (sigDenominator);
            row.removeFromLeft (18);
            place (bars);
            label (bars.getValue() == 1 ? "BAR" : "BARS", 38);
        }
        else
        {
            label ("1 /", 26);
            place (noteDenominator);
            label ("NOTE", 40);
        }
    }

    void updatePreview()
    {
        const auto length = currentLength();
        const float lengthBars = length.inFourFourBars();
        const float bpm = (lengthBars > 0.0f && previewBpm != nullptr) ? previewBpm (lengthBars) : 0.0f;

        previewOk = false;
        if (lengthBars <= 0.0f)
            previewText = invalidNumberText (length);
        else if (! std::isfinite (bpm) || bpm <= 0.0f)
            previewText = "Nothing to measure";
        else if (bpm < kMinBpm || bpm > kMaxBpm)
            previewText = "= " + juce::String (bpm, 2) + " BPM, outside 20-999";
        else
        {
            previewText = "= " + juce::String (bpm, 2) + " BPM";
            previewOk = true;
        }
        SettingsUi::setButtonEnabled (setButton, previewOk);
        if (mode == SetBpmLength::Mode::bars)
            resized();   // "BAR" / "BARS"
        repaint();
    }

    static juce::String invalidNumberText (const SetBpmLength& length)
    {
        if (length.mode == SetBpmLength::Mode::note)
            return "Note: 1/1 to 1/" + juce::String (SetBpmLength::kMaxNoteDenominator);
        if (! inRange (length.sigNumerator, SetBpmLength::kMaxSignature) || ! inRange (length.sigDenominator, SetBpmLength::kMaxSignature))
            return "Time signature: 1 to " + juce::String (SetBpmLength::kMaxSignature) + " each";
        return "Bars: 1 to " + juce::String (SetBpmLength::kMaxBars);
    }

    juce::String lengthLabel;
    SetBpmLength::Mode mode;
    std::function<float (float)> previewBpm;
    std::function<void (const SetBpmLength&)> onSet;
    juce::String previewText;
    bool previewOk = false;

    SegmentedControl modeControl;
    NumberField sigNumerator { SetBpmLength::kMaxSignature };
    NumberField sigDenominator { SetBpmLength::kMaxSignature };
    NumberField bars { SetBpmLength::kMaxBars };
    NumberField noteDenominator { SetBpmLength::kMaxNoteDenominator };
    juce::TextButton cancelButton { "CANCEL" };
    juce::TextButton setButton { "SET" };
    juce::Rectangle<int> headingArea, labelArea, previewArea;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> fieldLabels;
};
} // namespace

void showSetBpmCallout (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                        const juce::String& lengthLabel,
                        const SetBpmLength& initial,
                        std::function<float (float)> previewBpm,
                        std::function<void (const SetBpmLength&)> onSet)
{
    auto* popover = new SetBpmPopover (lengthLabel, initial, std::move (previewBpm), std::move (onSet));
    juce::Component::SafePointer<SetBpmPopover> safe (popover);
    popover->show (anchor, anchorArea);
    if (safe != nullptr && safe->isShowing())
        safe->focusMainField();
}
