#include "PluginEditor.h"

static constexpr int kBaseW      = 750;
static constexpr int kBaseH      = 550;
static constexpr int kHeaderH    = 50;
static constexpr int kSliceLaneH = 24;
static constexpr int kScrollbarH = 22;
static constexpr int kSliceCtrlH = 56;
static constexpr int kActionH    = 28;
static constexpr int kMargin     = 8;

static juce::File getUserSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("INTERSECT")
               .getChildFile ("settings.yaml");
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

    // If the APVTS scale is still at default (1.0), try loading from user settings
    float apvtsScale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    if (apvtsScale == 1.0f)
    {
        float userScale = loadUserScale();
        if (userScale > 0.0f && userScale != apvtsScale)
        {
            if (auto* p = processor.apvts.getParameter (ParamIds::uiScale))
                p->setValueNotifyingHost (p->convertTo0to1 (userScale));
        }
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
    g.fillAll (Theme::background);
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
        saveUserScale (scale);
    }

    repaint();
}

void IntersectEditor::saveUserScale (float scale)
{
    auto file = getUserSettingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText ("uiScale: " + juce::String (scale, 2) + "\n");
}

float IntersectEditor::loadUserScale()
{
    auto file = getUserSettingsFile();
    if (! file.existsAsFile())
        return -1.0f;

    auto content = file.loadFileAsString();
    for (auto line : juce::StringArray::fromLines (content))
    {
        line = line.trim();
        if (line.startsWith ("uiScale:"))
        {
            float val = line.fromFirstOccurrenceOf (":", false, false).trim().getFloatValue();
            if (val >= 0.5f && val <= 3.0f)
                return val;
        }
    }
    return -1.0f;
}
