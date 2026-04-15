#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

class IntersectProcessor;
class StemExportPanel;
class WaveformView;

class SampleLane : public juce::Component
{
public:
    SampleLane (IntersectProcessor& p, WaveformView& wv);
    ~SampleLane() override;
    std::function<void()> onInteraction;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    bool isStemExportOpen() const { return stemExportPanel != nullptr; }

private:
    struct VisibleSample
    {
        int sampleId = 0;
        int index = 0;
        int x1 = 0;
        int x2 = 0;
        bool selected = false;
        juce::Colour colour;
        juce::String label;
        juce::Rectangle<int> stemsBounds;
        juce::Rectangle<int> deleteBounds;
    };

    std::vector<VisibleSample> buildVisibleSamples() const;
    int hitTestSample (juce::Point<int> pos) const;
    void showStemExportPanel (int sampleId);
    void dismissStemExportPanel();

    IntersectProcessor& processor;
    WaveformView& waveformView;
    std::unique_ptr<StemExportPanel> stemExportPanel;
    int dragSampleId = -1;
    int dragStartX = 0;
    int dragTargetIndex = -1;
    bool dragging = false;
};
