#include "SettingsControls.h"
#include "../IntersectLookAndFeel.h"
#include <cmath>

namespace
{
constexpr int kChipGap = 4;
constexpr int kMinChipW = 40;

juce::Font chipFont()   { return IntersectLookAndFeel::makeFont (8.5f, true); }
}

namespace SettingsUi
{
float measureTextWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getWidth();
}

void paintTexts (juce::Graphics& g, const std::vector<PaintedText>& texts)
{
    for (const auto& t : texts)
    {
        switch (t.style)
        {
            case TextStyle::section:
                g.setColour (getTheme().text0.withAlpha (0.85f));
                g.setFont (IntersectLookAndFeel::makeFont (8.0f, true));
                break;
            case TextStyle::label:
                g.setColour (getTheme().text2.withAlpha (0.72f));
                g.setFont (IntersectLookAndFeel::makeFont (8.5f, true));
                break;
            case TextStyle::hint:
                g.setColour (getTheme().text0);
                g.setFont (IntersectLookAndFeel::makeFont (8.5f));
                break;
            case TextStyle::warning:
                g.setColour (getTheme().color5.brighter (0.1f));
                g.setFont (IntersectLookAndFeel::makeFont (8.5f));
                break;
            case TextStyle::value:
                g.setColour (getTheme().text1.brighter (0.3f));
                g.setFont (IntersectLookAndFeel::makeFont (9.5f));
                break;
            case TextStyle::title:
                g.setColour (getTheme().text2);
                g.setFont (IntersectLookAndFeel::makeFont (14.0f, true));
                break;
            case TextStyle::accent:
                g.setColour (getTheme().accent);
                g.setFont (IntersectLookAndFeel::makeFont (9.5f, true));
                break;
        }
        g.drawText (t.text, t.bounds, juce::Justification::centredLeft, true);
    }
}

void styleButton (juce::TextButton& button, bool accent)
{
    button.setWantsKeyboardFocus (false);
    button.setColour (juce::TextButton::buttonColourId,
                      accent ? getTheme().accent : getTheme().surface4.withAlpha (0.95f));
    const auto text = accent ? getTheme().surface0 : getTheme().text2.withAlpha (0.88f);
    button.setColour (juce::TextButton::textColourOnId, text);
    button.setColour (juce::TextButton::textColourOffId, text);
}

void setButtonEnabled (juce::Button& button, bool enabled)
{
    button.setEnabled (enabled);
    button.setAlpha (enabled ? 1.0f : 0.4f);
}

int buttonWidthFor (const juce::String& text, int minWidth)
{
    return juce::jmax (minWidth, juce::roundToInt (measureTextWidth (IntersectLookAndFeel::makeFont (8.8f), text)) + 22);
}

juce::Rectangle<int> placeButtonsRight (juce::Rectangle<int> area, std::initializer_list<juce::TextButton*> buttons)
{
    // The list reads left to right, like the buttons on screen.
    std::vector<juce::TextButton*> ordered (buttons);
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it)
    {
        auto* button = *it;
        if (! button->isVisible())
            continue;
        button->setBounds (area.removeFromRight (buttonWidthFor (button->getButtonText())));
        area.removeFromRight (kButtonGap);
    }
    return area;
}

RowLayout::RowLayout (juce::Rectangle<int> areaIn, std::vector<PaintedText>& textsIn)
    : area (areaIn), texts (textsIn), y (areaIn.getY())
{
    texts.clear();
}

void RowLayout::section (const juce::String& title)
{
    if (y > area.getY())
        y += 6;   // a little extra air above every heading but the first
    texts.push_back ({ title.toUpperCase(), { area.getX(), y, area.getWidth(), kSectionH }, TextStyle::section });
    y += kSectionH + 6;
}

juce::Rectangle<int> RowLayout::row (const juce::String& label, int height)
{
    texts.push_back ({ label.toUpperCase(), { area.getX(), y, kLabelW - 8, kRowH }, TextStyle::label });
    lastControl = { area.getX() + kLabelW, y, getControlWidth(), height };
    y += height + kRowGap;
    return lastControl;
}

juce::Rectangle<int> RowLayout::fullRow (int height)
{
    lastControl = { area.getX(), y, area.getWidth(), height };
    y += height + kRowGap;
    return lastControl;
}

void RowLayout::hint (const juce::String& text, bool warning)
{
    if (text.isEmpty())
        return;

    y += 3 - kRowGap;   // tuck under the control it explains
    texts.push_back ({ text, { lastControl.getX(), y, lastControl.getWidth(), kHintH },
                       warning ? TextStyle::warning : TextStyle::hint });
    y += kHintH + kRowGap;
}
} // namespace SettingsUi

//==============================================================================

void SegmentedControl::setOptions (const juce::StringArray& labels)
{
    if (labels == options)
        return;
    options = labels;
    if (selected >= options.size())
        selected = -1;
    hovered = -1;
    repaint();
}

void SegmentedControl::setSelectedIndex (int index)
{
    index = juce::isPositiveAndBelow (index, options.size()) ? index : -1;
    if (index != selected)
    {
        selected = index;
        repaint();
    }
}

std::vector<juce::Rectangle<int>> SegmentedControl::layoutChips (int width) const
{
    std::vector<juce::Rectangle<int>> chips;
    const auto font = chipFont();
    int x = 0, y = 0;
    for (const auto& label : options)
    {
        const int w = juce::jmin (juce::jmax (1, width),
                                  juce::jmax (kMinChipW, juce::roundToInt (SettingsUi::measureTextWidth (font, label)) + 20));
        if (x > 0 && x + w > width)
        {
            x = 0;
            y += SettingsUi::kRowH + kChipGap;
        }
        chips.push_back ({ x, y, w, SettingsUi::kRowH });
        x += w + kChipGap;
    }
    return chips;
}

int SegmentedControl::getHeightForWidth (int width) const
{
    const auto chips = layoutChips (width);
    return chips.empty() ? SettingsUi::kRowH : chips.back().getBottom();
}

int SegmentedControl::chipAt (juce::Point<int> position) const
{
    const auto chips = layoutChips (getWidth());
    for (int i = 0; i < (int) chips.size(); ++i)
        if (chips[(size_t) i].contains (position))
            return i;
    return -1;
}

void SegmentedControl::paint (juce::Graphics& g)
{
    const auto chips = layoutChips (getWidth());
    g.setFont (chipFont());

    for (int i = 0; i < (int) chips.size(); ++i)
    {
        const auto chip = chips[(size_t) i].toFloat().reduced (0.5f);
        const bool isSelected = i == selected;
        const bool isHovered = i == hovered && isEnabled();

        auto fill = isSelected ? getTheme().surface3.interpolatedWith (getTheme().accent, 0.22f)
                               : (isHovered ? getTheme().surface4 : getTheme().surface3);
        g.setColour (fill);
        g.fillRoundedRectangle (chip, 3.0f);

        if (isSelected)
        {
            g.setColour (getTheme().accent.withAlpha (0.7f));
            g.drawRoundedRectangle (chip, 3.0f, 1.0f);
        }

        g.setColour (isSelected ? getTheme().text2 : getTheme().text2.withAlpha (isHovered ? 0.9f : 0.68f));
        g.drawText (options[i], chip.reduced (6.0f, 0.0f), juce::Justification::centred, true);
    }
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    const int index = chipAt (e.getPosition());
    if (index < 0 || index == selected)
        return;

    selected = index;
    repaint();
    if (onChange != nullptr)
        onChange (index);
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const int index = chipAt (e.getPosition());
    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0)
    {
        hovered = -1;
        repaint();
    }
}

//==============================================================================

void DropdownField::setOptions (const juce::StringArray& labels)
{
    if (labels == options)
        return;
    options = labels;
    if (selected >= options.size())
        selected = -1;
    repaint();
}

void DropdownField::setSelectedIndex (int index)
{
    index = juce::isPositiveAndBelow (index, options.size()) ? index : -1;
    if (index != selected)
    {
        selected = index;
        repaint();
    }
}

void DropdownField::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (isMouseOver() ? getTheme().surface4 : getTheme().surface3);
    g.fillRoundedRectangle (box, 3.0f);

    auto area = getLocalBounds().reduced (8, 0);
    const auto chevronArea = area.removeFromRight (10).toFloat().withSizeKeepingCentre (8.0f, 4.0f);
    juce::Path chevron;
    chevron.startNewSubPath (chevronArea.getTopLeft());
    chevron.lineTo (chevronArea.getCentreX(), chevronArea.getBottom());
    chevron.lineTo (chevronArea.getTopRight());
    g.setColour (getTheme().text1);
    g.strokePath (chevron, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    area.removeFromRight (6);
    const bool hasValue = juce::isPositiveAndBelow (selected, options.size());
    g.setColour (hasValue ? getTheme().text2 : getTheme().text0);
    g.setFont (IntersectLookAndFeel::makeFont (9.5f));
    g.drawText (hasValue ? options[selected] : placeholder, area, juce::Justification::centredLeft, true);
}

void DropdownField::mouseDown (const juce::MouseEvent&)
{
    if (options.isEmpty())
        return;

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    for (int i = 0; i < options.size(); ++i)
        menu.addItem (i + 1, options[i], true, i == selected);

    menu.showMenuAsync (IntersectLookAndFeel::makeEditorMenuOptions (*this, getWidth(), 22).withTargetComponent (this),
        [safe = juce::Component::SafePointer<DropdownField> (this)] (int result)
        {
            if (safe == nullptr || result <= 0 || result - 1 == safe->selected)
                return;

            safe->selected = result - 1;
            safe->repaint();
            if (safe->onChange != nullptr)
                safe->onChange (safe->selected);
        });
}

//==============================================================================

void SettingsToggle::setOn (bool shouldBeOn)
{
    if (on != shouldBeOn)
    {
        on = shouldBeOn;
        repaint();
    }
}

void SettingsToggle::paint (juce::Graphics& g)
{
    const auto track = juce::Rectangle<float> (0.0f, (float) (getHeight() - 14) * 0.5f, 28.0f, 14.0f);
    g.setColour (on ? getTheme().accent.withAlpha (0.85f) : getTheme().surface4);
    g.fillRoundedRectangle (track, 7.0f);

    const float knobX = on ? track.getRight() - 12.0f : track.getX() + 2.0f;
    g.setColour (on ? getTheme().surface0 : getTheme().text1);
    g.fillEllipse (knobX, track.getY() + 2.0f, 10.0f, 10.0f);

    g.setColour (on ? getTheme().text2 : getTheme().text0);
    g.setFont (IntersectLookAndFeel::makeFont (8.5f, true));
    g.drawText (on ? "ON" : "OFF", getLocalBounds().withTrimmedLeft (36), juce::Justification::centredLeft, false);
}

void SettingsToggle::mouseDown (const juce::MouseEvent&)
{
    on = ! on;
    repaint();
    if (onChange != nullptr)
        onChange (on);
}

//==============================================================================

Stepper::Stepper()
{
    downButton.setButtonText (juce::String::charToString (0x2212));   // minus sign
    for (auto* button : { &downButton, &upButton })
        addAndMakeVisible (*button);

    downButton.onClick = [this] { if (onStep != nullptr) onStep (-1); };
    upButton.onClick = [this] { if (onStep != nullptr) onStep (1); };
    refreshThemeColours();
}

void Stepper::setValueText (const juce::String& text)
{
    if (text != valueText)
    {
        valueText = text;
        repaint();
    }
}

void Stepper::setCanStep (bool canGoDown, bool canGoUp)
{
    SettingsUi::setButtonEnabled (downButton, canGoDown);
    SettingsUi::setButtonEnabled (upButton, canGoUp);
}

void Stepper::refreshThemeColours()
{
    SettingsUi::styleButton (downButton);
    SettingsUi::styleButton (upButton);
    repaint();
}

void Stepper::paint (juce::Graphics& g)
{
    const auto valueArea = getLocalBounds().reduced (downButton.getWidth() + 3, 0).toFloat().reduced (0.0f, 0.5f);
    g.setColour (getTheme().surface3);
    g.fillRoundedRectangle (valueArea, 3.0f);
    g.setColour (getTheme().text2);
    g.setFont (IntersectLookAndFeel::makeFont (9.5f));
    g.drawText (valueText, valueArea, juce::Justification::centred, true);
}

void Stepper::resized()
{
    auto area = getLocalBounds();
    downButton.setBounds (area.removeFromLeft (24));
    upButton.setBounds (area.removeFromRight (24));
}

//==============================================================================

void PathField::setPath (const juce::File& folderIn, const juce::String& placeholderIn, bool markMissing)
{
    const bool nowMissing = markMissing && folderIn != juce::File() && ! folderIn.isDirectory();
    if (folderIn == folder && placeholderIn == placeholder && nowMissing == missing)
        return;

    folder = folderIn;
    placeholder = placeholderIn;
    missing = nowMissing;
    setTooltip (folder != juce::File() ? folder.getFullPathName() : juce::String());
    repaint();
}

void PathField::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (getTheme().surface1.withAlpha (0.92f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (getTheme().surface4.withAlpha (0.92f));
    g.drawRoundedRectangle (box, 3.0f, 1.0f);

    const auto textArea = getLocalBounds().reduced (7, 0);
    const auto font = IntersectLookAndFeel::makeFont (9.0f);
    g.setFont (font);

    if (folder == juce::File())
    {
        g.setColour (getTheme().text0);
        g.drawText (placeholder, textArea, juce::Justification::centredLeft, true);
        return;
    }

    // Drop leading characters until it fits, so the end of the path (the folder name) stays visible.
    auto text = folder.getFullPathName();
    if (SettingsUi::measureTextWidth (font, text) > (float) textArea.getWidth())
    {
        const juce::String ellipsis ("...");
        while (text.length() > 1 && SettingsUi::measureTextWidth (font, ellipsis + text) > (float) textArea.getWidth())
            text = text.substring (1);
        text = ellipsis + text;
    }

    g.setColour (missing ? getTheme().color5.brighter (0.1f) : getTheme().text2.withAlpha (0.88f));
    g.drawText (text, textArea, juce::Justification::centredLeft, false);
}

//==============================================================================

ItemRow::ItemRow()
{
    addChildComponent (actionButton);
    actionButton.onClick = [this] { if (onAction != nullptr) onAction(); };
    refreshThemeColours();
}

void ItemRow::setContent (const juce::String& newTitle, const juce::String& newStatus, bool highlightStatus)
{
    if (newTitle == title && newStatus == status && highlightStatus == highlight)
        return;

    title = newTitle;
    status = newStatus;
    highlight = highlightStatus;
    repaint();
}

void ItemRow::setAction (const juce::String& buttonText, bool enabled, bool accent)
{
    const bool visible = buttonText.isNotEmpty();
    const bool layoutChanged = visible != actionButton.isVisible() || buttonText != actionButton.getButtonText();

    actionButton.setButtonText (buttonText);
    actionButton.setVisible (visible);
    SettingsUi::setButtonEnabled (actionButton, enabled);
    if (accent != accentAction)
    {
        accentAction = accent;
        SettingsUi::styleButton (actionButton, accentAction);
    }

    if (layoutChanged)
        resized();
}

void ItemRow::setProgress (float newProgress)
{
    if (std::abs (newProgress - progress) > 0.001f)
    {
        progress = newProgress;
        repaint();
    }
}

void ItemRow::refreshThemeColours()
{
    SettingsUi::styleButton (actionButton, accentAction);
    repaint();
}

void ItemRow::paint (juce::Graphics& g)
{
    g.setColour (getTheme().surface3.withAlpha (0.5f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.0f, 1.0f), 3.0f);

    auto content = getLocalBounds().reduced (10, 4);
    if (actionButton.isVisible())
        content.setRight (actionButton.getX() - 10);

    g.setColour (getTheme().text2.withAlpha (0.9f));
    g.setFont (IntersectLookAndFeel::makeFont (9.5f));
    g.drawText (title, content.removeFromTop (13), juce::Justification::centredLeft, true);

    auto statusLine = content;
    if (progress >= 0.0f)
    {
        // Status on the left, a thin bar filling the rest of the line.
        const auto bar = statusLine.removeFromRight (juce::jmin (160, statusLine.getWidth() / 2))
                             .withSizeKeepingCentre (juce::jmin (160, statusLine.getWidth() / 2), 4).toFloat();
        g.setColour (getTheme().surface4);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (getTheme().accent.withAlpha (0.8f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress)), 2.0f);
        statusLine.removeFromRight (8);
    }

    g.setColour (highlight ? getTheme().accent : getTheme().text0);
    g.setFont (IntersectLookAndFeel::makeFont (8.0f));
    g.drawText (status, statusLine, juce::Justification::centredLeft, true);
}

void ItemRow::resized()
{
    if (actionButton.isVisible())
    {
        const int w = SettingsUi::buttonWidthFor (actionButton.getButtonText(), 64);
        actionButton.setBounds (getLocalBounds().reduced (6, 0).removeFromRight (w)
                                    .withSizeKeepingCentre (w, SettingsUi::kRowH));
    }
    repaint();
}
