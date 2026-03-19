#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class IntersectProcessor;
class WaveformView;
class AutoChopPanel;

class ActionPanel : public juce::Component
{
public:
    ActionPanel (IntersectProcessor& p, WaveformView& wv);
    ~ActionPanel() override;
    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void triggerAddSliceMode();
    void triggerLazyChop();
    void triggerDuplicateSlice();
    void triggerAutoChop();
    void triggerDeleteSelectedSlice();
    void toggleSnapToZeroCrossing();
    void toggleFollowMidiSelection();
    void toggleAutoChop();
    bool isAutoChopOpen() const { return autoChopPanel != nullptr; }

private:
    IntersectProcessor& processor;
    WaveformView& waveformView;

    struct ActionItem
    {
        juce::String text;
        juce::Rectangle<int> bounds;
        int id;
        bool isNarrow;
    };

    std::vector<ActionItem> items;
    int hoveredIndex = -1;

    int hitTestItem (juce::Point<int> pos) const;

    std::unique_ptr<AutoChopPanel> autoChopPanel;
};
