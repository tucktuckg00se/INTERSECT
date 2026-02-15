#include "PluginEditor.h"

static constexpr int kBaseW      = 750;
static constexpr int kBaseH      = 550;
static constexpr int kHeaderH    = 50;
static constexpr int kSliceLaneH = 24;
static constexpr int kScrollbarH = 22;
static constexpr int kSliceCtrlH = 56;
static constexpr int kActionH    = 28;
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

    setSize (kBaseW, kBaseH);
    startTimerHz (30);
}

IntersectEditor::~IntersectEditor()
{
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

    // 5. RulerBar / ScrollZoomBar (22px) — above action panel
    scrollZoomBar.setBounds (area.removeFromBottom (kScrollbarH).reduced (kMargin, 0));

    // 6. WaveformView (flexible) — remaining space
    waveformView.setBounds (area.reduced (kMargin, 0));
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
        saveUserSettings (scale, getTheme().name);
    }

    repaint();
}

void IntersectEditor::ensureDefaultThemes()
{
    auto dir = getThemesDir();
    dir.createDirectory();

    auto darkFile = dir.getChildFile ("dark.yaml");
    if (! darkFile.existsAsFile())
        darkFile.replaceWithText (ThemeData::darkTheme().toYaml());

    auto lightFile = dir.getChildFile ("light.yaml");
    if (! lightFile.existsAsFile())
        lightFile.replaceWithText (ThemeData::lightTheme().toYaml());
}

juce::StringArray IntersectEditor::getAvailableThemes()
{
    juce::StringArray names;
    auto dir = getThemesDir();
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.yaml"))
    {
        auto content = f.loadFileAsString();
        auto theme = ThemeData::fromYaml (content);
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
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.yaml"))
    {
        auto content = f.loadFileAsString();
        auto theme = ThemeData::fromYaml (content);
        if (theme.name == themeName)
        {
            setTheme (theme);
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
