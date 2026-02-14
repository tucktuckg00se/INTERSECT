#include "PluginEditor.h"

static constexpr int kBaseW      = 750;
static constexpr int kBaseH      = 550;
static constexpr int kHeaderH    = 50;
static constexpr int kSliceLaneH = 24;
static constexpr int kScrollbarH = 22;
static constexpr int kSliceCtrlH = 48;
static constexpr int kActionH    = 36;
static constexpr int kMargin     = 8;

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

    float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    lastScale = scale;
    setSize ((int) (kBaseW * scale), (int) (kBaseH * scale));
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
    float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    setTransform (juce::AffineTransform::scale (scale));

    auto area = juce::Rectangle<int> (0, 0, kBaseW, kBaseH);

    // 1. Header (50px)
    headerBar.setBounds (area.removeFromTop (kHeaderH));

    // 2. SliceLane (24px)
    sliceLane.setBounds (area.removeFromTop (kSliceLaneH).reduced (kMargin, 0));

    // Bottom: ActionBar (36px)
    actionPanel.setBounds (area.removeFromBottom (kActionH).reduced (kMargin, 0));

    // SliceControlBar (48px)
    sliceControlBar.setBounds (area.removeFromBottom (kSliceCtrlH));

    // Scrollbar (22px)
    scrollZoomBar.setBounds (area.removeFromBottom (kScrollbarH).reduced (kMargin, 0));

    // Waveform (flexible)
    waveformView.setBounds (area.reduced (kMargin, 0));
}

void IntersectEditor::timerCallback()
{
    waveformView.rebuildCacheIfNeeded();

    // Check if scale changed
    float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    if (scale != lastScale)
    {
        lastScale = scale;
        setSize ((int) (kBaseW * scale), (int) (kBaseH * scale));
    }

    repaint();
}
