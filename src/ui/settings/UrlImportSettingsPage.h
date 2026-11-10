#pragma once
#include "SettingsPages.h"

/** Import from URL: where imports are saved, which yt-dlp builds to follow, and the installed tools. */
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

    SegmentedControl saveModeChips;
    PathField folderField;
    juce::TextButton chooseButton { "CHOOSE..." };
    juce::TextButton defaultButton { "DEFAULT" };
    juce::TextButton openButton { "OPEN" };
    SegmentedControl channelChips;
    juce::TextButton updateButton { "UPDATE NOW" };
    juce::TextButton removeButton { "REMOVE" };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String installedText;
    juce::String jobText;
    bool toolsInstalled = false;
    bool jobWasBusy = false;
};
