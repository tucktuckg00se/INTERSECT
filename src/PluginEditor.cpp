#include "PluginEditor.h"
#include <algorithm>

static constexpr int kBaseW        = 800;
static constexpr int kBaseH        = 400;
static constexpr float kHeaderH    = 28.0f;
static constexpr float kSliceLaneH = 20.0f;
static constexpr float kScrollbarH = 10.0f;
static constexpr float kActionH    = 22.0f;
static constexpr float kSignalChainH = 94.0f;
static constexpr float kWaveformMinH = 180.0f;

static juce::File getSettingsDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("INTERSECT");
}

static juce::File getUserSettingsFile()
{
    return getSettingsDir().getChildFile ("settings.yaml");
}

static juce::File getThemesDir()
{
    return getSettingsDir().getChildFile ("themes");
}

IntersectEditor::IntersectEditor (IntersectProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      headerBar (p),
      signalChainBar (p),
      sliceLane (p),
      waveformView (p),
      scrollZoomBar (p),
      actionPanel (p, waveformView)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setLookAndFeel (&lnf);

    addAndMakeVisible (headerBar);
    addAndMakeVisible (signalChainBar);
    addAndMakeVisible (sliceLane);
    addAndMakeVisible (waveformView);
    addAndMakeVisible (scrollZoomBar);
    addAndMakeVisible (actionPanel);

    sliceLane.setWaveformView (&waveformView);

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
    setSize (kBaseW, kBaseH);
    lastUiSnapshotVersion = processor.getUiSliceSnapshotVersion();
    timerHz = 30;
    startTimerHz (timerHz);
}

IntersectEditor::~IntersectEditor()
{
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void IntersectEditor::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().background);
}

void IntersectEditor::resized()
{
    juce::FlexBox shell;
    shell.flexDirection = juce::FlexBox::Direction::column;
    shell.flexWrap = juce::FlexBox::Wrap::noWrap;

    shell.items.add (juce::FlexItem (headerBar)
                         .withMinHeight (kHeaderH)
                         .withMaxHeight (kHeaderH)
                         .withHeight (kHeaderH));
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
    shell.items.add (juce::FlexItem (signalChainBar)
                         .withMinHeight (kSignalChainH)
                         .withMaxHeight (kSignalChainH)
                         .withHeight (kSignalChainH));

    shell.performLayout (getLocalBounds().toFloat());
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

    // Ignore other Command/Alt combos and let host/OS handle them.
    if (mods.isCommandDown() || mods.isAltDown())
        return false;

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
    }

    // Delete / Backspace - Delete Slice
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        actionPanel.triggerDeleteSelectedSlice();
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

void IntersectEditor::timerCallback()
{
    bool uiChanged = false;
    bool viewportChanged = false;
    const bool previewActive = waveformView.hasActiveSlicePreview();
    const bool waveformInteracting = waveformView.isInteracting();
    const bool rulerDragging = scrollZoomBar.isDraggingNow();

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

    // Check if scale changed; scaleDirty forces application on first timer tick
    float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    if (scaleDirty || scale != lastScale)
    {
        scaleDirty = false;
        lastScale = scale;
        setTransform (juce::AffineTransform::scale (scale));
        IntersectLookAndFeel::setMenuScale (scale);
        saveUserSettings (scale, getTheme().name);
        uiChanged = true;
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
        || lastWaveformAnimating;

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
    dir.createDirectory();

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
    float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    saveUserSettings (scale, themeName);
    repaint();
}

void IntersectEditor::saveUserSettings (float scale, const juce::String& themeName)
{
    auto file = getUserSettingsFile();
    file.getParentDirectory().createDirectory();
    juce::String content;
    content << "uiScale: " << juce::String (scale, 2) << "\n";
    content << "theme: " << themeName << "\n";
    content << "nrpnEnabled: "  << (processor.midiEditState.enabled.load (std::memory_order_relaxed) ? "true" : "false") << "\n";
    content << "nrpnChannel: "  << processor.midiEditState.channel.load (std::memory_order_relaxed) << "\n";
    content << "nrpnBlockCc: "  << (processor.midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed) ? "true" : "false") << "\n";
    file.replaceWithText (content);
}

void IntersectEditor::loadUserSettings()
{
    savedScale = -1.0f;
    juce::String themeName = "dark";

    auto file = getUserSettingsFile();
    if (file.existsAsFile())
    {
        auto content = file.loadFileAsString();
        for (auto line : juce::StringArray::fromLines (content))
        {
            line = line.trim();
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
        }
    }

    // Apply theme
    applyTheme (themeName);
}
