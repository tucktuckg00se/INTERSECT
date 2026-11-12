#include "PluginEditor.h"
#include "AppFiles.h"
#include "midi/MidiExport.h"
#include <algorithm>
#include <cmath>

namespace
{
// Sample-level params of the sample that owns the selected slice (else the selected sample).
const SampleParams& selectedSliceSampleParams (const IntersectProcessor::UiSliceSnapshot& ui)
{
    const bool hasSlice = ui.selectedSlice >= 0 && ui.selectedSlice < ui.numSlices;
    return ui.sampleParamsFor (hasSlice ? ui.slices[(size_t) ui.selectedSlice].sampleId
                                        : ui.selectedSessionSampleId);
}
}

static constexpr int kBaseW        = 800;
static constexpr int kBaseH        = 400;
static constexpr float kHeaderH    = 28.0f;
static constexpr float kSampleLaneH = 20.0f;
static constexpr float kSliceLaneH = 20.0f;
static constexpr float kScrollbarH = 10.0f;
static constexpr float kActionH    = 22.0f;
static constexpr float kWaveformMinH = 180.0f;
static constexpr float kCollapsedSignalChainH = 94.0f;   // must match SignalChainBar kCollapsedHeight
static constexpr float kExpandedSignalChainH = 180.0f;   // must match SignalChainBar kExpandedHeight

// Auto-fit: reserve for host FX-window chrome around the plugin view, in
// JUCE-logical px (chrome is roughly constant in logical units across DPI).
static constexpr float kHostChromeReserveW = 20.0f;  // side borders + slack
static constexpr float kHostChromeReserveH = 56.0f;  // title bar + borders + host header strip
static constexpr float kMinEffectiveScale  = 0.4f;   // usability floor on tiny displays
static constexpr float kScaleEpsilon       = 0.005f; // half a 0.01 quantisation step

static juce::File getUserSettingsFile()
{
    return AppFiles::getSettingsDir().getChildFile ("settings.yaml");
}

static juce::File getThemesDir()
{
    return AppFiles::getSettingsDir().getChildFile ("themes");
}

namespace
{
struct FadeOverlayState
{
    bool visible = false;
    bool pingPong = false;
    bool reverse = false;
    float crossfadePct = 0.0f;

    bool operator== (const FadeOverlayState& other) const noexcept
    {
        if (visible != other.visible)
            return false;

        if (! visible)
            return true;

        if (pingPong != other.pingPong)
            return false;

        if (std::abs (crossfadePct - other.crossfadePct) > 1.0e-4f)
            return false;

        return pingPong || reverse == other.reverse;
    }
};

FadeOverlayState resolveSelectedFadeOverlayState (const IntersectProcessor::UiSliceSnapshot& ui,
                                                  float globalCrossfadePct,
                                                  int sampleLoopMode,
                                                  bool sampleReverse)
{
    const int selectedSlice = ui.selectedSlice;
    if (selectedSlice < 0 || selectedSlice >= ui.numSlices)
        return {};

    const auto& slice = ui.slices[(size_t) selectedSlice];
    if (! slice.active)
        return {};

    const float resolvedCrossfade = (slice.lockMask & kLockCrossfade) != 0
        ? slice.crossfadePct
        : globalCrossfadePct;
    const int resolvedLoopMode = (slice.lockMask & kLockLoop) != 0
        ? slice.loopMode
        : sampleLoopMode;
    const bool resolvedReverse = (slice.lockMask & kLockReverse) != 0
        ? slice.reverse
        : sampleReverse;

    if (resolvedCrossfade <= 0.0f || resolvedLoopMode == 0)
        return {};

    FadeOverlayState state;
    state.visible = true;
    state.pingPong = (resolvedLoopMode == 2);
    state.reverse = resolvedReverse;
    state.crossfadePct = resolvedCrossfade;
    return state;
}
} // namespace

IntersectEditor::IntersectEditor (IntersectProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      browser(),
      stemView (p),
      settingsView (p, *this),
      headerBar (p),
      sampleLane (p),
      signalChainBar (p),
      sliceLane (p),
      waveformView (p),
      scrollZoomBar (p),
      actionPanel (p, waveformView),
      urlImportPanel (p),
      settingsFile (getUserSettingsFile())
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (headerBar);
    addAndMakeVisible (sampleLane);
    addAndMakeVisible (signalChainBar);
    addAndMakeVisible (sliceLane);
    addAndMakeVisible (waveformView);
    addAndMakeVisible (scrollZoomBar);
    addAndMakeVisible (actionPanel);
    addChildComponent (browser);
    addChildComponent (stemView);
    addChildComponent (settingsView);
    addChildComponent (urlImportPanel);   // over the full views, under popovers and menus
    urlImportPanel.onLayoutChanged = [this] { positionUrlImportPanel(); };

    sliceLane.setWaveformView (&waveformView);
    sampleLane.onInteraction = [this] { deleteTarget = DeleteTarget::sample; };
    sliceLane.onInteraction = [this] { deleteTarget = DeleteTarget::slice; };
    waveformView.onInteraction = [this] { deleteTarget = DeleteTarget::slice; };
    actionPanel.onDeleteRequested = [this] { performContextualDelete (juce::ModifierKeys::getCurrentModifiers().isShiftDown()); };
    headerBar.onBrowserToggle = [this]
    {
        // Only the FILES button uses the default folder; SAVE and RELINK open where they need to.
        // "Last used" is shared by every instance, so read it fresh: another may have browsed since.
        if (! browserVisible)
        {
            const auto lastUsed = settingsFile.readValue ("browserLastFolder");
            browser.applyDefaultFolder (juce::File::isAbsolutePath (lastUsed) ? juce::File (lastUsed) : juce::File());
        }
        setBrowserVisible (! browserVisible);
    };
    headerBar.onSettingsToggle = [this] { setSettingsVisible (! settingsVisible); };
    settingsView.onCloseRequested = [this] { setSettingsVisible (false); };
    browser.onAddFiles = [this] (const std::vector<juce::File>& files) { addFilesToKit (files); };
    browser.onLoadFiles = [this] (const std::vector<juce::File>& files) { replaceKitWithFiles (files); };
    browser.onPresetChosen = [this] (const juce::File& preset) { loadPreset (preset); };
    browser.onExportRequested = [this] (const juce::File& preset, bool embedSamples) { exportPreset (preset, embedSamples); };
    headerBar.onSaveRequested = [this] { saveKitToLibrary(); };
    browser.onOpenDialogRequested = [this] { openFileDialog(); };
    browser.onImportUrlRequested = [this] (const juce::String& url)
    {
        // A link entered in the path bar starts right away (Enter means go); the menu asks for one.
        if (url.isNotEmpty())
        {
            positionUrlImportPanel();
            urlImportPanel.startWithUrl (url);
        }
        else
        {
            showUrlImport ({});
        }
    };
    browser.onCloseRequested = [this] { setBrowserVisible (false); };
    browser.onPersistentStateChanged = [this] { persistSettings(); };
    browser.onFolderChanged = [this] { persistSettings(); };   // other instances follow the last folder
    browser.onAuditionRequested = [this] (const juce::File& file) { processor.auditionFileAsync (file); };
    browser.onAuditionStopRequested = [this] { processor.stopAudition(); };
    browser.onAuditionPauseRequested = [this] { processor.pauseAudition(); };
    browser.onAuditionResumeRequested = [this] { processor.resumeAudition(); };
    browser.onAuditionGainChanged = [this] (float db) { processor.setAuditionGainDb (db); };
    sampleLane.onFilesDropped = [this] (const std::vector<juce::File>& files) { handleDroppedFiles (files); };
    sampleLane.onStemViewRequested = [this] (int sampleId)
    {
        // STEMS on the sample already shown closes the view; another sample retargets it.
        const bool showingThisSample = stemViewVisible && stemView.getTargetSampleId() == sampleId;
        setStemViewVisible (! showingThisSample, sampleId);
    };
    stemView.onCloseRequested = [this] { setStemViewVisible (false); };
    sampleLane.writeMidiForDrag = [this] (int sampleId) { return writeMidiForDrag (sampleId); };
    sampleLane.onMidiSaveRequested = [this] (int sampleId) { saveMidiAs (sampleId); };
    sampleLane.getAudioFileForDrag = [this] (int sampleId) { return resolveAudioExportFile (sampleId); };
    sampleLane.onAudioSaveRequested = [this] (int sampleId) { saveAudioAs (sampleId); };
    sampleLane.onRelinkRequested = [this] (int sampleId) { startRelink (sampleId); };
    waveformView.onRelinkRequested = [this] (int sampleId) { startRelink (sampleId); };
    waveformView.onRelinkFileDropped = [this] (int sampleId, const juce::File& file) { relinkSample (sampleId, file); };
    headerBar.onRelinkRequested = [this] (int sampleId) { startRelink (sampleId); };
    browser.onRelinkFile = [this] (int sampleId, const juce::File& file) { relinkSample (sampleId, file); };

    signalChainBar.onHeightChanged = [this] { applyLogicalSize(); };

    // Write default theme files if they don't exist
    ensureDefaultThemes();

    // Load user settings (scale + theme)
    loadUserSettings();

    // If the APVTS scale is still at default (1.0), apply loaded user scale
    float apvtsScale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    if (apvtsScale == 1.0f && savedScale > 0.0f && savedScale != apvtsScale)
    {
        if (auto* param = processor.apvts.getParameter (ParamIds::uiScale))
            param->setValueNotifyingHost (param->convertTo0to1 (savedScale));
    }

    setWantsKeyboardFocus (true);
    applyLogicalSize();
    updateUiTransform();   // host's first getSize sees the restored scale; refitted post-attach
    lastUiSnapshotVersion = processor.getUiSliceSnapshotVersion();
    lastPresetSaveVersion = processor.getPresetSaveVersion();
    {
        const auto& fadeSample = selectedSliceSampleParams (processor.getUiSliceSnapshot());
        lastSampleFadeCrossfade = fadeSample.crossfadePct;
        lastSampleFadeLoopMode = fadeSample.loopMode;
        lastSampleFadeReverse = fadeSample.reverse ? 1 : 0;
    }
    timerHz = 30;
    startTimerHz (timerHz);
}

IntersectEditor::~IntersectEditor()
{
    processor.stopAudition();
    persistSettings();   // remembers the browser's last folder
    setLookAndFeel (nullptr);
}

void IntersectEditor::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().surface1);
}

void IntersectEditor::resized()
{
    auto bounds = getLocalBounds();

    // The browser covers everything below the sample lane; header and lane stay usable.
    browser.setBounds (bounds.withTrimmedTop ((int) (kHeaderH + kSampleLaneH)));
    stemView.setBounds (browser.getBounds());
    settingsView.setBounds (browser.getBounds());

    juce::FlexBox shell;
    shell.flexDirection = juce::FlexBox::Direction::column;
    shell.flexWrap = juce::FlexBox::Wrap::noWrap;

    shell.items.add (juce::FlexItem (headerBar)
                         .withMinHeight (kHeaderH)
                         .withMaxHeight (kHeaderH)
                         .withHeight (kHeaderH));
    shell.items.add (juce::FlexItem (sampleLane)
                         .withMinHeight (kSampleLaneH)
                         .withMaxHeight (kSampleLaneH)
                         .withHeight (kSampleLaneH));
    shell.items.add (juce::FlexItem (sliceLane)
                         .withMinHeight (kSliceLaneH)
                         .withMaxHeight (kSliceLaneH)
                         .withHeight (kSliceLaneH));
    shell.items.add (juce::FlexItem (waveformView)
                         .withFlex (1.0f)
                         .withMinHeight (kWaveformMinH));
    shell.items.add (juce::FlexItem (scrollZoomBar)
                         .withMinHeight (kScrollbarH)
                         .withMaxHeight (kScrollbarH)
                         .withHeight (kScrollbarH));
    shell.items.add (juce::FlexItem (actionPanel)
                         .withMinHeight (kActionH)
                         .withMaxHeight (kActionH)
                         .withHeight (kActionH));
    const float signalChainH = signalChainBar.getDesiredHeight();
    shell.items.add (juce::FlexItem (signalChainBar)
                         .withMinHeight (signalChainH)
                         .withMaxHeight (signalChainH)
                         .withHeight (signalChainH));

    shell.performLayout (bounds.toFloat());
    positionUrlImportPanel();
}

bool IntersectEditor::keyPressed (const juce::KeyPress& key)
{
    auto mods = key.getModifiers();
    int code = key.getKeyCode();

    // Ctrl+Shift+Z - Redo
    if (code == 'Z' && mods.isCommandDown() && mods.isShiftDown())
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdRedo;
        processor.pushCommand (cmd);
        return true;
    }

    // Ctrl+Z - Undo
    if (code == 'Z' && mods.isCommandDown())
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdUndo;
        processor.pushCommand (cmd);
        return true;
    }

    // Ctrl+= / Ctrl+- - UI scale up / down
    if (mods.isCommandDown() && (code == '=' || code == '+' || code == juce::KeyPress::numberPadAdd))
    {
        adjustUiScale (0.25f);
        return true;
    }
    if (mods.isCommandDown() && (code == '-' || code == juce::KeyPress::numberPadSubtract))
    {
        adjustUiScale (-0.25f);
        return true;
    }

    // Cmd/Ctrl+V with a link on the clipboard opens Import from URL with it. A focused text field
    // gets the paste first, so this only sees pastes nothing else wanted.
    if (code == 'V' && mods.isCommandDown() && ! mods.isShiftDown() && ! mods.isAltDown())
    {
        const auto clipboard = juce::SystemClipboard::getTextFromClipboard().trim();
        if (! UrlImport::isWebUrl (clipboard))
            return false;
        showUrlImport (clipboard);
        return true;
    }

    // Ignore other Command/Alt combos and let host/OS handle them.
    if (mods.isCommandDown() || mods.isAltDown())
        return false;

    // While browsing, editing shortcuts would act on the hidden editor. Esc that the browser
    // didn't use (e.g. focus was on the header) closes it.
    if (browserVisible)
    {
        if (code == juce::KeyPress::escapeKey)
        {
            setBrowserVisible (false);
            return true;
        }
        return false;
    }

    // Same for the stem view and settings.
    if (stemViewVisible)
    {
        if (code == juce::KeyPress::escapeKey)
        {
            setStemViewVisible (false);
            return true;
        }
        return false;
    }

    if (settingsVisible)
    {
        if (code == juce::KeyPress::escapeKey)
        {
            setSettingsVisible (false);
            return true;
        }
        return false;
    }

    // Esc - Close Auto Chop panel if open
    if (code == juce::KeyPress::escapeKey)
    {
        if (actionPanel.isAutoChopOpen())  { actionPanel.toggleAutoChop();  return true; }
    }

    // Shift shortcuts keep plain letter keys available for DAW keyboard MIDI.
    if (mods.isShiftDown())
    {
        if (code == 'A')
        {
            actionPanel.triggerAddSliceMode();
            return true;
        }

        if (code == 'Z')
        {
            actionPanel.triggerLazyChop();
            return true;
        }

        if (code == 'C')
        {
            actionPanel.triggerAutoChop();
            return true;
        }

        if (code == 'D')
        {
            actionPanel.triggerDuplicateSlice();
            return true;
        }

        if (code == 'X')
        {
            actionPanel.toggleSnapToZeroCrossing();
            return true;
        }

        if (code == 'F')
        {
            actionPanel.toggleFollowMidiSelection();
            return true;
        }

        if (code == 'R')
        {
            actionPanel.triggerReseqMidi();
            return true;
        }
    }

    // Delete / Backspace - delete the slice, or remove the sample (Shift: without asking)
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        performContextualDelete (mods.isShiftDown());
        return true;
    }

    // Right arrow / Tab - Next Slice
    if (code == juce::KeyPress::rightKey
        || (code == juce::KeyPress::tabKey && ! mods.isShiftDown()))
    {
        const auto& ui = processor.getUiSliceSnapshot();
        int sel = ui.selectedSlice;
        int num = ui.numSlices;
        if (num > 0)
        {
            deleteTarget = DeleteTarget::slice;
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdSelectSlice;
            cmd.intParam1 = juce::jlimit (0, num - 1, sel + 1);
            processor.pushCommand (cmd);
            repaint();
        }
        return true;
    }

    // Left arrow / Shift+Tab - Prev Slice
    if (code == juce::KeyPress::leftKey
        || (code == juce::KeyPress::tabKey && mods.isShiftDown()))
    {
        const auto& ui = processor.getUiSliceSnapshot();
        int sel = ui.selectedSlice;
        int num = ui.numSlices;
        if (num > 0)
        {
            deleteTarget = DeleteTarget::slice;
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdSelectSlice;
            cmd.intParam1 = juce::jlimit (0, num - 1, sel - 1);
            processor.pushCommand (cmd);
            repaint();
        }
        return true;
    }

    return false;
}

// Writes one session sample's slices as a .mid file (see intersectMidi::buildKitMidiFile).
bool IntersectEditor::writeMidiExport (const juce::File& dest, int sampleId)
{
    const auto sampleSnap = processor.sampleData.getSnapshot();
    const SampleData::SessionSample* sample = nullptr;
    if (sampleSnap != nullptr)
        for (const auto& s : sampleSnap->sessionSamples)
            if (s.sampleId == sampleId)
                sample = &s;

    if (sample == nullptr)
        return false;

    const auto& ui = processor.getUiSliceSnapshot();
    return intersectMidi::writeKitMidiFile (dest, ui, sample, ui.sampleParamsFor (sampleId).bpm);
}

juce::String IntersectEditor::getExportBaseName (int sampleId) const
{
    const auto& ui = processor.getUiSliceSnapshot();
    juce::String name;
    for (int i = 0; i < ui.numSessionSamples; ++i)
        if (ui.sessionSamples[(size_t) i].sampleId == sampleId)
            name = ui.sessionSamples[(size_t) i].fileName.toString();

    name = juce::File::createLegalFileName (juce::File (name).getFileNameWithoutExtension()).trim();
    return name.isEmpty() ? juce::String ("INTERSECT slices") : name;
}

juce::File IntersectEditor::getTempExportFile (int sampleId, const juce::String& extension) const
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("INTERSECT");
    if (! folder.createDirectory())
        return {};

    juce::String safeName;
    for (auto c : getExportBaseName (sampleId))
    {
        const bool keep = juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '_' || c == '.';
        safeName += juce::String::charToString (keep ? c : (juce::juce_wchar) '_');
    }
    return folder.getChildFile (safeName + "." + extension);
}

// MIDI dragged out of the sample lane: the OS drag needs a real file, so write it up front.
juce::File IntersectEditor::writeMidiForDrag (int sampleId)
{
    auto file = getTempExportFile (sampleId, "mid");
    if (file == juce::File())
        return {};
    if (! writeMidiExport (file, sampleId))
    {
        processor.showTransientStatusMessage ("No slices to export as MIDI", true);
        return {};
    }
    return file;
}

void IntersectEditor::saveMidiAs (int sampleId)
{
    const auto& ui = processor.getUiSliceSnapshot();
    bool hasSlices = false;
    for (int i = 0; i < ui.numSlices && ! hasSlices; ++i)
        hasSlices = ui.slices[(size_t) i].active && ui.slices[(size_t) i].sampleId == sampleId;
    if (! hasSlices)
    {
        processor.showTransientStatusMessage ("No slices to export as MIDI", true);
        return;
    }

    const auto folder = lastExportFolder.isDirectory()
        ? lastExportFolder
        : juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    auto fileChooser = std::make_shared<juce::FileChooser> (
        "Save Slices as MIDI File",
        folder.getChildFile (getExportBaseName (sampleId) + ".mid"),
        "*.mid");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, fileChooser, sampleId] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File())
                return;

            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension ("mid");

            lastExportFolder = file.getParentDirectory();
            if (writeMidiExport (file, sampleId))
                processor.showTransientStatusMessage ("MIDI saved to " + file.getFullPathName(), false);
            else
                processor.showTransientStatusMessage ("Couldn't write " + file.getFullPathName(), true);
        });
}

juce::File IntersectEditor::resolveAudioExportFile (int sampleId)
{
    const auto sampleSnap = processor.sampleData.getSnapshot();
    const SampleData::SessionSample* sample = nullptr;
    if (sampleSnap != nullptr)
        for (const auto& s : sampleSnap->sessionSamples)
            if (s.sampleId == sampleId)
                sample = &s;

    if (sample == nullptr || sample->isPlaceholder())
    {
        processor.showTransientStatusMessage ("This sample's file is missing - relink it first", true);
        return {};
    }

    const auto source = juce::File::createFileWithoutCheckingPath (sample->filePath);
    if (! source.existsAsFile())
    {
        processor.showTransientStatusMessage ("Couldn't find " + source.getFullPathName(), true);
        return {};
    }
    if (! AppFiles::isRex2File (source))
        return source;   // the original file: the DAW imports or references it itself

    // REX2: hand over what INTERSECT plays (rendered slices with their tails) as a WAV.
    const auto& buffer = sampleSnap->buffer;
    const int start = juce::jlimit (0, buffer.getNumSamples(), sample->startFrame);
    const int numFrames = juce::jlimit (0, buffer.getNumSamples() - start, sample->numFrames);
    auto wavFile = getTempExportFile (sampleId, "wav");
    if (numFrames <= 0 || wavFile == juce::File() || sampleSnap->decodedSampleRate <= 0.0)
    {
        processor.showTransientStatusMessage ("Couldn't export this sample's audio", true);
        return {};
    }

    wavFile.deleteFile();
    auto fileStream = std::make_unique<juce::FileOutputStream> (wavFile);
    std::unique_ptr<juce::AudioFormatWriter> writer;
    if (fileStream->openedOk())
    {
        std::unique_ptr<juce::OutputStream> output = std::move (fileStream);   // the writer takes it on success
        writer = juce::WavAudioFormat().createWriterFor (output, juce::AudioFormatWriterOptions{}
                                                                     .withSampleRate (sampleSnap->decodedSampleRate)
                                                                     .withNumChannels (buffer.getNumChannels())
                                                                     .withBitsPerSample (24));
    }
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, start, numFrames))
    {
        writer.reset();
        wavFile.deleteFile();
        processor.showTransientStatusMessage ("Couldn't write " + wavFile.getFullPathName(), true);
        return {};
    }
    return wavFile;
}

void IntersectEditor::saveAudioAs (int sampleId)
{
    const auto source = resolveAudioExportFile (sampleId);
    if (! source.existsAsFile())
        return;   // already explained in the status line

    const auto extension = source.getFileExtension();   // ".wav", ".flac"...
    const auto folder = lastExportFolder.isDirectory()
        ? lastExportFolder
        : juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    auto fileChooser = std::make_shared<juce::FileChooser> (
        "Save a Copy of the Audio File",
        folder.getChildFile (getExportBaseName (sampleId) + extension),
        "*" + extension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, fileChooser, source, extension] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File())
                return;

            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension (extension);

            lastExportFolder = file.getParentDirectory();
            if (file == source)
            {
                processor.showTransientStatusMessage ("That is the sample's own file - pick another name or folder", true);
                return;
            }
            if (source.copyFileTo (file))
                processor.showTransientStatusMessage ("Audio saved to " + file.getFullPathName(), false);
            else
                processor.showTransientStatusMessage ("Couldn't write " + file.getFullPathName(), true);
        });
}

void IntersectEditor::performContextualDelete (bool skipConfirm)
{
    if (deleteTarget == DeleteTarget::sample)
    {
        const int selectedSampleId = processor.selectedSessionSampleId.load (std::memory_order_relaxed);
        if (selectedSampleId >= 0)
        {
            sampleLane.requestRemoveSample (selectedSampleId, skipConfirm);   // asks first unless Shift
            return;
        }
    }

    actionPanel.deleteSelectedSliceDirect();
}

void IntersectEditor::setEditingBandsVisible (bool shouldBeVisible)
{
    // Full views take over the editing area; hiding the bands keeps them out of focus and
    // hit-testing while one is open.
    for (auto* band : std::initializer_list<juce::Component*> { &sliceLane, &waveformView, &scrollZoomBar,
                                                                 &actionPanel, &signalChainBar })
        band->setVisible (shouldBeVisible);
}

void IntersectEditor::setBrowserVisible (bool shouldBeVisible)
{
    if (browserVisible == shouldBeVisible)
        return;

    if (shouldBeVisible)
    {
        setStemViewVisible (false);
        setSettingsVisible (false);
    }

    browserVisible = shouldBeVisible;
    if (browserVisible && actionPanel.isAutoChopOpen())
        actionPanel.toggleAutoChop();

    setEditingBandsVisible (! browserVisible);

    browser.setVisible (browserVisible);
    headerBar.setBrowserActive (browserVisible);
    sampleLane.setShowEmptyHint (browserVisible);

    if (browserVisible)
    {
        browser.toFront (false);   // above any overlay panel parented to the editor
        browser.focusFileList();
    }
    else
    {
        browser.endRelink();
        processor.stopAudition();
        persistSettings();
        grabKeyboardFocus();
    }
}

void IntersectEditor::setStemViewVisible (bool shouldBeVisible, int sampleId)
{
    if (shouldBeVisible)
    {
        setBrowserVisible (false);
        setSettingsVisible (false);
        stemView.openForSample (sampleId);
    }

    if (stemViewVisible == shouldBeVisible)
        return;

    stemViewVisible = shouldBeVisible;
    if (stemViewVisible && actionPanel.isAutoChopOpen())
        actionPanel.toggleAutoChop();

    setEditingBandsVisible (! stemViewVisible);
    stemView.setVisible (stemViewVisible);

    if (stemViewVisible)
    {
        stemView.toFront (false);
        stemView.grabKeyboardFocus();
    }
    else
    {
        persistSettings();   // remembered model, stem choices and mode
        grabKeyboardFocus();
    }
}

void IntersectEditor::setSettingsVisible (bool shouldBeVisible)
{
    if (settingsVisible == shouldBeVisible)
        return;

    if (shouldBeVisible)
    {
        setBrowserVisible (false);
        setStemViewVisible (false);
    }

    settingsVisible = shouldBeVisible;
    if (settingsVisible && actionPanel.isAutoChopOpen())
        actionPanel.toggleAutoChop();

    setEditingBandsVisible (! settingsVisible);
    headerBar.setSettingsActive (settingsVisible);

    if (settingsVisible)
    {
        // Opening settings is the only time INTERSECT asks GitHub for a newer release (at most every 6 h).
        processor.getUpdateChecker().checkIfStale (JucePlugin_VersionString, false);
        settingsView.reloadActivePage();
        settingsView.setVisible (true);
        settingsView.toFront (false);
        settingsView.grabKeyboardFocus();
    }
    else
    {
        settingsView.setVisible (false);
        persistSettings();
        grabKeyboardFocus();
    }
}

float IntersectEditor::computeEffectiveScale (float desiredScale) const
{
    // Fit against the MAXIMAL layout (signal chain expanded) so panel toggles never change
    // the rendered scale — only scale choice or display do. The browser opens inside the
    // same area, so it never widens the window.
    constexpr float maxLogicalW = (float) kBaseW;
    constexpr float maxLogicalH = kBaseH + (kExpandedSignalChainH - kCollapsedSignalChainH);

    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
    if (display == nullptr)
        return desiredScale;

    const float availW = (float) display->userBounds.getWidth()  - kHostChromeReserveW;
    const float availH = (float) display->userBounds.getHeight() - kHostChromeReserveH;
    if (availW <= 0.0f || availH <= 0.0f)
        return desiredScale;

    float fit = juce::jmin (desiredScale, availW / maxLogicalW, availH / maxLogicalH);
    fit = std::floor (fit * 100.0f) / 100.0f;   // quantise DOWN: never overflows, tick-stable
    return juce::jmax (kMinEffectiveScale, fit);
}

bool IntersectEditor::updateUiTransform()
{
    // Cheap guard: skip the display lookup unless an input changed (desired scale,
    // or on-screen position — catches monitor drags, for which embedded plugin
    // windows get no OS event). A ~1 Hz slow revalidation catches in-place display
    // config changes (resolution/DPI edits).
    const float desired  = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    const auto screenPos = getScreenBounds().getPosition();
    const bool slowTick  = (++fitCheckCounter % 30 == 0);
    if (! slowTick && desired == lastFitDesired && screenPos == lastFitScreenPos)
        return false;
    lastFitDesired = desired;
    lastFitScreenPos = screenPos;

    const float effective = computeEffectiveScale (desired);
    if (std::abs (effective - lastAppliedScale) <= kScaleEpsilon)
        return false;

    lastAppliedScale = effective;
    setTransform (juce::AffineTransform::scale (effective));
    return true;
}

void IntersectEditor::applyLogicalSize()
{
    const float delta = signalChainBar.getDesiredHeight() - kCollapsedSignalChainH;
    const int w = kBaseW;
    const int h = kBaseH + (int) delta;
    setSize (w, h);
    setResizeLimits (w, h, w, h);   // pin logical size; min==max keeps canResize false
}

namespace
{
std::vector<juce::File> existingAudioFiles (const std::vector<juce::File>& files)
{
    std::vector<juce::File> audio;
    for (const auto& file : files)
        if (file.existsAsFile() && AppFiles::isSupportedAudioFile (file))
            audio.push_back (file);
    return audio;
}
}

// ADD: append to the session (or load into an empty one). Undoable.
void IntersectEditor::addFilesToKit (const std::vector<juce::File>& files)
{
    processor.addFilesToKit (files);
}

// LOAD: replace the kit (samples and slices) and return to the editor. Undoable.
void IntersectEditor::replaceKitWithFiles (const std::vector<juce::File>& files)
{
    const auto audio = existingAudioFiles (files);
    if (audio.empty())
        return;

    processor.stopAudition();
    processor.enqueueUiUndoSnapshot();
    processor.loadFilesAsync (audio, false);
    processor.zoom.store (1.0f);
    processor.scroll.store (0.0f);
    setBrowserVisible (false);
}

void IntersectEditor::loadPreset (const juce::File& preset)
{
    processor.stopAudition();
    processor.loadPresetAsync (preset);
    setBrowserVisible (false);
}

// Browser rows dropped on the sample lane: a preset loads, audio is added.
void IntersectEditor::handleDroppedFiles (const std::vector<juce::File>& files)
{
    for (const auto& file : files)
    {
        if (AppFiles::isPresetFile (file))
        {
            loadPreset (file);
            return;
        }
    }

    addFilesToKit (files);
}

void IntersectEditor::startRelink (int sampleId)
{
    const auto sampleSnap = processor.sampleData.getSnapshot();
    if (sampleSnap == nullptr)
        return;

    for (const auto& sample : sampleSnap->sessionSamples)
    {
        if (sample.sampleId == sampleId && sample.isPlaceholder())
        {
            processor.selectedSessionSampleId.store (sampleId, std::memory_order_relaxed);
            processor.markUiSnapshotDirty();
            setBrowserVisible (true);
            browser.beginRelink (sampleId, sample.fileName,
                                 juce::File::createFileWithoutCheckingPath (sample.filePath));
            return;
        }
    }
}

void IntersectEditor::relinkSample (int sampleId, const juce::File& file)
{
    const auto sampleSnap = processor.sampleData.getSnapshot();
    if (sampleSnap == nullptr || existingAudioFiles ({ file }).empty())
        return;

    // Missing files usually moved together: any other placeholder whose file name exists next
    // to the chosen file is relinked in the same step.
    std::vector<std::pair<int, juce::File>> relinks { { sampleId, file } };
    const auto folder = file.getParentDirectory();
    for (const auto& sample : sampleSnap->sessionSamples)
    {
        if (sample.sampleId == sampleId || ! sample.isPlaceholder())
            continue;
        const auto sibling = folder.getChildFile (sample.fileName);
        if (sibling.existsAsFile() && ! existingAudioFiles ({ sibling }).empty())
            relinks.emplace_back (sample.sampleId, sibling);
    }

    processor.stopAudition();
    processor.relinkSessionSamplesAsync (relinks);
    processor.showTransientStatusMessage (relinks.size() == 1
                                              ? "Relinked " + file.getFileName()
                                              : "Relinked " + juce::String ((int) relinks.size()) + " samples",
                                          false);

    // Keep going with the next missing sample, or return to the editor when none are left.
    for (const auto& sample : sampleSnap->sessionSamples)
    {
        const bool handled = std::any_of (relinks.begin(), relinks.end(),
                                          [&sample] (const auto& r) { return r.first == sample.sampleId; });
        if (sample.isPlaceholder() && ! handled)
        {
            browser.beginRelink (sample.sampleId, sample.fileName,
                                 juce::File::createFileWithoutCheckingPath (sample.filePath));
            return;
        }
    }

    browser.endRelink();
    setBrowserVisible (false);
}

void IntersectEditor::openFileDialog()
{
    openChooser = std::make_unique<juce::FileChooser> (
        "Open Audio or Preset",
        browser.getCurrentFolder(),
        "*.wav;*.ogg;*.aiff;*.aif;*.flac;*.mp3;*.rx2;*" + juce::String (AppFiles::kPresetExtension));

    openChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
        [this] (const juce::FileChooser& fc)
        {
            const auto results = fc.getResults();
            if (results.isEmpty())
                return;

            std::vector<juce::File> files (results.begin(), results.end());
            browser.revealFile (files.front());
            if (browser.isRelinking())
            {
                if (const auto audio = existingAudioFiles (files); ! audio.empty())
                    relinkSample (browser.getRelinkSampleId(), audio.front());
                return;
            }
            handleDroppedFiles (files);
        });
}

void IntersectEditor::showUrlImport (const juce::String& prefillUrl)
{
    positionUrlImportPanel();
    urlImportPanel.open (prefillUrl);
}

void IntersectEditor::positionUrlImportPanel()
{
    // Centred just under the sample lane, over whatever is shown there (editor, browser, settings).
    const int width = juce::jmin (UrlImportPanel::kWidth, getWidth() - 16);
    urlImportPanel.setBounds ((getWidth() - width) / 2, (int) (kHeaderH + kSampleLaneH) + 6,
                              width, urlImportPanel.getPreferredHeight());
}

void IntersectEditor::askWhereToSaveImport (const UrlImport::JobResult& result)
{
    auto folder = lastUrlImportSaveFolder.isDirectory() ? lastUrlImportSaveFolder : processor.getResolvedUrlImportFolder();
    folder.createDirectory();
    auto extension = result.file.getFileExtension().trimCharactersAtStart (".");   // what ffmpeg wrote
    if (extension.isEmpty())
        extension = "wav";
    const auto suggested = folder.getChildFile (UrlImport::makeOutputFileName (result.title, {}, extension));

    urlImportChooser = std::make_unique<juce::FileChooser> ("Save Imported Audio", suggested, "*." + extension);
    urlImportChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                       | juce::FileBrowserComponent::canSelectFiles
                                       | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, result, extension] (const juce::FileChooser& chooser)
        {
            auto destination = chooser.getResult();
            if (destination == juce::File())
            {
                processor.discardUrlImport (result.workFolder);
                return;
            }
            if (! destination.hasFileExtension (extension))
                destination = destination.withFileExtension (extension);
            lastUrlImportSaveFolder = destination.getParentDirectory();
            processor.placeUrlImport (result.file, destination, result.workFolder, result.title);
        });
}

void IntersectEditor::persistSettings()
{
    saveUserSettings (getDesiredUiScale(), getTheme().name);
}

float IntersectEditor::getDesiredUiScale() const
{
    return processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
}

void IntersectEditor::adjustUiScale (float delta)
{
    // Saved by timerCallback when it sees the parameter change.
    if (auto* param = processor.apvts.getParameter (ParamIds::uiScale))
        param->setValueNotifyingHost (param->convertTo0to1 (juce::jlimit (0.5f, 3.0f, getDesiredUiScale() + delta)));
}

void IntersectEditor::setBrowserDefaultFolderMode (Browser::DefaultFolderMode mode)
{
    browser.setDefaultFolderMode (mode);
    persistSettings();
}

void IntersectEditor::setBrowserDefaultFolder (const juce::File& folder)
{
    browser.setDefaultFolder (folder);
    persistSettings();
}

// SAVE: write the kit straight into the preset library under a unique name, then show it in
// the browser; timerCallback opens the name for editing once the file exists.
void IntersectEditor::saveKitToLibrary()
{
    const auto folder = browser.getPresetSaveFolder();
    if (! folder.createDirectory())
    {
        processor.showTransientStatusMessage ("Couldn't create " + folder.getFullPathName(), true);
        return;
    }

    const auto baseName = getDefaultPresetName();
    auto target = folder.getChildFile (baseName + AppFiles::kPresetExtension);
    for (int n = 2; target.exists() || target == pendingRenamePreset; ++n)   // pending: a save still being written
        target = folder.getChildFile (baseName + " " + juce::String (n) + AppFiles::kPresetExtension);

    pendingRenamePreset = target;
    processor.savePresetAsync (target, false);
    setBrowserVisible (true);
}

// Right-click Export... / Export with Samples... on a preset in the browser.
void IntersectEditor::exportPreset (const juce::File& preset, bool embedSamples)
{
    const auto startFolder = lastExportFolder.isDirectory()
        ? lastExportFolder
        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    presetChooser = std::make_unique<juce::FileChooser> (
        embedSamples ? "Export Preset with Samples" : "Export Preset",
        startFolder.getChildFile (preset.getFileName()),
        juce::String ("*") + AppFiles::kPresetExtension);

    presetChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                    | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, preset, embedSamples] (const juce::FileChooser& fc)
        {
            auto destination = fc.getResult();
            if (destination == juce::File())
                return;   // cancelled

            // Some native dialogs don't add the filter's extension; append rather than replace,
            // so a name like "Kit.v2" keeps its dot.
            if (! AppFiles::isPresetFile (destination))
                destination = destination.getSiblingFile (destination.getFileName() + AppFiles::kPresetExtension);

            if (destination == preset && ! embedSamples)
                return;   // copying a file onto itself changes nothing

            lastExportFolder = destination.getParentDirectory();
            processor.exportPresetAsync (preset, destination, embedSamples);
        });
}

juce::String IntersectEditor::getDefaultPresetName() const
{
    const auto& ui = processor.getUiSliceSnapshot();
    const auto sourceName = ui.numSessionSamples > 0 ? ui.sessionSamples[0].fileName.toString()
                                                     : ui.sampleFileName.toString();
    const auto name = juce::File::createLegalFileName (
        sourceName.upToLastOccurrenceOf (".", false, false).trim());
    return name.isNotEmpty() ? name : juce::String ("Untitled");
}

void IntersectEditor::setCustomPresetsFolder (const juce::File& folder)
{
    browser.setCustomPresetsFolder (folder);
    persistSettings();
}

void IntersectEditor::timerCallback()
{
    // Apply deferred non-RT parameter restores from undo/redo.
    processor.applyDeferredParamRestore();

    if (processor.getPresetSaveVersion() != lastPresetSaveVersion)
    {
        lastPresetSaveVersion = processor.getPresetSaveVersion();
        const auto saved = processor.getLastSavedPresetFile();
        browser.revealFile (saved);
        if (saved == pendingRenamePreset)
        {
            pendingRenamePreset = juce::File();
            if (browserVisible)
                browser.beginRename (saved);
        }
    }

    if (browserVisible)
        browser.setAuditionStatus (processor.pollAuditionStatus());

    urlImportPanel.tick();
    if (auto pending = processor.takePendingUrlImportSave())
        askWhereToSaveImport (*pending);

    bool uiChanged = false;
    bool viewportChanged = false;
    bool fadeOverlayChanged = false;
    const bool previewActive = waveformView.hasActiveSlicePreview();
    const bool waveformInteracting = waveformView.isInteracting();
    const bool rulerDragging = scrollZoomBar.isDraggingNow();
    const auto& ui = processor.getUiSliceSnapshot();

    const auto snapshotVersion = processor.getUiSliceSnapshotVersion();
    if (snapshotVersion != lastUiSnapshotVersion)
    {
        lastUiSnapshotVersion = snapshotVersion;
        uiChanged = true;
    }

    const float zoom = processor.zoom.load();
    const float scroll = processor.scroll.load();
    if (zoom != lastZoom || scroll != lastScroll)
    {
        lastZoom = zoom;
        lastScroll = scroll;
        viewportChanged = true;
    }

    // Persist the *desired* scale when the param changes; the transform uses the
    // effective (auto-fitted) scale so the window always fits the current display.
    const float desiredScale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    if (desiredScale != lastScale)
    {
        lastScale = desiredScale;
        saveUserSettings (desiredScale, getTheme().name);
        uiChanged = true;
    }
    if (updateUiTransform())
        uiChanged = true;

    // The selected slice inherits fade/loop/reverse from its own sample.
    const auto& fadeSample = selectedSliceSampleParams (ui);
    const float sampleCrossfade = fadeSample.crossfadePct;
    const int sampleLoopMode = fadeSample.loopMode;
    const int sampleReverse = fadeSample.reverse ? 1 : 0;
    const bool sampleFadeParamsChanged = std::abs (sampleCrossfade - lastSampleFadeCrossfade) > 1.0e-4f
        || sampleLoopMode != lastSampleFadeLoopMode
        || sampleReverse != lastSampleFadeReverse;

    if (sampleFadeParamsChanged)
    {
        const auto previousFadeState = resolveSelectedFadeOverlayState (ui,
                                                                        lastSampleFadeCrossfade,
                                                                        lastSampleFadeLoopMode,
                                                                        lastSampleFadeReverse != 0);
        const auto currentFadeState = resolveSelectedFadeOverlayState (ui,
                                                                       sampleCrossfade,
                                                                       sampleLoopMode,
                                                                       sampleReverse != 0);
        fadeOverlayChanged = ! (previousFadeState == currentFadeState);
        lastSampleFadeCrossfade = sampleCrossfade;
        lastSampleFadeLoopMode = sampleLoopMode;
        lastSampleFadeReverse = sampleReverse;
    }

    const bool playbackActive = std::any_of (processor.voicePool.voicePositions.begin(),
                                             processor.voicePool.voicePositions.end(),
                                             [] (const std::atomic<float>& pos)
                                             {
                                                 return pos.load (std::memory_order_relaxed) > 0.0f;
                                             });

    const bool waveformAnimating = waveformInteracting
        || rulerDragging
        || previewActive
        || playbackActive
        || processor.lazyChop.isActive();
    const bool waveformNeedsRepaint = uiChanged
        || viewportChanged
        || waveformAnimating
        || lastWaveformAnimating
        || fadeOverlayChanged;

    const bool laneNeedsRepaint = uiChanged
        || viewportChanged
        || previewActive
        || lastPreviewActive;

    const bool rulerNeedsRepaint = uiChanged
        || viewportChanged
        || rulerDragging;

    lastWaveformAnimating = waveformAnimating;
    lastPreviewActive = previewActive;

    const int targetHz = waveformAnimating ? 60 : 30;
    if (targetHz != timerHz)
    {
        startTimerHz (targetHz);
        timerHz = targetHz;
    }

    if (waveformNeedsRepaint)
        waveformView.repaint();

    if (laneNeedsRepaint)
        sampleLane.repaint();

    if (laneNeedsRepaint)
        sliceLane.repaint();

    if (rulerNeedsRepaint)
        scrollZoomBar.repaint();

    // HeaderBar and SignalChainBar display APVTS param values that can change
    // independently of the audio-thread snapshot (e.g. dragging a header param
    // while a slice is selected), so repaint them every tick.
    headerBar.repaint();
    signalChainBar.repaint();

    if (uiChanged)
        actionPanel.repaint();
}

void IntersectEditor::ensureDefaultThemes()
{
    auto dir = getThemesDir();
    if (! dir.createDirectory())
        return;  // sandboxed or read-only — fall back to in-memory defaults

    auto darkFile = dir.getChildFile ("dark.intersectstyle");
    auto darkText = ThemeData::darkTheme().toThemeFile();
    if (! darkFile.existsAsFile() || darkFile.loadFileAsString() != darkText)
        darkFile.replaceWithText (darkText);

    auto lightFile = dir.getChildFile ("light.intersectstyle");
    auto lightText = ThemeData::lightTheme().toThemeFile();
    if (! lightFile.existsAsFile() || lightFile.loadFileAsString() != lightText)
        lightFile.replaceWithText (lightText);
}

juce::StringArray IntersectEditor::getAvailableThemes()
{
    juce::StringArray names;
    auto dir = getThemesDir();
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.intersectstyle"))
    {
        auto content = f.loadFileAsString();
        auto theme = ThemeData::fromThemeFile (content);
        if (theme.name.isNotEmpty())
            names.add (theme.name);
    }
    if (names.isEmpty())
    {
        names.add ("dark");
        names.add ("light");
    }
    return names;
}

void IntersectEditor::applyTheme (const juce::String& themeName)
{
    auto dir = getThemesDir();
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.intersectstyle"))
    {
        auto content = f.loadFileAsString();
        auto theme = ThemeData::fromThemeFile (content);
        if (theme.name == themeName)
        {
            setTheme (theme);
            processor.sliceManager.setSlicePalette (getTheme().slicePalette);
            processor.sliceManager.recolourFromPalette();
            processor.markUiSnapshotDirty();
            browser.refreshThemeColours();
            stemView.refreshThemeColours();
            settingsView.refreshThemeColours();
            urlImportPanel.refreshThemeColours();
            float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
            saveUserSettings (scale, themeName);
            repaint();
            return;
        }
    }

    // Fallback to built-in
    if (themeName == "light")
        setTheme (ThemeData::lightTheme());
    else
        setTheme (ThemeData::darkTheme());

    processor.sliceManager.setSlicePalette (getTheme().slicePalette);
    processor.sliceManager.recolourFromPalette();
    processor.markUiSnapshotDirty();
    browser.refreshThemeColours();
    stemView.refreshThemeColours();
    settingsView.refreshThemeColours();
    urlImportPanel.refreshThemeColours();
    float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    saveUserSettings (scale, themeName);
    repaint();
}

void IntersectEditor::setMiddleCOctave (int octave)
{
    middleCOctave = octave;
    signalChainBar.middleCOctave = octave;
    processor.middleCOctave.store (octave, std::memory_order_relaxed);
    signalChainBar.markLayoutDirty();
    signalChainBar.repaint();
    float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    saveUserSettings (scale, getTheme().name);
}

void IntersectEditor::saveUserSettings (float scale, const juce::String& themeName)
{
    if (loadingSettings)
        return;   // values are only half applied; writing now would overwrite the file with defaults

    auto boolText = [] (bool b) { return juce::String (b ? "true" : "false"); };
    auto setPath = [] (SettingsValues& values, const char* key, const juce::File& path)
    {
        if (path != juce::File())
            values.set (key, path.getFullPathName());
    };

    SettingsValues values;
    values.set ("uiScale", juce::String (scale, 2));
    values.set ("theme", themeName);
    values.set ("nrpnEnabled", boolText (processor.midiEditState.enabled.load (std::memory_order_relaxed)));
    values.set ("nrpnChannel", juce::String (processor.midiEditState.channel.load (std::memory_order_relaxed)));
    values.set ("nrpnBlockCc", boolText (processor.midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed)));
    values.set ("middleC", juce::String (middleCOctave));
    values.setList ("sampleBrowserBookmarks", browser.getBookmarks());
    values.setList ("browserRecentFolders", browser.getRecentFolders());
    setPath (values, "browserLastFolder", browser.getCurrentFolder());
    values.set ("browserDefaultFolderMode",
                browser.getDefaultFolderMode() == Browser::DefaultFolderMode::chosenFolder ? "folder" : "last");
    setPath (values, "browserDefaultFolder", browser.getDefaultFolder());
    values.set ("auditionAutoPlay", boolText (browser.getAutoPlay()));
    values.set ("auditionGainDb", juce::String (browser.getAuditionGainDb(), 1));
    setPath (values, "stemModelFolder", processor.getStemModelFolder());
    setPath (values, "presetFolder", browser.getCustomPresetsFolder());
    values.set ("stemComputeDevice", stemComputeDeviceToString (processor.getStemComputeDevice()));
    if (const auto stemModel = stemView.getLastModelId(); stemModel.isNotEmpty())
        values.set ("stemLastModel", stemModel);
    values.set ("stemExportMode", stemExportModeToString (stemView.getExportMode()));
    values.setList ("stemSelections", stemView.getRememberedSelections());
    const auto& urlImport = processor.getUrlImportSettings();
    values.set ("urlImportSaveMode", urlImport.askEachTime ? "ask" : "folder");
    setPath (values, "urlImportFolder", urlImport.folder);
    values.set ("urlImportChannel", UrlImport::channelToString (urlImport.channel));
    values.set ("urlImportFormat", UrlImport::formatToString (urlImport.format));
    values.set ("urlImportSampleRate", juce::String (urlImport.format.sampleRate));

    // Only the keys this editor changed are written; other instances' changes are kept.
    settingsFile.save (values);
}

void IntersectEditor::loadUserSettings()
{
    // Applying values below triggers callbacks that would save a half-loaded state.
    const juce::ScopedValueSetter<bool> loading (loadingSettings, true);
    const auto values = settingsFile.load();

    auto getBool = [&values] (const char* key, bool fallback)
    {
        const auto text = values.getString (key);
        return text.isEmpty() ? fallback : text == "true";
    };
    auto getPath = [&values] (const char* key)
    {
        const auto path = values.getString (key);
        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    };

    savedScale = -1.0f;
    if (const auto scale = values.getString ("uiScale"); scale.isNotEmpty())
        if (const float val = scale.getFloatValue(); val >= 0.5f && val <= 3.0f)
            savedScale = val;

    // NRPN state is also restored with the session; only override what the file actually has.
    if (values.contains ("nrpnEnabled"))
        processor.midiEditState.enabled.store (getBool ("nrpnEnabled", false), std::memory_order_relaxed);
    if (const auto channel = values.getString ("nrpnChannel"); channel.isNotEmpty())
        processor.midiEditState.channel.store (juce::jlimit (0, 16, channel.getIntValue()), std::memory_order_relaxed);
    if (values.contains ("nrpnBlockCc"))
        processor.midiEditState.consumeMidiEditCc.store (getBool ("nrpnBlockCc", true), std::memory_order_relaxed);

    if (const int octave = values.getString ("middleC").getIntValue(); octave == 3 || octave == 4 || octave == 5)
        middleCOctave = octave;
    signalChainBar.middleCOctave = middleCOctave;
    processor.middleCOctave.store (middleCOctave, std::memory_order_relaxed);

    // "sampleBrowserVisible" is retired: the full-view browser always starts closed.
    browser.setBookmarks (values.getList ("sampleBrowserBookmarks"));
    browser.setRecentFolders (values.getList ("browserRecentFolders"));
    browser.setCustomPresetsFolder (getPath ("presetFolder"));
    browser.setAutoPlay (getBool ("auditionAutoPlay", true));
    const float auditionGainDb = values.contains ("auditionGainDb")
        ? juce::jlimit (PreviewPane::kMinGainDb, PreviewPane::kMaxGainDb, values.getString ("auditionGainDb").getFloatValue())
        : PreviewPane::kDefaultGainDb;
    browser.setAuditionGainDb (auditionGainDb);
    processor.setAuditionGainDb (auditionGainDb);
    browser.setDefaultFolder (getPath ("browserDefaultFolder"));
    browser.setDefaultFolderMode (values.getString ("browserDefaultFolderMode") == "folder"
                                      ? Browser::DefaultFolderMode::chosenFolder
                                      : Browser::DefaultFolderMode::lastUsed);
    if (const auto lastFolder = getPath ("browserLastFolder"); lastFolder != juce::File())
        browser.setStartFolder (lastFolder);

    if (const auto stemFolder = getPath ("stemModelFolder"); stemFolder != juce::File())
        processor.setStemModelFolder (stemFolder);
    else if (const auto legacyModel = getPath ("stemModelPath"); legacyModel.existsAsFile())
        processor.setStemModelFolder (legacyModel.getParentDirectory());   // pre-folder builds stored one model file
    if (values.contains ("stemComputeDevice"))
        processor.setStemComputeDevice (stemComputeDeviceFromString (values.getString ("stemComputeDevice")));
    if (values.contains ("stemLastModel"))
        stemView.setLastModelId (normaliseStemModelId (values.getString ("stemLastModel")));
    if (values.contains ("stemExportMode"))
        stemView.setExportMode (stemExportModeFromString (values.getString ("stemExportMode")));
    stemView.setRememberedSelections (values.getList ("stemSelections"));

    IntersectProcessor::UrlImportSettings urlImport;
    urlImport.askEachTime = values.getString ("urlImportSaveMode") == "ask";
    urlImport.folder = getPath ("urlImportFolder");
    urlImport.channel = UrlImport::channelFromString (values.getString ("urlImportChannel"));
    urlImport.format = UrlImport::formatFromString (values.getString ("urlImportFormat"));
    const int sampleRate = values.getString ("urlImportSampleRate").getIntValue();
    urlImport.format.sampleRate = UrlImport::isValidSampleRate (sampleRate) ? sampleRate : 0;
    processor.setUrlImportSettings (urlImport);

    applyTheme (values.getString ("theme", "dark"));
}
