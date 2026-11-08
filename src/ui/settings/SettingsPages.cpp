#include "SettingsPages.h"
#include "../IntersectLookAndFeel.h"
#include "../../AppFiles.h"
#include "../../PluginProcessor.h"
#include <cmath>

using namespace SettingsUi;

namespace
{
constexpr float kMinUiScale = 0.5f;
constexpr float kMaxUiScale = 3.0f;
constexpr float kUiScaleStep = 0.25f;

juce::String formatScale (float scale)
{
    return juce::String (scale, 2) + "x";
}

juce::String formatChannel (int channel)
{
    return channel == 0 ? juce::String ("OMNI") : "CH " + juce::String (channel);
}

/** Opens a folder in the system file manager; creates it first if asked. */
void openFolder (IntersectProcessor& processor, const juce::File& dir, bool createIfMissing)
{
    if (createIfMissing && ! dir.createDirectory().wasOk())
    {
        processor.showTransientStatusMessage ("Couldn't create " + dir.getFullPathName(), true);
        return;
    }
    if (dir.isDirectory())
        dir.startAsProcess();
}
} // namespace

//==============================================================================

void SettingsPage::paint (juce::Graphics& g)
{
    paintTexts (g, texts);
}

void SettingsPage::resized()
{
    if (getWidth() <= 0)
        return;   // not fitted into the view yet

    RowLayout rows (getLocalBounds().reduced (18, 0).withTrimmedTop (12), texts);
    layout (rows);
    repaint();

    const int height = rows.getBottom() + 4;
    if (height != contentHeight)
    {
        contentHeight = height;
        if (onContentHeightChanged != nullptr)
            onContentHeightChanged();
    }
}

//==============================================================================
// GENERAL

GeneralSettingsPage::GeneralSettingsPage (IntersectProcessor& p, SettingsHost& h)
    : SettingsPage (p, h)
{
    for (auto* c : std::initializer_list<juce::Component*> { &scaleStepper, &themeDropdown, &middleCChips,
                                                             &updateButton, &discordButton, &coffeeButton })
        addAndMakeVisible (*c);

    updateButton.onClick = [this]
    {
        auto& checker = processor.getUpdateChecker();
        const auto status = checker.getStatus();
        if (status.state == UpdateChecker::State::updateAvailable)
            juce::URL (status.pageUrl.isNotEmpty() ? status.pageUrl : juce::String (UpdateCheck::kReleasesPageUrl))
                .launchInDefaultBrowser();
        else
            checker.checkIfStale (JucePlugin_VersionString, true);
        updateUpdateRow();
    };

    scaleStepper.onStep = [this] (int direction)
    {
        host.adjustUiScale (kUiScaleStep * (float) direction);
        updateScale();
    };

    themeDropdown.onChange = [this] (int index)
    {
        if (juce::isPositiveAndBelow (index, themes.size()))
            host.applyTheme (themes[index]);   // also saves settings and recolours this view
    };

    middleCChips.setOptions ({ "C3", "C4", "C5" });
    middleCChips.onChange = [this] (int index) { host.setMiddleCOctave (3 + index); };

    discordButton.onClick = [] { juce::URL ("https://discord.gg/yhNReDnQvE").launchInDefaultBrowser(); };
    coffeeButton.onClick = [] { juce::URL ("https://buymeacoffee.com/tucktuckgoose").launchInDefaultBrowser(); };
    refreshThemeColours();
}

void GeneralSettingsPage::reload()
{
    themes = host.getAvailableThemes();
    themes.sortNatural();
    themeDropdown.setOptions (themes);
    themeDropdown.setSelectedIndex (themes.indexOf (getTheme().name));
    middleCChips.setSelectedIndex (host.getMiddleCOctave() - 3);
    updateScale();
    updateUpdateRow();
    relayout();
}

void GeneralSettingsPage::tick()
{
    updateScale();       // the effective scale changes when the window moves to another display
    updateUpdateRow();   // the check runs in the background
}

void GeneralSettingsPage::updateUpdateRow()
{
    const auto status = processor.getUpdateChecker().getStatus();
    juce::String text, buttonText;
    bool accent = false;

    switch (status.state)
    {
        case UpdateChecker::State::idle:
            text = "Not checked yet";
            buttonText = "CHECK NOW";
            break;
        case UpdateChecker::State::checking:
            text = "Checking for updates...";
            break;
        case UpdateChecker::State::upToDate:
            text = "You have the latest version (" + status.currentVersion + ")";
            buttonText = "CHECK AGAIN";
            break;
        case UpdateChecker::State::updateAvailable:
            text = "INTERSECT " + status.latestVersion + " is available (you have " + status.currentVersion + ")";
            buttonText = "VIEW RELEASE";
            accent = true;
            break;
        case UpdateChecker::State::failed:
            text = "Couldn't reach GitHub to check for updates";
            buttonText = "TRY AGAIN";
            break;
    }

    if (text == updateText && buttonText == updateButton.getButtonText() && accent == updateTextIsAccent)
        return;

    updateText = text;
    updateTextIsAccent = accent;
    updateButton.setButtonText (buttonText);
    updateButton.setVisible (buttonText.isNotEmpty());
    styleButton (updateButton, accent);
    updateButton.setTooltip (accent ? "Open the release page in your browser" : "Ask GitHub for the newest release");
    relayout();
}

void GeneralSettingsPage::updateScale()
{
    const float desired = host.getDesiredUiScale();
    const float effective = host.getEffectiveUiScale();
    scaleStepper.setValueText (formatScale (desired));
    scaleStepper.setCanStep (desired > kMinUiScale + 0.001f, desired < kMaxUiScale - 0.001f);

    const auto hint = effective < desired - 0.005f
        ? "Shown at " + formatScale (effective) + " so the window fits this display"
        : juce::String ("Shortcut: Ctrl/Cmd and + or -");
    if (hint != scaleHint)
    {
        scaleHint = hint;
        relayout();
    }
}

void GeneralSettingsPage::refreshThemeColours()
{
    scaleStepper.refreshThemeColours();
    styleButton (discordButton);
    styleButton (coffeeButton);
    styleButton (updateButton, updateTextIsAccent);
    // The theme may have been switched from here: keep the dropdown on the active theme.
    themeDropdown.setSelectedIndex (themes.indexOf (getTheme().name));
    repaint();
}

void GeneralSettingsPage::layout (RowLayout& rows)
{
    scaleStepper.setBounds (rows.row ("UI scale").removeFromLeft (Stepper::kPreferredWidth));
    rows.hint (scaleHint);

    themeDropdown.setBounds (rows.row ("Theme").removeFromLeft (200));
    rows.hint ("Themes are .intersectstyle files in the themes folder (OPEN SETTINGS FOLDER below)");

    middleCChips.setBounds (rows.row ("Middle C").removeFromLeft (140));
    rows.hint ("The name shown for MIDI note 60 in note fields");

    auto updateRow = rows.row ("Updates");
    const auto textArea = placeButtonsRight (updateRow, { &updateButton });
    texts.push_back ({ updateText, textArea.withTrimmedRight (8), updateTextIsAccent ? TextStyle::accent : TextStyle::value });
    rows.hint ("Checked on GitHub when you open settings, at most every 6 hours");

    rows.section ("Support");
    auto support = rows.row ("Community");
    discordButton.setBounds (support.removeFromLeft (buttonWidthFor (discordButton.getButtonText(), 120)));
    support.removeFromLeft (6);
    coffeeButton.setBounds (support.removeFromLeft (buttonWidthFor (coffeeButton.getButtonText(), 120)));
    rows.hint ("Questions, bug reports and new releases are on the Discord");
}

//==============================================================================
// MIDI

MidiSettingsPage::MidiSettingsPage (IntersectProcessor& p, SettingsHost& h)
    : SettingsPage (p, h)
{
    for (auto* c : std::initializer_list<juce::Component*> { &nrpnToggle, &consumeToggle, &channelStepper, &omniButton })
        addAndMakeVisible (*c);

    nrpnToggle.onChange = [this] (bool on)
    {
        processor.midiEditState.enabled.store (on, std::memory_order_release);
        host.persistSettings();
    };
    consumeToggle.onChange = [this] (bool on)
    {
        processor.midiEditState.consumeMidiEditCc.store (on, std::memory_order_relaxed);
        host.persistSettings();
    };
    channelStepper.onStep = [this] (int direction)
    {
        setChannel (processor.midiEditState.channel.load (std::memory_order_relaxed) + direction);
    };
    omniButton.setTooltip ("Listen on every MIDI channel");
    omniButton.onClick = [this] { setChannel (0); };

    refreshThemeColours();
}

void MidiSettingsPage::setChannel (int channel)
{
    processor.midiEditState.channel.store (juce::jlimit (0, 16, channel), std::memory_order_relaxed);
    host.persistSettings();
    reload();
}

void MidiSettingsPage::reload()
{
    nrpnToggle.setOn (processor.midiEditState.enabled.load (std::memory_order_relaxed));
    consumeToggle.setOn (processor.midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed));

    const int channel = processor.midiEditState.channel.load (std::memory_order_relaxed);
    channelStepper.setValueText (formatChannel (channel));
    channelStepper.setCanStep (channel > 0, channel < 16);
    setButtonEnabled (omniButton, channel != 0);
}

void MidiSettingsPage::refreshThemeColours()
{
    channelStepper.refreshThemeColours();
    styleButton (omniButton);
    repaint();
}

void MidiSettingsPage::layout (RowLayout& rows)
{
    rows.section ("NRPN controller editing");

    nrpnToggle.setBounds (rows.row ("NRPN editing").removeFromLeft (SettingsToggle::kPreferredWidth));
    rows.hint ("Endless encoders sending NRPN (MSB 64) zoom and nudge the selected slice's start and end");

    consumeToggle.setBounds (rows.row ("Consume NRPN CCs").removeFromLeft (SettingsToggle::kPreferredWidth));
    rows.hint ("Keep the NRPN edit CCs from passing through to instruments after INTERSECT");

    auto channelRow = rows.row ("Channel");
    channelStepper.setBounds (channelRow.removeFromLeft (Stepper::kPreferredWidth));
    channelRow.removeFromLeft (8);
    omniButton.setBounds (channelRow.removeFromLeft (buttonWidthFor (omniButton.getButtonText())));
    rows.hint ("The MIDI channel NRPN edits are read from");
}

//==============================================================================
// FILES

FilesSettingsPage::FilesSettingsPage (IntersectProcessor& p, SettingsHost& h)
    : SettingsPage (p, h)
{
    for (auto* c : std::initializer_list<juce::Component*> { &modeChips, &folderField, &chooseButton, &clearButton })
        addAndMakeVisible (*c);

    modeChips.setOptions ({ "LAST USED", "CHOSEN FOLDER" });
    modeChips.onChange = [this] (int index)
    {
        const auto mode = index == 1 ? Browser::DefaultFolderMode::chosenFolder : Browser::DefaultFolderMode::lastUsed;
        host.setBrowserDefaultFolderMode (mode);
        reload();
        if (mode == Browser::DefaultFolderMode::chosenFolder && host.getBrowserDefaultFolder() == juce::File())
            chooseFolder();   // nothing to open yet: ask right away
    };

    chooseButton.setTooltip ("Pick the default folder FILES opens in");
    chooseButton.onClick = [this] { chooseFolder(); };
    clearButton.setTooltip ("Forget the default folder and go back to the last folder used");
    clearButton.onClick = [this]
    {
        host.setBrowserDefaultFolder ({});
        host.setBrowserDefaultFolderMode (Browser::DefaultFolderMode::lastUsed);
        reload();
    };

    refreshThemeColours();
}

void FilesSettingsPage::chooseFolder()
{
    const auto current = host.getBrowserDefaultFolder();
    chooser = std::make_unique<juce::FileChooser> (
        "Choose the Default Folder for FILES",
        current.isDirectory() ? current : juce::File::getSpecialLocation (juce::File::userHomeDirectory));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<FilesSettingsPage> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;

            const auto folder = fc.getResult();
            if (folder.isDirectory())
            {
                safe->host.setBrowserDefaultFolder (folder);
                safe->host.setBrowserDefaultFolderMode (Browser::DefaultFolderMode::chosenFolder);
            }
            safe->reload();
        });
}

void FilesSettingsPage::reload()
{
    const auto folder = host.getBrowserDefaultFolder();
    modeChips.setSelectedIndex (host.getBrowserDefaultFolderMode() == Browser::DefaultFolderMode::chosenFolder ? 1 : 0);
    folderField.setPath (folder, "No folder chosen");
    setButtonEnabled (clearButton, folder != juce::File());
    relayout();
}

void FilesSettingsPage::refreshThemeColours()
{
    styleButton (chooseButton);
    styleButton (clearButton);
    repaint();
}

void FilesSettingsPage::layout (RowLayout& rows)
{
    const bool chosenMode = host.getBrowserDefaultFolderMode() == Browser::DefaultFolderMode::chosenFolder;
    const auto folder = host.getBrowserDefaultFolder();

    rows.section ("FILES button");

    modeChips.setBounds (rows.row ("Default folder").removeFromLeft (220));
    rows.hint (chosenMode ? "FILES opens in the folder below every time; Back returns to where you were"
                          : "FILES opens where you last browsed in any INTERSECT, also after a restart");

    auto folderRow = rows.row ("Folder");
    folderField.setBounds (placeButtonsRight (folderRow, { &chooseButton, &clearButton }).withTrimmedRight (4));

    if (folder == juce::File())
        rows.hint (chosenMode ? "No folder chosen yet, so FILES opens where you last browsed"
                              : "Tip: right-click a folder in the browser and choose Set as Default Folder",
                   chosenMode);
    else if (folderField.isMissing())
        rows.hint ("Folder not found, so FILES opens where you last browsed", true);
    else if (! chosenMode)
        rows.hint ("Used when CHOSEN FOLDER is selected");
    else
        rows.hint ("Right-click a folder in the browser to change it quickly");

    texts.push_back ({ "Only the FILES button uses this: SAVE shows the new preset, RELINK starts where the file was",
                       rows.fullRow (kHintH), TextStyle::hint });
}

//==============================================================================
// PRESETS

PresetsSettingsPage::PresetsSettingsPage (IntersectProcessor& p, SettingsHost& h)
    : SettingsPage (p, h)
{
    for (auto* c : std::initializer_list<juce::Component*> { &defaultField, &customField, &openDefaultButton,
                                                             &chooseButton, &clearButton, &openCustomButton })
        addAndMakeVisible (*c);

    openDefaultButton.setTooltip ("Show in the file manager");
    openDefaultButton.onClick = [this] { openFolder (processor, AppFiles::getPresetsDir(), true); };

    chooseButton.onClick = [this] { chooseCustomFolder(); };
    clearButton.setTooltip ("Stop listing the custom folder (its presets stay on disk)");
    clearButton.onClick = [this]
    {
        host.setCustomPresetsFolder ({});
        reload();
    };
    openCustomButton.setTooltip ("Show in the file manager");
    openCustomButton.onClick = [this] { openFolder (processor, host.getCustomPresetsFolder(), false); };

    refreshThemeColours();
}

void PresetsSettingsPage::chooseCustomFolder()
{
    const auto current = host.getCustomPresetsFolder();
    chooser = std::make_unique<juce::FileChooser> (
        "Select Custom Presets Folder",
        current.isDirectory() ? current : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<PresetsSettingsPage> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;

            const auto folder = fc.getResult();
            if (folder.isDirectory())
                safe->host.setCustomPresetsFolder (folder);
            safe->reload();
        });
}

void PresetsSettingsPage::reload()
{
    const auto custom = host.getCustomPresetsFolder();
    defaultField.setPath (AppFiles::getPresetsDir(), {}, false);   // created on first use
    customField.setPath (custom, "None");
    setButtonEnabled (clearButton, custom != juce::File());
    setButtonEnabled (openCustomButton, custom.isDirectory());
    relayout();
}

void PresetsSettingsPage::refreshThemeColours()
{
    for (auto* button : { &openDefaultButton, &chooseButton, &clearButton, &openCustomButton })
        styleButton (*button);
    repaint();
}

void PresetsSettingsPage::layout (RowLayout& rows)
{
    rows.section ("Preset library");

    defaultField.setBounds (placeButtonsRight (rows.row ("Default folder"), { &openDefaultButton }).withTrimmedRight (4));
    rows.hint ("SAVE stores kits here, or in the presets folder you are browsing");

    customField.setBounds (placeButtonsRight (rows.row ("Custom folder"), { &chooseButton, &clearButton, &openCustomButton })
                               .withTrimmedRight (4));
    if (customField.isMissing())
        rows.hint ("Folder not found", true);
    else
        rows.hint ("An extra presets folder, listed under PRESETS in the browser sidebar");
}
