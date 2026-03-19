#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/IntersectLookAndFeel.h"
#include "ui/HeaderBar.h"
#include "ui/SignalChainBar.h"
#include "ui/SliceLane.h"
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
    void saveUserSettings (float scale, const juce::String& themeName);

private:
    void timerCallback() override;
    void ensureDefaultThemes();
    void loadUserSettings();

    IntersectProcessor& processor;
    float lastScale = 1.0f;   // last applied scale value, compared each tick
    bool scaleDirty = true;   // forces scale application on first timer tick
    float lastZoom = -1.0f;
    float lastScroll = -1.0f;
    int timerHz = 30;
    bool lastWaveformAnimating = false;
    bool lastPreviewActive = false;
    float savedScale = -1.0f;
    uint32_t lastUiSnapshotVersion = 0;

    IntersectLookAndFeel lnf;
    HeaderBar       headerBar;
    SignalChainBar  signalChainBar;
    SliceLane       sliceLane;
    WaveformView    waveformView;
    ScrollZoomBar   scrollZoomBar;
    ActionPanel     actionPanel;

    juce::TooltipWindow tooltipWindow { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntersectEditor)
};
