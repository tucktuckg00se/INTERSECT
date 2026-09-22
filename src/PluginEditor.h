#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/IntersectLookAndFeel.h"
#include "ui/HeaderBar.h"
#include "ui/SampleLane.h"
#include "ui/browser/BrowserView.h"
#include "ui/SignalChainBar.h"
#include "ui/SliceLane.h"
#include "ui/WaveformView.h"
#include "ui/ScrollZoomBar.h"
#include "ui/ActionPanel.h"

class IntersectEditor : public juce::AudioProcessorEditor,
                             public juce::DragAndDropContainer,
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
    void setMiddleCOctave (int octave);
    int getMiddleCOctave() const { return middleCOctave; }
    float getEffectiveUiScale() const noexcept { return lastAppliedScale; }
    juce::File getCustomPresetsFolder() const { return browser.getCustomPresetsFolder(); }
    void setCustomPresetsFolder (const juce::File& folder);

private:
    enum class DeleteTarget
    {
        slice,
        sample,
    };

    void performContextualDelete();
    void timerCallback() override;
    void ensureDefaultThemes();
    void loadUserSettings();
    void setBrowserVisible (bool shouldBeVisible);
    void addFilesToKit (const std::vector<juce::File>& files);
    void replaceKitWithFiles (const std::vector<juce::File>& files);
    void loadPreset (const juce::File& preset);
    void handleDroppedFiles (const std::vector<juce::File>& files);
    void openFileDialog();
    void saveKitToLibrary();
    void exportPreset (const juce::File& preset, bool embedSamples);
    void persistSettings();
    juce::String getDefaultPresetName() const;
    float computeEffectiveScale (float desiredScale) const;
    bool updateUiTransform();
    void applyLogicalSize();

    IntersectProcessor& processor;
    int middleCOctave = 4;
    float lastScale = 1.0f;          // last desired scale persisted to settings
    float lastAppliedScale = 1.0f;   // effective (auto-fitted) scale currently in the transform
    float lastFitDesired = -1.0f;    // guard cache: forces first fit on first post-attach tick
    juce::Point<int> lastFitScreenPos;
    int fitCheckCounter = 0;
    float lastZoom = -1.0f;
    float lastScroll = -1.0f;
    float lastGlobalFadeCrossfade = -1.0f;
    int lastGlobalFadeLoopMode = -1;
    int lastGlobalFadeReverse = -1;
    int timerHz = 30;
    bool lastWaveformAnimating = false;
    bool lastPreviewActive = false;
    float savedScale = -1.0f;
    bool browserVisible = false;
    uint32_t lastUiSnapshotVersion = 0;
    uint32_t lastPresetSaveVersion = 0;
    DeleteTarget deleteTarget = DeleteTarget::slice;

    IntersectLookAndFeel lnf;
    BrowserView browser;
    HeaderBar       headerBar;
    SampleLane      sampleLane;
    SignalChainBar  signalChainBar;
    SliceLane       sliceLane;
    WaveformView    waveformView;
    ScrollZoomBar   scrollZoomBar;
    ActionPanel     actionPanel;

    juce::TooltipWindow tooltipWindow { this, 500 };
    std::unique_ptr<juce::FileChooser> presetChooser;
    std::unique_ptr<juce::FileChooser> openChooser;
    juce::File pendingRenamePreset;   // a SAVE whose new file should open for renaming once written
    juce::File lastExportFolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntersectEditor)
};
