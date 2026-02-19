#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/IntersectLookAndFeel.h"
#include "ui/HeaderBar.h"
#include "ui/SliceLane.h"
#include "ui/SliceControlBar.h"
#include "ui/WaveformView.h"
#include "ui/ScrollZoomBar.h"
#include "ui/ActionPanel.h"

class IntersectEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit IntersectEditor (IntersectProcessor&);
    ~IntersectEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    juce::StringArray getAvailableThemes();
    void applyTheme (const juce::String& themeName);

private:
    void timerCallback() override;
    void ensureDefaultThemes();
    void saveUserSettings (float scale, const juce::String& themeName);
    void loadUserSettings();

    IntersectProcessor& processor;
    float lastScale = -1.0f;  // sentinel so first timer tick always applies
    float savedScale = -1.0f;

    IntersectLookAndFeel lnf;
    HeaderBar       headerBar;
    SliceLane       sliceLane;
    WaveformView    waveformView;
    ScrollZoomBar   scrollZoomBar;
    SliceControlBar sliceControlBar;
    ActionPanel     actionPanel;

    juce::TooltipWindow tooltipWindow { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntersectEditor)
};
