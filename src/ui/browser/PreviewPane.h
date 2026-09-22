#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BrowserTypes.h"
#include "../../audio/AuditionPlayer.h"
#include <functional>
#include <vector>

class FileInfoCache;

/**
    Right-hand pane of the browser: what the selection is, its waveform and format (audio),
    its samples (presets), audition controls, and the actions (ADD / LOAD, LOAD PRESET, OPEN).
*/
class PreviewPane : public juce::Component
{
public:
    explicit PreviewPane (FileInfoCache& infoCache);

    void setSelection (std::vector<Browser::FileRow> selected, const Browser::FileRow* focused);
    void setAuditionStatus (const AuditionStatus& status);
    void infoUpdated();

    void setAutoPlay (bool shouldAutoPlay);
    bool getAutoPlay() const noexcept { return autoPlay; }
    void setGainDb (float db);
    float getGainDb() const noexcept { return gainDb; }
    void refreshThemeColours();

    void paint (juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onAdd;
    std::function<void()> onLoad;
    std::function<void()> onLoadPreset;
    std::function<void()> onOpenFolder;
    std::function<void()> onPlayToggle;
    std::function<void (bool)> onAutoPlayChanged;
    std::function<void (float)> onGainChanged;

    static constexpr float kMinGainDb = -24.0f;
    static constexpr float kMaxGainDb = 6.0f;
    static constexpr float kDefaultGainDb = -6.0f;

private:
    enum class Mode
    {
        none,
        audio,
        multi,
        preset,
        folder,
    };

    /** Horizontal audition volume control: drag, wheel, double-click resets. */
    class GainStrip : public juce::Component,
                      public juce::SettableTooltipClient
    {
    public:
        explicit GainStrip (PreviewPane& ownerIn) : owner (ownerIn) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    private:
        void setFromX (int x);
        PreviewPane& owner;
    };

    void updateButtons();
    void setGainFromUser (float db);
    int countSelectedAudio() const;
    bool auditionMatchesFocus() const;
    void paintWaveform (juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintMeta (juce::Graphics& g, juce::Rectangle<int> area) const;

    FileInfoCache& info;
    std::vector<Browser::FileRow> selection;
    Browser::FileRow focus;
    bool hasFocus = false;
    Mode mode = Mode::none;

    AuditionStatus audition;
    bool autoPlay = true;
    float gainDb = kDefaultGainDb;

    juce::TextButton playButton { "PLAY" };
    juce::TextButton autoButton { "AUTO" };
    juce::TextButton primaryButton;
    juce::TextButton secondaryButton;
    GainStrip gainStrip { *this };

    juce::Rectangle<int> titleArea, subtitleArea, waveArea, metaArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewPane)
};
