#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

// Small painted controls and a row layout shared by the settings pages.
namespace SettingsUi
{
constexpr int kLabelW = 124;   // label column left of every control
constexpr int kRowH = 20;      // one control line (chips, buttons, fields)
constexpr int kRowGap = 12;    // space between rows
constexpr int kHintH = 13;     // muted note under a control
constexpr int kSectionH = 16;
constexpr int kButtonGap = 4;

enum class TextStyle
{
    section,   // small caps group heading
    label,     // row label in the label column
    hint,      // muted note under a control
    warning,   // a note that something is wrong (missing folder, ...)
    value,     // plain body text inside the control column
    title,     // large heading
    accent,    // good news worth noticing (an update is available)
};

/** A line of text a page paints itself, positioned by RowLayout. */
struct PaintedText
{
    juce::String text;
    juce::Rectangle<int> bounds;
    TextStyle style = TextStyle::label;
};

void paintTexts (juce::Graphics& g, const std::vector<PaintedText>& texts);
float measureTextWidth (const juce::Font& font, const juce::String& text);

/** Filled settings button (accent = the one action that stands out, e.g. UPDATE). No keyboard
    focus, so Esc keeps reaching the settings view. */
void styleButton (juce::TextButton& button, bool accent = false);
/** Enables and dims: the editor's look-and-feel draws disabled buttons like enabled ones. */
void setButtonEnabled (juce::Button& button, bool enabled);
int buttonWidthFor (const juce::String& text, int minWidth = 44);
/** Lays buttons out right to left from the end of area; returns what is left of area. */
juce::Rectangle<int> placeButtonsRight (juce::Rectangle<int> area, std::initializer_list<juce::TextButton*> buttons);

/** Stacks section headings and "label | control" rows top to bottom; pages run it from resized(). */
class RowLayout
{
public:
    RowLayout (juce::Rectangle<int> area, std::vector<PaintedText>& texts);

    void section (const juce::String& title);
    /** A labelled row; returns the control area right of the label column. */
    juce::Rectangle<int> row (const juce::String& label, int height = kRowH);
    /** A row across the full width, no label. */
    juce::Rectangle<int> fullRow (int height = kRowH);
    /** A muted note under the previous row's control. */
    void hint (const juce::String& text, bool warning = false);
    void gap (int pixels) { y += pixels; }

    int getControlWidth() const noexcept { return area.getWidth() - kLabelW; }
    int getFullWidth() const noexcept    { return area.getWidth(); }
    int getBottom() const noexcept       { return y; }

private:
    juce::Rectangle<int> area;
    std::vector<PaintedText>& texts;
    int y = 0;
    juce::Rectangle<int> lastControl;
};
} // namespace SettingsUi

/** A row of chips with one selected (themes, C3/C4/C5, CPU/GPU...). Chips wrap onto more lines. */
class SegmentedControl : public juce::Component
{
public:
    void setOptions (const juce::StringArray& labels);
    /** -1 selects nothing. Does not call onChange. */
    void setSelectedIndex (int index);
    int getSelectedIndex() const noexcept { return selected; }
    int getHeightForWidth (int width) const;

    std::function<void (int)> onChange;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

private:
    std::vector<juce::Rectangle<int>> layoutChips (int width) const;
    int chipAt (juce::Point<int> position) const;

    juce::StringArray options;
    int selected = -1;
    int hovered = -1;
};

/** The current value with a chevron; a click opens a menu of the options, current one ticked.
    The menu is parented to the editor like every other menu (UI scale, issue #41). */
class DropdownField : public juce::Component
{
public:
    void setOptions (const juce::StringArray& labels);
    /** -1 shows the placeholder. Does not call onChange. */
    void setSelectedIndex (int index);
    int getSelectedIndex() const noexcept { return selected; }
    void setPlaceholder (const juce::String& text) { placeholder = text; repaint(); }

    std::function<void (int)> onChange;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    juce::StringArray options;
    juce::String placeholder;
    int selected = -1;
};

/** On/off switch with an ON / OFF label. */
class SettingsToggle : public juce::Component
{
public:
    static constexpr int kPreferredWidth = 64;

    void setOn (bool shouldBeOn);
    bool isOn() const noexcept { return on; }

    std::function<void (bool)> onChange;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    bool on = false;
};

/** [-] value [+]. The owner formats the value and decides which directions are allowed. */
class Stepper : public juce::Component
{
public:
    static constexpr int kPreferredWidth = 118;

    Stepper();
    void setValueText (const juce::String& text);
    void setCanStep (bool canGoDown, bool canGoUp);
    void refreshThemeColours();

    /** -1 or +1. */
    std::function<void (int)> onStep;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::TextButton downButton;
    juce::TextButton upButton { "+" };
    juce::String valueText;
};

/** A read-only folder path, elided at the start so the folder name stays visible. Hover shows
    the full path. A folder that doesn't exist is drawn in the warning colour. */
class PathField : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    void setPath (const juce::File& folder, const juce::String& placeholder, bool markMissing = true);
    bool isMissing() const noexcept { return missing; }

    void paint (juce::Graphics& g) override;

private:
    juce::File folder;
    juce::String placeholder;
    bool missing = false;
};

/** One downloadable item (ONNX Runtime bundle, stem model): name, status, progress and an action. */
class ItemRow : public juce::Component
{
public:
    static constexpr int kHeight = 32;

    ItemRow();
    void setContent (const juce::String& title, const juce::String& status, bool highlightStatus);
    /** Empty text hides the button. */
    void setAction (const juce::String& buttonText, bool enabled, bool accent);
    /** 0-1 shows a progress bar; below 0 hides it. */
    void setProgress (float progress);
    void refreshThemeColours();

    std::function<void()> onAction;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::TextButton actionButton;
    juce::String title;
    juce::String status;
    bool highlight = false;
    bool accentAction = false;
    float progress = -1.0f;
};
