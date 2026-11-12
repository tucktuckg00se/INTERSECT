#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SettingsControls.h"
#include "SettingsHost.h"
#include "../../UpdateCheck.h"
#include <functional>
#include <memory>
#include <vector>

class IntersectProcessor;

/**
    One category of the settings view. A page lays itself out top to bottom at the width it is
    given and reports the height it needs; the view scrolls it when that is taller than the view.
*/
class SettingsPage : public juce::Component
{
public:
    SettingsPage (IntersectProcessor& p, SettingsHost& h) : processor (p), host (h) {}

    /** Re-reads every value. Called when the view opens and when the page is shown. */
    virtual void reload() = 0;
    /** 10 Hz while the page is shown: picks up values that change on their own. Keep it cheap. */
    virtual void tick() {}
    virtual void refreshThemeColours() {}

    int getContentHeight() const noexcept { return contentHeight; }
    /** The page needs a different height (a hint appeared, a list grew); the view refits it. */
    std::function<void()> onContentHeightChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;

protected:
    /** Positions every control for the current width, adding labels and hints to rows. */
    virtual void layout (SettingsUi::RowLayout& rows) = 0;
    /** Texts or visibility changed: lay out again. */
    void relayout() { resized(); }

    IntersectProcessor& processor;
    SettingsHost& host;
    std::vector<SettingsUi::PaintedText> texts;

private:
    int contentHeight = 0;
};

/** UI scale, theme, note naming, update check and support links (the version is in the bottom bar). */
class GeneralSettingsPage final : public SettingsPage
{
public:
    GeneralSettingsPage (IntersectProcessor& p, SettingsHost& h);
    void reload() override;
    void tick() override;
    void refreshThemeColours() override;

protected:
    void layout (SettingsUi::RowLayout& rows) override;

private:
    void updateScale();
    /** Text and button for the update row from the shared checker's status. */
    void updateUpdateRow();
    /** Version, OS, plugin format and host for REPORT AN ISSUE's pre-filled form. */
    UpdateCheck::IssueEnvironment describeEnvironment() const;

    Stepper scaleStepper;
    DropdownField themeDropdown;
    SegmentedControl middleCChips;
    juce::TextButton updateButton;
    juce::String updateText;
    bool updateTextIsAccent = false;
    juce::TextButton discordButton { "JOIN THE DISCORD" };
    juce::TextButton coffeeButton { "BUY ME A COFFEE" };
    juce::TextButton issueButton { "REPORT AN ISSUE" };
    juce::StringArray themes;
    juce::String scaleHint;
};

/** NRPN editing from a hardware controller. */
class MidiSettingsPage final : public SettingsPage
{
public:
    MidiSettingsPage (IntersectProcessor& p, SettingsHost& h);
    void reload() override;
    void tick() override { reload(); }
    void refreshThemeColours() override;

protected:
    void layout (SettingsUi::RowLayout& rows) override;

private:
    void setChannel (int channel);

    SettingsToggle nrpnToggle;
    SettingsToggle consumeToggle;
    Stepper channelStepper;
    juce::TextButton omniButton { "OMNI" };
};

/** Where the FILES button opens the browser. */
class FilesSettingsPage final : public SettingsPage
{
public:
    FilesSettingsPage (IntersectProcessor& p, SettingsHost& h);
    void reload() override;
    void refreshThemeColours() override;

protected:
    void layout (SettingsUi::RowLayout& rows) override;

private:
    void chooseFolder();

    SegmentedControl modeChips;
    PathField folderField;
    juce::TextButton chooseButton { "CHOOSE..." };
    juce::TextButton clearButton { "CLEAR" };
    std::unique_ptr<juce::FileChooser> chooser;
};

/** Default and custom preset folders. */
class PresetsSettingsPage final : public SettingsPage
{
public:
    PresetsSettingsPage (IntersectProcessor& p, SettingsHost& h);
    void reload() override;
    void refreshThemeColours() override;

protected:
    void layout (SettingsUi::RowLayout& rows) override;

private:
    void chooseCustomFolder();

    PathField defaultField;
    PathField customField;
    juce::TextButton openDefaultButton { "OPEN" };
    juce::TextButton chooseButton { "CHOOSE..." };
    juce::TextButton clearButton { "CLEAR" };
    juce::TextButton openCustomButton { "OPEN" };
    std::unique_ptr<juce::FileChooser> chooser;
};
