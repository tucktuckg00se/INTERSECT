#include "UrlImportSettingsPage.h"
#include "../ConfirmCallout.h"
#include "../IntersectLookAndFeel.h"
#include "../../PluginProcessor.h"

using namespace SettingsUi;

namespace
{
juce::String separatorDot()
{
    return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
}
} // namespace

UrlImportSettingsPage::UrlImportSettingsPage (IntersectProcessor& p, SettingsHost& h)
    : SettingsPage (p, h)
{
    for (auto* c : std::initializer_list<juce::Component*> { &saveModeChips, &folderField, &chooseButton, &defaultButton,
                                                             &openButton, &channelChips, &updateButton, &removeButton })
        addAndMakeVisible (*c);

    saveModeChips.setOptions ({ "TO THE FOLDER", "ASK EACH TIME" });
    saveModeChips.onChange = [this] (int index)
    {
        auto settings = processor.getUrlImportSettings();
        settings.askEachTime = index == 1;
        processor.setUrlImportSettings (settings);
        host.persistSettings();
        reload();
    };

    chooseButton.setTooltip ("Pick the folder imports are saved to");
    chooseButton.onClick = [this] { chooseFolder(); };
    defaultButton.setTooltip ("Save to Music/INTERSECT Downloads");
    defaultButton.onClick = [this]
    {
        auto settings = processor.getUrlImportSettings();
        settings.folder = juce::File();
        processor.setUrlImportSettings (settings);
        host.persistSettings();
        reload();
    };
    openButton.setTooltip ("Show in the file manager");
    openButton.onClick = [this] { openFolder(); };

    channelChips.setOptions ({ "STABLE", "NIGHTLY" });
    channelChips.onChange = [this] (int index)
    {
        auto settings = processor.getUrlImportSettings();
        settings.channel = index == 1 ? UrlImport::Channel::nightly : UrlImport::Channel::stable;
        processor.setUrlImportSettings (settings);
        host.persistSettings();
        reload();
    };

    updateButton.setTooltip ("Check for a newer yt-dlp and audio tools now");
    updateButton.onClick = [this]
    {
        processor.startUrlToolsUpdate();
        tick();
    };
    removeButton.setTooltip ("Delete the downloaded tools; they download again on the next import");
    removeButton.onClick = [this]
    {
        showConfirmCallout (removeButton, removeButton.getLocalBounds(),
                            "Delete yt-dlp and the audio tools? They download again on the next import.",
                            "REMOVE",
                            [safe = juce::Component::SafePointer<UrlImportSettingsPage> (this)]
                            {
                                if (safe != nullptr)
                                {
                                    safe->processor.startUrlToolsRemoval();
                                    safe->tick();
                                }
                            });
    };

    refreshThemeColours();
}

void UrlImportSettingsPage::chooseFolder()
{
    const auto current = processor.getResolvedUrlImportFolder();
    chooser = std::make_unique<juce::FileChooser> (
        "Choose the Folder for Imports",
        current.isDirectory() ? current : juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<UrlImportSettingsPage> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;

            const auto folder = fc.getResult();
            if (folder.isDirectory())
            {
                auto settings = safe->processor.getUrlImportSettings();
                settings.folder = folder;
                safe->processor.setUrlImportSettings (settings);
                safe->host.persistSettings();
            }
            safe->reload();
        });
}

void UrlImportSettingsPage::openFolder()
{
    const auto folder = processor.getResolvedUrlImportFolder();
    if (! folder.createDirectory().wasOk())
    {
        processor.showTransientStatusMessage ("Couldn't create " + folder.getFullPathName(), true);
        return;
    }
    folder.startAsProcess();
}

void UrlImportSettingsPage::reload()
{
    const auto& settings = processor.getUrlImportSettings();
    saveModeChips.setSelectedIndex (settings.askEachTime ? 1 : 0);
    // The default folder doesn't exist until the first import creates it: not a warning.
    folderField.setPath (processor.getResolvedUrlImportFolder(), {}, settings.folder != juce::File());
    setButtonEnabled (defaultButton, settings.folder != juce::File());
    channelChips.setSelectedIndex (settings.channel == UrlImport::Channel::nightly ? 1 : 0);

    const auto tools = processor.getUrlImportJob().readInstalled();
    toolsInstalled = tools.hasYtDlp() || tools.hasMediaTools();
    if (! toolsInstalled)
    {
        installedText = "Not installed yet: they download on the first import";
    }
    else
    {
        installedText = tools.hasYtDlp()
                          ? "yt-dlp " + tools.ytDlpVersion + (tools.ytDlpChannel == UrlImport::Channel::nightly ? " (nightly)" : "")
                          : juce::String ("yt-dlp not installed");
        installedText << separatorDot()
                      << (tools.hasMediaTools() ? "ffmpeg " + tools.ffmpegVersion + separatorDot() + "QuickJS " + tools.qjsVersion
                                                : juce::String ("audio tools not installed"));
    }

    jobWasBusy = processor.getUrlImportJob().isBusy();
    tick();
    relayout();
}

void UrlImportSettingsPage::tick()
{
    auto& job = processor.getUrlImportJob();
    const bool busy = job.isBusy();

    juce::String text;
    if (job.isRunning())
    {
        text = job.getStage();
        if (const auto progress = job.getProgress(); progress >= 0.0f)
            text << "  " << juce::roundToInt (progress * 100.0f) << "%";
        if (text.isEmpty())
            text = "Working...";
    }

    setButtonEnabled (updateButton, ! busy);
    setButtonEnabled (removeButton, ! busy && toolsInstalled);

    if (busy != jobWasBusy)
    {
        jobWasBusy = busy;
        if (! busy)
        {
            jobText = {};
            reload();   // versions may have changed
            return;
        }
    }
    if (text != jobText)
    {
        jobText = text;
        relayout();
    }
}

void UrlImportSettingsPage::refreshThemeColours()
{
    for (auto* button : { &chooseButton, &defaultButton, &openButton, &removeButton })
        styleButton (*button);
    styleButton (updateButton);
    repaint();
}

void UrlImportSettingsPage::layout (RowLayout& rows)
{
    const auto& settings = processor.getUrlImportSettings();

    rows.section ("Saving imports");
    saveModeChips.setBounds (rows.row ("Save").removeFromLeft (240));
    rows.hint (settings.askEachTime ? "A save dialog opens after each download"
                                    : "Each import is saved as a WAV in the folder below, then added to the kit");

    auto folderRow = rows.row ("Folder");
    folderField.setBounds (placeButtonsRight (folderRow, { &chooseButton, &defaultButton, &openButton }).withTrimmedRight (4));
    if (settings.folder != juce::File() && folderField.isMissing())
        rows.hint ("Folder not found; it is created on the next import", true);
    else
        rows.hint (settings.askEachTime ? "Where the save dialog starts" : "Created on the first import if it doesn't exist");

    rows.section ("yt-dlp");
    channelChips.setBounds (rows.row ("Updates").removeFromLeft (200));
    rows.hint (settings.channel == UrlImport::Channel::nightly
                   ? "Nightly builds: when a site changes, fixes usually arrive within a day"
                   : "Tested releases, about monthly. If a site stops working, try NIGHTLY");

    auto toolsRow = rows.row ("Installed");
    const auto textArea = placeButtonsRight (toolsRow, { &removeButton, &updateButton });
    texts.push_back ({ installedText, textArea.withTrimmedRight (6), TextStyle::value });
    if (jobText.isNotEmpty())
        rows.hint (jobText);
    else
        rows.hint ("Checked for updates at most every 6 hours, when you import");

    if (const auto testUrl = UrlImport::ToolInstall::getTestManifestUrl(); testUrl.isNotEmpty())
        rows.hint ("Test tools manifest: " + testUrl, true);

    rows.gap (6);
    texts.push_back ({ "yt-dlp comes from its official GitHub releases, ffmpeg and QuickJS from INTERSECT's tools repo.",
                       rows.fullRow (SettingsUi::kHintH), TextStyle::hint });
    texts.push_back ({ "Every download is checked against its published SHA-256 before it is installed.",
                       rows.fullRow (SettingsUi::kHintH), TextStyle::hint });
    texts.push_back ({ "Only import audio you have the rights to use.", rows.fullRow (SettingsUi::kHintH), TextStyle::hint });
}
