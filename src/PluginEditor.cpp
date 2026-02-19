#include "PluginEditor.h"

static constexpr int kBaseW      = 900;
static constexpr int kBaseH      = 550;
static constexpr int kHeaderH    = 66;
static constexpr int kSliceLaneH = 30;
static constexpr int kScrollbarH = 28;
static constexpr int kSliceCtrlH = 72;
static constexpr int kActionH    = 34;
static constexpr int kMargin     = 8;

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
      sliceLane (p),
      waveformView (p),
      scrollZoomBar (p, waveformView),
      sliceControlBar (p),
      actionPanel (p, waveformView)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setLookAndFeel (&lnf);

    addAndMakeVisible (headerBar);
    addAndMakeVisible (sliceLane);
    addAndMakeVisible (waveformView);
    addAndMakeVisible (scrollZoomBar);
    addAndMakeVisible (sliceControlBar);
    addAndMakeVisible (actionPanel);

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
    startTimerHz (30);
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
    auto area = juce::Rectangle<int> (0, 0, kBaseW, kBaseH);

    // 1. Header (50px) — top
    headerBar.setBounds (area.removeFromTop (kHeaderH));

    // 2. SliceLane (24px)
    sliceLane.setBounds (area.removeFromTop (kSliceLaneH).reduced (kMargin, 0));

    // 3. SliceControlBar (56px) — bottom
    sliceControlBar.setBounds (area.removeFromBottom (kSliceCtrlH));

    // 4. ActionPanel (28px) — above slice control
    actionPanel.setBounds (area.removeFromBottom (kActionH).reduced (kMargin, 0));

    // 4px gap between scroll bar and action panel
    area.removeFromBottom (4);

    // 5. RulerBar / ScrollZoomBar (22px) — above action panel
    scrollZoomBar.setBounds (area.removeFromBottom (kScrollbarH).reduced (kMargin, 0));

    // 6. WaveformView (flexible) — remaining space
    waveformView.setBounds (area.reduced (kMargin, 0));
}

bool IntersectEditor::keyPressed (const juce::KeyPress& key)
{
    auto mods = key.getModifiers();
    int code = key.getKeyCode();

    // Ctrl+Shift+Z — Redo
    if (code == 'Z' && mods.isCommandDown() && mods.isShiftDown())
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdRedo;
        processor.pushCommand (cmd);
        return true;
    }

    // Ctrl+Z — Undo
    if (code == 'Z' && mods.isCommandDown())
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdUndo;
        processor.pushCommand (cmd);
        return true;
    }

    // Skip single-key shortcuts if any modifier is held
    if (mods.isCommandDown() || mods.isAltDown())
        return false;

    // Esc — Close Auto Chop panel (only if open)
    if (code == juce::KeyPress::escapeKey && actionPanel.isAutoChopOpen())
    {
        actionPanel.toggleAutoChop();
        return true;
    }

    // C — Toggle Auto Chop
    if (code == 'C')
    {
        actionPanel.toggleAutoChop();
        return true;
    }

    // A — Add Slice mode
    if (code == 'A')
    {
        waveformView.sliceDrawMode = ! waveformView.sliceDrawMode;
        waveformView.setMouseCursor (waveformView.sliceDrawMode
            ? juce::MouseCursor::IBeamCursor
            : juce::MouseCursor::NormalCursor);
        repaint();
        return true;
    }

    // L — Lazy Chop
    if (code == 'L')
    {
        IntersectProcessor::Command cmd;
        cmd.type = processor.lazyChop.isActive()
            ? IntersectProcessor::CmdLazyChopStop
            : IntersectProcessor::CmdLazyChopStart;
        processor.pushCommand (cmd);
        repaint();
        return true;
    }

    // D — Duplicate Slice
    if (code == 'D')
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdDuplicateSlice;
        processor.pushCommand (cmd);
        return true;
    }

    // Delete / Backspace — Delete Slice
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        int sel = processor.sliceManager.selectedSlice;
        if (sel >= 0)
        {
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdDeleteSlice;
            cmd.intParam1 = sel;
            processor.pushCommand (cmd);
        }
        return true;
    }

    // Z — Snap to Zero-Crossing
    if (code == 'Z')
    {
        bool current = processor.snapToZeroCrossing.load();
        processor.snapToZeroCrossing.store (! current);
        repaint();
        return true;
    }

    // F — Follow MIDI
    if (code == 'F')
    {
        bool current = processor.midiSelectsSlice.load();
        processor.midiSelectsSlice.store (! current);
        repaint();
        return true;
    }

    // Right arrow / Tab — Next Slice
    if (code == juce::KeyPress::rightKey
        || (code == juce::KeyPress::tabKey && ! mods.isShiftDown()))
    {
        int sel = processor.sliceManager.selectedSlice;
        int num = processor.sliceManager.getNumSlices();
        if (num > 0)
        {
            processor.sliceManager.selectedSlice = juce::jlimit (0, num - 1, sel + 1);
            repaint();
        }
        return true;
    }

    // Left arrow / Shift+Tab — Prev Slice
    if (code == juce::KeyPress::leftKey
        || (code == juce::KeyPress::tabKey && mods.isShiftDown()))
    {
        int sel = processor.sliceManager.selectedSlice;
        int num = processor.sliceManager.getNumSlices();
        if (num > 0)
        {
            processor.sliceManager.selectedSlice = juce::jlimit (0, num - 1, sel - 1);
            repaint();
        }
        return true;
    }

    return false;
}

void IntersectEditor::timerCallback()
{
    waveformView.rebuildCacheIfNeeded();

    // Check if scale changed (lastScale starts at -1 so first tick always applies)
    float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    if (scale != lastScale)
    {
        lastScale = scale;
        setTransform (juce::AffineTransform::scale (scale));
        IntersectLookAndFeel::setMenuScale (scale);
        saveUserSettings (scale, getTheme().name);
    }

    repaint();
}

void IntersectEditor::ensureDefaultThemes()
{
    auto dir = getThemesDir();
    dir.createDirectory();

    auto darkFile = dir.getChildFile ("dark.intersectstyle");
    if (! darkFile.existsAsFile())
        darkFile.replaceWithText (ThemeData::darkTheme().toThemeFile());

    auto lightFile = dir.getChildFile ("light.intersectstyle");
    if (! lightFile.existsAsFile())
        lightFile.replaceWithText (ThemeData::lightTheme().toThemeFile());
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
        }
    }

    // Apply theme
    applyTheme (themeName);
}
