#pragma once
#include "SettingsPages.h"
#include "../../urlimport/UrlImportLogic.h"

/** Import from URL: where imports are saved, what they are saved as, which yt-dlp builds to
    follow, and the installed tools. */
class UrlImportSettingsPage final : public SettingsPage
{
public:
    UrlImportSettingsPage (IntersectProcessor& p, SettingsHost& h);
    void reload() override;
    void tick() override;
    void refreshThemeColours() override;

protected:
    void layout (SettingsUi::RowLayout& rows) override;

private:
    void chooseFolder();
    void openFolder();
    void setOutputFormat (const UrlImport::OutputFormat& format);
    /** The custom rate field's text, applied if it is a valid rate. */
    void commitCustomRate();

    SegmentedControl saveModeChips;
    PathField folderField;
    juce::TextButton chooseButton { "CHOOSE..." };
    juce::TextButton defaultButton { "DEFAULT" };
    juce::TextButton openButton { "OPEN" };
    DropdownField formatDropdown;
    SegmentedControl rateChips;
    juce::TextEditor customRateField;
    bool customRateSelected = false;   // CUSTOM chosen, even before a valid rate is typed
    bool customRateInvalid = false;
    SegmentedControl channelChips;
    juce::TextButton updateButton { "UPDATE NOW" };
    juce::TextButton removeButton { "REMOVE" };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String installedText;
    juce::String jobText;
    bool toolsInstalled = false;
    bool jobWasBusy = false;
};
