#include "PluginEditor.h"
#include "AppFiles.h"
#include <algorithm>
#include <cmath>

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
                                                  int globalLoopMode,
                                                  bool globalReverse)
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
        : globalLoopMode;
    const bool resolvedReverse = (slice.lockMask & kLockReverse) != 0
        ? slice.reverse
        : globalReverse;

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
      headerBar (p),
      sampleLane (p, waveformView),
      signalChainBar (p),
      sliceLane (p),
      waveformView (p),
      scrollZoomBar (p),
      actionPanel (p, waveformView)
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

    sliceLane.setWaveformView (&waveformView);
    sampleLane.onInteraction = [this] { deleteTarget = DeleteTarget::sample; };
    sliceLane.onInteraction = [this] { deleteTarget = DeleteTarget::slice; };
    waveformView.onInteraction = [this] { deleteTarget = DeleteTarget::slice; };
    actionPanel.onDeleteRequested = [this] { performContextualDelete(); };
    headerBar.onBrowserToggle = [this] { setBrowserVisible (! browserVisible); };
    browser.onAddFiles = [this] (const std::vector<juce::File>& files) { addFilesToKit (files); };
    browser.onLoadFiles = [this] (const std::vector<juce::File>& files) { replaceKitWithFiles (files); };
    browser.onPresetChosen = [this] (const juce::File& preset) { loadPreset (preset); };
    browser.onExportRequested = [this] (const juce::File& preset, bool embedSamples) { exportPreset (preset, embedSamples); };
    headerBar.onSaveRequested = [this] { saveKitToLibrary(); };
    browser.onOpenDialogRequested = [this] { openFileDialog(); };
    browser.onCloseRequested = [this] { setBrowserVisible (false); };
    browser.onPersistentStateChanged = [this] { persistSettings(); };
    browser.onAuditionRequested = [this] (const juce::File& file) { processor.auditionFileAsync (file); };
    browser.onAuditionStopRequested = [this] { processor.stopAudition(); };
    browser.onAuditionPauseRequested = [this] { processor.pauseAudition(); };
    browser.onAuditionResumeRequested = [this] { processor.resumeAudition(); };
    browser.onAuditionGainChanged = [this] (float db) { processor.setAuditionGainDb (db); };
    sampleLane.onFilesDropped = [this] (const std::vector<juce::File>& files) { handleDroppedFiles (files); };
    sampleLane.onStemPanelRequested = [this] { setBrowserVisible (false); };

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
    lastGlobalFadeCrossfade = processor.apvts.getRawParameterValue (ParamIds::defaultCrossfade)->load();
    lastGlobalFadeLoopMode = juce::roundToInt (processor.apvts.getRawParameterValue (ParamIds::defaultLoop)->load());
    lastGlobalFadeReverse = processor.apvts.getRawParameterValue (ParamIds::defaultReverse)->load() >= 0.5f ? 1 : 0;
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
        headerBar.adjustScale (0.25f);
        return true;
    }
    if (mods.isCommandDown() && (code == '-' || code == juce::KeyPress::numberPadSubtract))
    {
        headerBar.adjustScale (-0.25f);
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

    // Delete / Backspace - Delete Slice
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        performContextualDelete();
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

void IntersectEditor::performContextualDelete()
{
    if (deleteTarget == DeleteTarget::sample)
    {
        const int selectedSampleId = processor.selectedSessionSampleId.load (std::memory_order_relaxed);
        if (selectedSampleId >= 0)
        {
            processor.deleteSessionSampleAsync (selectedSampleId);
            return;
        }
    }

    actionPanel.deleteSelectedSliceDirect();
}

void IntersectEditor::setBrowserVisible (bool shouldBeVisible)
{
    if (browserVisible == shouldBeVisible)
        return;

    browserVisible = shouldBeVisible;
    if (browserVisible && actionPanel.isAutoChopOpen())
        actionPanel.toggleAutoChop();

    // The browser takes over the editing area; hiding the bands keeps them out of focus and
    // hit-testing while it is open.
    for (auto* band : std::initializer_list<juce::Component*> { &sliceLane, &waveformView, &scrollZoomBar,
                                                                 &actionPanel, &signalChainBar })
        band->setVisible (! browserVisible);

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
        processor.stopAudition();
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
    const auto audio = existingAudioFiles (files);
    if (audio.empty())
        return;

    processor.stopAudition();
    processor.enqueueUiUndoSnapshot();
    const bool append = processor.sampleData.isLoaded();
    processor.loadFilesAsync (audio, append);
    if (! append)
    {
        processor.zoom.store (1.0f);
        processor.scroll.store (0.0f);
    }
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

void IntersectEditor::openFileDialog()
{
    openChooser = std::make_unique<juce::FileChooser> (
        "Open Audio or Preset",
        browser.getCurrentFolder(),
        "*.wav;*.ogg;*.aiff;*.aif;*.flac;*.mp3;*" + juce::String (AppFiles::kPresetExtension));

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
            handleDroppedFiles (files);
        });
}

void IntersectEditor::persistSettings()
{
    const float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    saveUserSettings (scale, getTheme().name);
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

    const float globalCrossfade = processor.apvts.getRawParameterValue (ParamIds::defaultCrossfade)->load();
    const int globalLoopMode = juce::roundToInt (processor.apvts.getRawParameterValue (ParamIds::defaultLoop)->load());
    const int globalReverse = processor.apvts.getRawParameterValue (ParamIds::defaultReverse)->load() >= 0.5f ? 1 : 0;
    const bool globalFadeParamsChanged = std::abs (globalCrossfade - lastGlobalFadeCrossfade) > 1.0e-4f
        || globalLoopMode != lastGlobalFadeLoopMode
        || globalReverse != lastGlobalFadeReverse;

    if (globalFadeParamsChanged)
    {
        const auto previousFadeState = resolveSelectedFadeOverlayState (ui,
                                                                        lastGlobalFadeCrossfade,
                                                                        lastGlobalFadeLoopMode,
                                                                        lastGlobalFadeReverse != 0);
        const auto currentFadeState = resolveSelectedFadeOverlayState (ui,
                                                                       globalCrossfade,
                                                                       globalLoopMode,
                                                                       globalReverse != 0);
        fadeOverlayChanged = ! (previousFadeState == currentFadeState);
        lastGlobalFadeCrossfade = globalCrossfade;
        lastGlobalFadeLoopMode = globalLoopMode;
        lastGlobalFadeReverse = globalReverse;
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
    auto file = getUserSettingsFile();
    if (! file.getParentDirectory().createDirectory())
        return;  // sandboxed or read-only — skip silently
    juce::String content;
    content << "uiScale: " << juce::String (scale, 2) << "\n";
    content << "theme: " << themeName << "\n";
    content << "nrpnEnabled: "  << (processor.midiEditState.enabled.load (std::memory_order_relaxed) ? "true" : "false") << "\n";
    content << "nrpnChannel: "  << processor.midiEditState.channel.load (std::memory_order_relaxed) << "\n";
    content << "nrpnBlockCc: "  << (processor.midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed) ? "true" : "false") << "\n";
    content << "middleC: " << middleCOctave << "\n";
    auto writeList = [&content] (const char* key, const juce::StringArray& paths)
    {
        if (paths.isEmpty())
            return;
        content << key << ":\n";
        for (const auto& path : paths)
            content << "  - " << path << "\n";
    };
    writeList ("sampleBrowserBookmarks", browser.getBookmarks());
    writeList ("browserRecentFolders", browser.getRecentFolders());
    if (browser.getCurrentFolder() != juce::File())
        content << "browserLastFolder: " << browser.getCurrentFolder().getFullPathName() << "\n";
    content << "auditionAutoPlay: " << (browser.getAutoPlay() ? "true" : "false") << "\n";
    content << "auditionGainDb: " << juce::String (browser.getAuditionGainDb(), 1) << "\n";
    const auto stemFolder = processor.getStemModelFolder();
    if (stemFolder != juce::File())
        content << "stemModelFolder: " << stemFolder.getFullPathName() << "\n";
    const auto presetFolder = browser.getCustomPresetsFolder();
    if (presetFolder != juce::File())
        content << "presetFolder: " << presetFolder.getFullPathName() << "\n";
    content << "stemComputeDevice: " << stemComputeDeviceToString (processor.getStemComputeDevice()) << "\n";
    file.replaceWithText (content);
}

void IntersectEditor::loadUserSettings()
{
    savedScale = -1.0f;
    juce::String themeName = "dark";
    juce::StringArray browserBookmarks;
    juce::StringArray recentFolders;
    juce::File customPresetsFolder;
    juce::File lastFolder;
    bool auditionAutoPlay = true;
    float auditionGainDb = PreviewPane::kDefaultGainDb;

    auto file = getUserSettingsFile();
    if (file.existsAsFile())
    {
        auto content = file.loadFileAsString();
        juce::StringArray* readingList = nullptr;   // the "  - item" list currently being read
        for (auto line : juce::StringArray::fromLines (content))
        {
            const auto rawLine = line;
            line = line.trim();

            if (readingList != nullptr)
            {
                if (line.startsWith ("-"))
                {
                    auto path = line.fromFirstOccurrenceOf ("-", false, false).trim();
                    if (path.isNotEmpty())
                        readingList->addIfNotAlreadyThere (path);
                    continue;
                }

                if (! rawLine.startsWithChar (' ') && ! rawLine.startsWithChar ('\t'))
                    readingList = nullptr;
            }

            if (line.startsWith ("uiScale:"))
            {
                float val = line.fromFirstOccurrenceOf (":", false, false).trim().getFloatValue();
                if (val >= 0.5f && val <= 3.0f)
                    savedScale = val;
            }
            else if (line.startsWith ("theme:"))
            {
                themeName = line.fromFirstOccurrenceOf (":", false, false).trim();
            }
            else if (line.startsWith ("nrpnEnabled:"))
            {
                auto val = line.fromFirstOccurrenceOf (":", false, false).trim();
                processor.midiEditState.enabled.store (val == "true", std::memory_order_relaxed);
            }
            else if (line.startsWith ("nrpnChannel:"))
            {
                int ch = line.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
                processor.midiEditState.channel.store (juce::jlimit (0, 16, ch), std::memory_order_relaxed);
            }
            else if (line.startsWith ("nrpnBlockCc:"))
            {
                auto val = line.fromFirstOccurrenceOf (":", false, false).trim();
                processor.midiEditState.consumeMidiEditCc.store (val == "true", std::memory_order_relaxed);
            }
            else if (line.startsWith ("middleC:"))
            {
                int val = line.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
                if (val == 3 || val == 4 || val == 5)
                    middleCOctave = val;
            }
            else if (line.startsWith ("sampleBrowserVisible:"))
            {
                // Retired: the full-view browser always starts closed. Still recognised so older
                // settings files parse cleanly.
            }
            else if (line.startsWith ("sampleBrowserBookmarks:"))
            {
                readingList = &browserBookmarks;
            }
            else if (line.startsWith ("browserRecentFolders:"))
            {
                readingList = &recentFolders;
            }
            else if (line.startsWith ("browserLastFolder:"))
            {
                const auto path = line.fromFirstOccurrenceOf (":", false, false).trim();
                if (juce::File::isAbsolutePath (path))
                    lastFolder = juce::File (path);
            }
            else if (line.startsWith ("auditionAutoPlay:"))
            {
                auditionAutoPlay = line.fromFirstOccurrenceOf (":", false, false).trim() != "false";
            }
            else if (line.startsWith ("auditionGainDb:"))
            {
                auditionGainDb = juce::jlimit (PreviewPane::kMinGainDb, PreviewPane::kMaxGainDb,
                                               line.fromFirstOccurrenceOf (":", false, false).trim().getFloatValue());
            }
            else if (line.startsWith ("presetFolder:"))
            {
                const auto path = line.fromFirstOccurrenceOf (":", false, false).trim();
                if (juce::File::isAbsolutePath (path))
                    customPresetsFolder = juce::File (path);
            }
            else if (line.startsWith ("stemModelFolder:"))
            {
                processor.setStemModelFolder (juce::File (line.fromFirstOccurrenceOf (":", false, false).trim()));
            }
            else if (line.startsWith ("stemComputeDevice:"))
            {
                processor.setStemComputeDevice (
                    stemComputeDeviceFromString (line.fromFirstOccurrenceOf (":", false, false).trim()));
            }
            else if (line.startsWith ("stemModelPath:"))
            {
                auto legacyPath = juce::File (line.fromFirstOccurrenceOf (":", false, false).trim());
                if (legacyPath.existsAsFile())
                    processor.setStemModelFolder (legacyPath.getParentDirectory());
            }
        }
    }

    signalChainBar.middleCOctave = middleCOctave;
    processor.middleCOctave.store (middleCOctave, std::memory_order_relaxed);
    browser.setBookmarks (browserBookmarks);
    browser.setRecentFolders (recentFolders);
    browser.setCustomPresetsFolder (customPresetsFolder);
    browser.setAutoPlay (auditionAutoPlay);
    browser.setAuditionGainDb (auditionGainDb);
    processor.setAuditionGainDb (auditionGainDb);
    if (lastFolder != juce::File())
        browser.setStartFolder (lastFolder);

    // Apply theme
    applyTheme (themeName);
}
