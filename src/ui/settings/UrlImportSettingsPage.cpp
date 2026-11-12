#include "UrlImportSettingsPage.h"
#include "../Callout.h"
#include "../IntersectLookAndFeel.h"
#include "../../PluginProcessor.h"

using namespace SettingsUi;

namespace
{
juce::String separatorDot()
{
    return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
}

// The Format dropdown, in order. Codec and bit depth only; the rate is its own row.
const juce::StringArray kFormatLabels { "WAV 32-bit float", "WAV 24-bit", "WAV 16-bit", "FLAC 24-bit", "FLAC 16-bit" };
const juce::StringArray kFormatValues { "wav-f32", "wav-24", "wav-16", "flac-24", "flac-16" };

// The rate chips before CUSTOM; 0 keeps the source's rate.
constexpr int kPresetRates[] = { 0, 44100, 48000, 96000 };
constexpr int kNumPresetRates = (int) (sizeof (kPresetRates) / sizeof (kPresetRates[0]));

int presetIndexFor (int sampleRate)
{
    for (int i = 0; i < kNumPresetRates; ++i)
        if (kPresetRates[i] == sampleRate)
            return i;
    return -1;
}
} // namespace

UrlImportSettingsPage::UrlImportSettingsPage (IntersectProcessor& p, SettingsHost& h)
    : SettingsPage (p, h)
{
    for (auto* c : std::initializer_list<juce::Component*> { &saveModeChips, &folderField, &chooseButton, &defaultButton,
                                                             &openButton, &formatDropdown, &rateChips,
                                                             &channelChips, &updateButton, &removeButton })
        addAndMakeVisible (*c);
    addChildComponent (customRateField);

    formatDropdown.setOptions (kFormatLabels);
    formatDropdown.onChange = [this] (int index)
    {
        auto format = UrlImport::formatFromString (kFormatValues[index]);
        format.sampleRate = processor.getUrlImportSettings().format.sampleRate;
        setOutputFormat (format);
    };

    rateChips.setOptions ({ "ORIGINAL", "44.1 kHz", "48 kHz", "96 kHz", "CUSTOM" });
    rateChips.onChange = [this] (int index)
    {
        if (index < kNumPresetRates)
        {
            customRateSelected = false;
            customRateInvalid = false;
            auto format = processor.getUrlImportSettings().format;
            format.sampleRate = kPresetRates[index];
            setOutputFormat (format);
            return;
        }

        // CUSTOM: nothing changes until a valid rate is typed.
        customRateSelected = true;
        const int current = processor.getUrlImportSettings().format.sampleRate;
        customRateField.setText (presetIndexFor (current) < 0 ? juce::String (current) : juce::String(), false);
        relayout();
        customRateField.grabKeyboardFocus();
    };

    customRateField.setMultiLine (false);
    customRateField.setInputRestrictions (6, "0123456789");
    customRateField.setJustification (juce::Justification::centredLeft);
    customRateField.setIndents (6, 0);
    customRateField.setFont (IntersectLookAndFeel::makeFont (9.5f));
    // TextEditor callbacks arrive as async commands; apply afterwards, never from inside one.
    auto commitLater = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<UrlImportSettingsPage> (this)]
        {
            if (safe != nullptr)
                safe->commitCustomRate();
        });
    };
    customRateField.onReturnKey = commitLater;
    customRateField.onFocusLost = commitLater;

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

void UrlImportSettingsPage::setOutputFormat (const UrlImport::OutputFormat& format)
{
    auto settings = processor.getUrlImportSettings();
    settings.format = format;
    processor.setUrlImportSettings (settings);
    host.persistSettings();
    reload();
}

void UrlImportSettingsPage::commitCustomRate()
{
    if (! customRateSelected)
        return;

    const auto text = customRateField.getText().trim();
    if (text.isEmpty())
    {
        // Left empty: back to whatever is saved.
        const int saved = processor.getUrlImportSettings().format.sampleRate;
        customRateSelected = presetIndexFor (saved) < 0;
        customRateInvalid = false;
        rateChips.setSelectedIndex (customRateSelected ? kNumPresetRates : presetIndexFor (saved));
        relayout();
        return;
    }

    const int rate = text.getIntValue();
    if (! UrlImport::isValidSampleRate (rate))
    {
        customRateInvalid = true;
        relayout();
        return;
    }

    customRateInvalid = false;
    auto format = processor.getUrlImportSettings().format;
    if (format.sampleRate == rate)
    {
        relayout();
        return;
    }
    format.sampleRate = rate;
    setOutputFormat (format);
}

void UrlImportSettingsPage::reload()
{
    const auto& settings = processor.getUrlImportSettings();
    formatDropdown.setSelectedIndex (juce::jmax (0, kFormatValues.indexOf (UrlImport::formatToString (settings.format))));
    const int presetIndex = presetIndexFor (settings.format.sampleRate);
    if (presetIndex < 0)
    {
        customRateSelected = true;
        customRateInvalid = false;
        customRateField.setText (juce::String (settings.format.sampleRate), false);
    }
    rateChips.setSelectedIndex (customRateSelected ? kNumPresetRates : presetIndex);
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

    const auto& theme = getTheme();
    customRateField.setColour (juce::TextEditor::backgroundColourId, theme.surface1.withAlpha (0.92f));
    customRateField.setColour (juce::TextEditor::outlineColourId, theme.surface4.withAlpha (0.92f));
    customRateField.setColour (juce::TextEditor::focusedOutlineColourId, theme.accent.withAlpha (0.85f));
    customRateField.setColour (juce::TextEditor::textColourId, theme.text2);
    customRateField.setColour (juce::TextEditor::highlightColourId, theme.accent.withAlpha (0.35f));
    customRateField.setTextToShowWhenEmpty ("e.g. 16000", theme.text0);
    repaint();
}

void UrlImportSettingsPage::layout (RowLayout& rows)
{
    const auto& settings = processor.getUrlImportSettings();

    rows.section ("Saving imports");
    saveModeChips.setBounds (rows.row ("Save").removeFromLeft (240));
    rows.hint (settings.askEachTime ? "A save dialog opens after each download"
                                    : "Each import is saved in the folder below, then added to the kit");

    auto folderRow = rows.row ("Folder");
    folderField.setBounds (placeButtonsRight (folderRow, { &chooseButton, &defaultButton, &openButton }).withTrimmedRight (4));
    if (settings.folder != juce::File() && folderField.isMissing())
        rows.hint ("Folder not found; it is created on the next import", true);
    else
        rows.hint (settings.askEachTime ? "Where the save dialog starts" : "Created on the first import if it doesn't exist");

    rows.section ("Output");
    formatDropdown.setBounds (rows.row ("Format").removeFromLeft (200));
    rows.hint (settings.format.codec == UrlImport::OutputFormat::Codec::flac
                   ? "Lossless, about half the size of WAV. Very loud tracks can clip slightly"
                   : settings.format.bitDepth == 32 ? "Never clips; the largest files"
                                                    : "Very loud tracks can clip slightly; 32-bit float never does");

    const int rateWidth = juce::jmin (rows.getControlWidth(), 330);
    auto rateRow = rows.row ("Sample rate", juce::jmax (SettingsUi::kRowH, rateChips.getHeightForWidth (rateWidth)));
    rateChips.setBounds (rateRow.removeFromLeft (rateWidth));
    customRateField.setVisible (customRateSelected);
    if (customRateSelected)
    {
        auto customRow = rows.row ("Custom rate");
        customRateField.setBounds (customRow.removeFromLeft (110));
        texts.push_back ({ "Hz", customRow.withTrimmedLeft (6), TextStyle::value });
        if (customRateInvalid)
            rows.hint ("Enter a rate from " + juce::String (UrlImport::kMinSampleRate) + " to "
                           + juce::String (UrlImport::kMaxSampleRate) + " Hz, then press Return", true);
        else
            rows.hint ("Press Return to apply. Low rates such as 16000 Hz sound lo-fi");
    }
    else
    {
        rows.hint ("INTERSECT plays any rate; this only changes the saved file");
    }

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
