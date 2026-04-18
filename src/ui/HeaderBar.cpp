#include "HeaderBar.h"
#include "IntersectLookAndFeel.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

namespace
{
enum SettingsMenuItemId
{
    kMenuScaleStatus = 1000,
    kMenuScaleDown,
    kMenuScaleUp,
    kMenuNrpnEnabled,
    kMenuConsumeCc,
    kMenuMidiStatus,
    kMenuSetMidiOmni,
    kMenuMidiPrev,
    kMenuMidiNext,
    kMenuSponsor,
    kMenuThemeBase = 2000,
    kMenuMiddleCBase = 3000,  // +0=C3, +1=C4, +2=C5
    kMenuStemFolder = 4000,
    kMenuStemUseDefaultFolder,
    kMenuStemComputeCpu,
    kMenuStemComputeGpu,
    kMenuStemDownloadMissing,
    kMenuStemCancelDownloads,
    kMenuStemDownloadBase = 4100,
    kMenuOrtCancelDownload = 4200,
    kMenuOrtBundleDownloadBase = 4300,  // + index into getOrtBundlesForCurrentPlatform()
    kMenuOrtBundleActivateBase = 4400,  // + index into getOrtBundlesForCurrentPlatform()
};

float measureTextWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getWidth();
}

juce::String formatScaleStatus (float scale)
{
    return "UI Scale  " + juce::String (scale, 2) + "x";
}

juce::String formatNrpnStatus (int channel)
{
    if (channel == 0)
        return "NRPN Settings  OMNI";

    return "NRPN Settings  CH " + juce::String (channel);
}
}

HeaderBar::HeaderBar (IntersectProcessor& p) : processor (p)
{
    for (auto* btn : { &undoBtn, &redoBtn, &panicBtn, &loadBtn, &appendBtn, &settingsBtn })
    {
        addAndMakeVisible (*btn);
        btn->setAlwaysOnTop (true);
        btn->getProperties().set (IntersectLookAndFeel::outlineOnlyButtonProperty, true);
        btn->setColour (juce::TextButton::buttonColourId, getTheme().surface4);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().text2);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().text2);
    }

    panicBtn.setTooltip ("Panic: kill all sound");
    panicBtn.onClick = [this]
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdPanic;
        processor.pushCommand (cmd);
    };

    undoBtn.setTooltip ("Undo (Ctrl+Z)");
    redoBtn.setTooltip ("Redo (Ctrl+Shift+Z)");
    loadBtn.setTooltip ("Load sample");
    appendBtn.setTooltip ("Append samples");
    settingsBtn.setTooltip ("Settings");

    undoBtn.onClick = [this]
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdUndo;
        processor.pushCommand (cmd);
    };

    redoBtn.onClick = [this]
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdRedo;
        processor.pushCommand (cmd);
    };

    loadBtn.onClick = [this] { openFileBrowser (false); };
    appendBtn.onClick = [this] { openFileBrowser (true); };
    settingsBtn.onClick = [this] { showSettingsPopup(); };
}

juce::String HeaderBar::getTooltip()
{
    if (! sampleInfoBounds.contains (getMouseXYRelative()))
        return {};

    const auto& ui = processor.getUiSliceSnapshot();
    if (ui.hasStatusMessage && ui.statusIsWarning)
        return "Copy message";

    return {};
}

void HeaderBar::resized()
{
    auto area = getLocalBounds().reduced (16, 0);
    const int contentHeight = 20;
    const int buttonHeight = 16;
    const int buttonGap = 2;
    const auto buttonFont = IntersectLookAndFeel::makeFont (9.0f);

    auto buttonWidth = [&] (const juce::String& text, int minWidth)
    {
        return juce::jmax (minWidth, juce::roundToInt (measureTextWidth (buttonFont, text)) + 18);
    };

    const int setW = buttonWidth (settingsBtn.getButtonText(), 38);
    const int appendW = buttonWidth (appendBtn.getButtonText(), 58);
    const int loadW = buttonWidth (loadBtn.getButtonText(), 42);
    const int panicW = buttonWidth (panicBtn.getButtonText(), 50);
    const int redoW = buttonWidth (redoBtn.getButtonText(), 44);
    const int undoW = buttonWidth (undoBtn.getButtonText(), 44);
    const int buttonStripW = undoW + redoW + panicW + loadW + appendW + setW + buttonGap * 5;

    juce::FlexBox row;
    row.flexDirection = juce::FlexBox::Direction::row;
    row.flexWrap = juce::FlexBox::Wrap::noWrap;
    row.alignItems = juce::FlexBox::AlignItems::center;

    // File info (flex) | buttons
    row.items.add (juce::FlexItem().withFlex (1.0f).withMinWidth (80.0f).withHeight ((float) contentHeight));
    row.items.add (juce::FlexItem().withWidth (8.0f).withHeight ((float) contentHeight));   // gap
    row.items.add (juce::FlexItem().withWidth ((float) buttonStripW).withHeight ((float) contentHeight));
    row.performLayout (area.toFloat());

    sampleInfoBounds = row.items[0].currentBounds.getSmallestIntegerContainer();

    const auto buttonArea = row.items[2].currentBounds.getSmallestIntegerContainer();
    juce::FlexBox buttons;
    buttons.flexDirection = juce::FlexBox::Direction::row;
    buttons.flexWrap = juce::FlexBox::Wrap::noWrap;
    buttons.alignItems = juce::FlexBox::AlignItems::center;
    buttons.items.add (juce::FlexItem (undoBtn).withWidth ((float) undoW).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem().withWidth ((float) buttonGap).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem (redoBtn).withWidth ((float) redoW).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem().withWidth ((float) buttonGap).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem (panicBtn).withWidth ((float) panicW).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem().withWidth ((float) buttonGap).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem (loadBtn).withWidth ((float) loadW).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem().withWidth ((float) buttonGap).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem (appendBtn).withWidth ((float) appendW).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem().withWidth ((float) buttonGap).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem (settingsBtn).withWidth ((float) setW).withHeight ((float) buttonHeight));
    buttons.performLayout (buttonArea.toFloat());
}

void HeaderBar::paint (juce::Graphics& g)
{
    for (auto* btn : { &undoBtn, &redoBtn, &panicBtn, &loadBtn, &appendBtn, &settingsBtn })
    {
        auto text = getTheme().text2.withAlpha (btn->isMouseOverOrDragging() ? 1.0f : 0.88f);
        btn->setColour (juce::TextButton::buttonColourId,
                        (btn->isMouseOverOrDragging() ? getTheme().surface5 : getTheme().surface4).withAlpha (0.94f));
        btn->setColour (juce::TextButton::textColourOnId, text);
        btn->setColour (juce::TextButton::textColourOffId, text);
    }

    g.fillAll (getTheme().surface2);
    g.setColour (getTheme().surface3.withAlpha (0.8f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    const auto& ui = processor.getUiSliceSnapshot();

    juce::String fileText;
    if (ui.hasStatusMessage)
    {
        fileText = ui.statusMessage.toString();
        g.setColour (ui.statusIsWarning ? juce::Colours::orange.brighter (0.1f)
                                        : getTheme().text1);
    }
    else if (ui.sampleMissing)
    {
        fileText = "MISSING: " + ui.sampleFileName.toString();
        g.setColour (juce::Colours::orange.brighter (0.1f));
    }
    else
    {
        fileText = "INTERSECT";
        g.setColour (getTheme().text2.withAlpha (0.52f));
    }

    g.setFont (IntersectLookAndFeel::fitFontToWidth (fileText, 11.0f, 9.0f, sampleInfoBounds.getWidth(), false));
    g.drawText (fileText, sampleInfoBounds, juce::Justification::centredLeft);
}

void HeaderBar::mouseDown (const juce::MouseEvent& e)
{
    if (sampleInfoBounds.contains (e.getPosition()))
    {
        const auto& ui = processor.getUiSliceSnapshot();
        if (ui.hasStatusMessage && ui.statusIsWarning)
            juce::SystemClipboard::copyTextToClipboard (ui.statusMessage.toString());
    }
}

void HeaderBar::adjustScale (float delta)
{
    if (auto* p = processor.apvts.getParameter (ParamIds::uiScale))
    {
        float current = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
        float newScale = juce::jlimit (0.5f, 3.0f, current + delta);
        p->setValueNotifyingHost (p->convertTo0to1 (newScale));
    }
}

void HeaderBar::showSettingsPopup()
{
    auto* editor = dynamic_cast<IntersectEditor*> (getParentComponent());
    if (editor == nullptr)
        return;

    auto themes = editor->getAvailableThemes();
    themes.sortNatural();
    auto currentName = getTheme().name;

    const bool nrpnEnabled = processor.midiEditState.enabled.load (std::memory_order_relaxed);
    const bool blockCc     = processor.midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed);
    const int  nrpnCh      = processor.midiEditState.channel.load (std::memory_order_relaxed);
    const float currentScale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    const int currentMiddleC = editor->getMiddleCOctave();

    juce::PopupMenu middleCMenu;
    middleCMenu.setLookAndFeel (&getLookAndFeel());
    for (int octave : { 3, 4, 5 })
        middleCMenu.addItem (kMenuMiddleCBase + (octave - 3), "C" + juce::String (octave),
                             true, currentMiddleC == octave);

    juce::PopupMenu nrpnMenu;
    nrpnMenu.setLookAndFeel (&getLookAndFeel());
    nrpnMenu.addSectionHeader ("NRPN Settings");
    nrpnMenu.addItem (kMenuNrpnEnabled, "Enable NRPN Editing", true, nrpnEnabled);
    nrpnMenu.addItem (kMenuConsumeCc, "Consume NRPN CCs", true, blockCc);
    nrpnMenu.addSeparator();
    nrpnMenu.addItem (kMenuMidiStatus, nrpnCh == 0 ? "Current Channel  OMNI"
                                                    : "Current Channel  CH " + juce::String (nrpnCh),
                      false, false);
    nrpnMenu.addItem (kMenuSetMidiOmni, "Set MIDI Channel to OMNI", nrpnCh != 0);
    nrpnMenu.addItem (kMenuMidiPrev, "Previous MIDI Channel", nrpnCh > 0);
    nrpnMenu.addItem (kMenuMidiNext, "Next MIDI Channel", nrpnCh < 16);

    juce::PopupMenu themesMenu;
    themesMenu.setLookAndFeel (&getLookAndFeel());
    themesMenu.addSectionHeader ("Themes");
    for (int i = 0; i < themes.size(); ++i)
        themesMenu.addItem (kMenuThemeBase + i, themes[i], true, themes[i] == currentName);

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addSectionHeader ("INTERSECT  v" + juce::String (JucePlugin_VersionString));
    menu.addItem (kMenuScaleStatus, formatScaleStatus (currentScale), false, false);
    menu.addItem (kMenuScaleUp, "Scale Up", currentScale < 3.0f);
    menu.addItem (kMenuScaleDown, "Scale Down", currentScale > 0.5f);
    menu.addSeparator();
    menu.addSubMenu ("Middle C  C" + juce::String (currentMiddleC), middleCMenu);
    menu.addSubMenu (formatNrpnStatus (nrpnCh), nrpnMenu);
    menu.addSubMenu ("Themes  " + currentName, themesMenu);

    const auto stemFolder = processor.getResolvedStemModelFolder();
    const auto customStemFolder = processor.getStemModelFolder();
    const auto installedModels = processor.getInstalledStemModels();
    const bool downloadActive = processor.getUiSliceSnapshot().stemDownloadState == StemModelDownloadState::downloading;

    juce::PopupMenu stemComputeMenu;
    stemComputeMenu.setLookAndFeel (&getLookAndFeel());
    const auto computeDevice = processor.getStemComputeDevice();
    stemComputeMenu.addItem (kMenuStemComputeCpu, "CPU", true, computeDevice == StemComputeDevice::cpu);
    stemComputeMenu.addItem (kMenuStemComputeGpu, "GPU", true, computeDevice == StemComputeDevice::gpu);

    juce::PopupMenu stemDownloadMenu;
    stemDownloadMenu.setLookAndFeel (&getLookAndFeel());
    stemDownloadMenu.addSectionHeader ("Download Models");
    stemDownloadMenu.addItem (kMenuStemDownloadMissing, "Download Missing");
    stemDownloadMenu.addSeparator();
    for (size_t i = 0; i < getStemModelCatalog().size(); ++i)
    {
        const auto& entry = getStemModelCatalog()[i];
        const bool installed = processor.isStemModelInstalled (entry.id);
        auto label = juce::String (entry.menuLabel);
        if (installed)
            label << "  (installed)";
        stemDownloadMenu.addItem (kMenuStemDownloadBase + (int) i, label, ! installed);
    }
    if (downloadActive)
    {
        stemDownloadMenu.addSeparator();
        stemDownloadMenu.addItem (kMenuStemCancelDownloads, "Cancel Active Download");
    }

    // ── ONNX Runtime bundle submenu ───────────────────────────────────
    const auto platformBundles = getOrtBundlesForCurrentPlatform();
    const auto installedBundles = processor.getInstalledOrtBundles();
    const auto activeBundleDir = processor.getActiveOrtBundleDirectoryName();
    const bool ortDownloadActive = processor.getOrtBundleDownloadJob().getState()
                                        == StemModelDownloadState::downloading;

    juce::PopupMenu ortMenu;
    ortMenu.setLookAndFeel (&getLookAndFeel());
    if (! platformBundles.empty())
    {
        juce::String activeLabel = "Active: (none)";
        for (const auto& entry : platformBundles)
            if (entry.directoryName == activeBundleDir)
                activeLabel = "Active: " + entry.menuLabel;
        ortMenu.addItem (0x4ffd, activeLabel, false, false);
        ortMenu.addSeparator();
    }

    for (size_t i = 0; i < platformBundles.size(); ++i)
    {
        const auto& entry = platformBundles[i];
        const bool installed = std::find (installedBundles.begin(), installedBundles.end(), entry.id)
                                   != installedBundles.end();
        const bool isActive = entry.directoryName == activeBundleDir;

        juce::String label = entry.menuLabel;
        if (installed && isActive)
            label << "  (active)";
        else if (installed)
            label << "  (installed — click to activate)";
        else
            label << "  (download)";

        const int baseId = installed ? kMenuOrtBundleActivateBase : kMenuOrtBundleDownloadBase;
        ortMenu.addItem (baseId + (int) i, label, ! isActive);
    }

    if (ortDownloadActive)
    {
        ortMenu.addSeparator();
        ortMenu.addItem (kMenuOrtCancelDownload, "Cancel Active Download");
    }

    juce::PopupMenu stemMenu;
    stemMenu.setLookAndFeel (&getLookAndFeel());
    stemMenu.addSectionHeader ("Stem Separation");
    stemMenu.addItem (kMenuStemFolder, "Model Folder: " + stemFolder.getFullPathName());
    stemMenu.addItem (kMenuStemUseDefaultFolder, "Use Default Folder", customStemFolder != juce::File());
    stemMenu.addSubMenu ("Compute  " + stemComputeDeviceToString (computeDevice), stemComputeMenu);
    stemMenu.addSubMenu ("Download Models", stemDownloadMenu);
    if (! platformBundles.empty())
        stemMenu.addSubMenu ("ONNX Runtime", ortMenu);
    stemMenu.addItem (0x4fff, "Installed Models  " + juce::String ((int) installedModels.size()), false, false);
    menu.addSubMenu ("Stem Separation", stemMenu);

    menu.addSeparator();
    menu.addSectionHeader ("Support");
    menu.addItem (kMenuSponsor, juce::CharPointer_UTF8 ("\xe2\x98\x95  Buy Me a Coffee"));

    auto* topLevel = getTopLevelComponent();
    float ms = IntersectLookAndFeel::getMenuScale();
    auto options = juce::PopupMenu::Options().withTargetComponent (&settingsBtn)
                                              .withDeletionCheck (*this)
                                              .withParentComponent (topLevel)
                                              .withMinimumWidth ((int) std::round (220.0f * ms))
                                              .withMaximumNumColumns (1)
                                              .withStandardItemHeight ((int) std::round (24.0f * ms));

    menu.showMenuAsync (options,
        [this, editor, themes] (int result)
        {
            float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();

            if (result == kMenuScaleDown)
                adjustScale (-0.25f);
            else if (result == kMenuScaleUp)
                adjustScale (0.25f);
            else if (result == kMenuNrpnEnabled)
            {
                const bool current = processor.midiEditState.enabled.load (std::memory_order_relaxed);
                processor.midiEditState.enabled.store (! current, std::memory_order_release);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == kMenuConsumeCc)
            {
                const bool current = processor.midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed);
                processor.midiEditState.consumeMidiEditCc.store (! current, std::memory_order_relaxed);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == kMenuSetMidiOmni)
            {
                processor.midiEditState.channel.store (0, std::memory_order_relaxed);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == kMenuMidiPrev)
            {
                const int ch = processor.midiEditState.channel.load (std::memory_order_relaxed);
                processor.midiEditState.channel.store (juce::jlimit (0, 16, ch - 1), std::memory_order_relaxed);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == kMenuMidiNext)
            {
                const int ch = processor.midiEditState.channel.load (std::memory_order_relaxed);
                processor.midiEditState.channel.store (juce::jlimit (0, 16, ch + 1), std::memory_order_relaxed);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == kMenuSponsor)
            {
                juce::URL ("https://buymeacoffee.com/tucktuckgoose").launchInDefaultBrowser();
            }
            else if (result >= kMenuMiddleCBase && result <= kMenuMiddleCBase + 2)
            {
                editor->setMiddleCOctave (3 + (result - kMenuMiddleCBase));
            }
            else if (result >= kMenuThemeBase && result < kMenuThemeBase + themes.size())
            {
                editor->applyTheme (themes[result - kMenuThemeBase]);
            }
            else if (result == kMenuStemFolder)
            {
                fileChooser = std::make_unique<juce::FileChooser> (
                    "Select Stem Model Folder", processor.getResolvedStemModelFolder());
                fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                              | juce::FileBrowserComponent::canSelectDirectories,
                    [this, editor, scale] (const juce::FileChooser& fc)
                    {
                        auto chosenFolder = fc.getResult();
                        if (chosenFolder.isDirectory())
                        {
                            processor.setStemModelFolder (chosenFolder);
                            editor->saveUserSettings (scale, getTheme().name);
                        }
                    });
            }
            else if (result == kMenuStemUseDefaultFolder)
            {
                processor.setStemModelFolder ({});
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == kMenuStemComputeCpu)
            {
                processor.setStemComputeDevice (StemComputeDevice::cpu);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == kMenuStemComputeGpu)
            {
                processor.setStemComputeDevice (StemComputeDevice::gpu);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == kMenuStemDownloadMissing)
            {
                std::vector<StemModelId> missingModels;
                for (const auto& entry : getStemModelCatalog())
                    if (! processor.isStemModelInstalled (entry.id))
                        missingModels.push_back (entry.id);

                if (missingModels.empty())
                    processor.showTransientStatusMessage ("All stem models are already installed", false);
                else
                    processor.startStemModelDownload (missingModels);
            }
            else if (result == kMenuStemCancelDownloads)
            {
                processor.cancelStemModelDownload();
            }
            else if (result >= kMenuStemDownloadBase
                     && result < kMenuStemDownloadBase + (int) getStemModelCatalog().size())
            {
                const auto& entry = getStemModelCatalog()[(size_t) (result - kMenuStemDownloadBase)];
                processor.startStemModelDownload ({ entry.id });
            }
            else if (result == kMenuOrtCancelDownload)
            {
                processor.cancelOrtBundleDownload();
            }
            else if (result >= kMenuOrtBundleDownloadBase
                     && result < kMenuOrtBundleDownloadBase + 100)
            {
                const auto bundles = getOrtBundlesForCurrentPlatform();
                const int idx = result - kMenuOrtBundleDownloadBase;
                if (idx >= 0 && idx < (int) bundles.size())
                    processor.startOrtBundleDownload (bundles[(size_t) idx].id);
            }
            else if (result >= kMenuOrtBundleActivateBase
                     && result < kMenuOrtBundleActivateBase + 100)
            {
                const auto bundles = getOrtBundlesForCurrentPlatform();
                const int idx = result - kMenuOrtBundleActivateBase;
                if (idx >= 0 && idx < (int) bundles.size())
                    processor.setActiveOrtBundle (bundles[(size_t) idx].id);
            }
        });
}

void HeaderBar::openFileBrowser (bool append)
{
    fileChooser = std::make_unique<juce::FileChooser> (
        append ? "Append Audio Files" : "Load Audio Files",
        juce::File(),
        "*.wav;*.ogg;*.aiff;*.aif;*.flac;*.mp3");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::canSelectMultipleItems,
        [this, append] (const juce::FileChooser& fc)
        {
            const auto results = fc.getResults();
            std::vector<juce::File> files;
            files.reserve (results.size());
            for (const auto& result : results)
            {
                if (result.existsAsFile())
                    files.push_back (result);
            }

            if (files.empty())
                return;

            const bool doAppend = append && processor.sampleData.isLoaded();
            processor.loadFilesAsync (files, doAppend);
            if (! doAppend)
            {
                processor.zoom.store (1.0f);
                processor.scroll.store (0.0f);
            }
        });
}

void HeaderBar::openRelinkBrowser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Relink Audio File",
        juce::File(),
        "*.wav;*.ogg;*.aiff;*.aif;*.flac;*.mp3");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile())
                processor.relinkFileAsync (result);
        });
}
