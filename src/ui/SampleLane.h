#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;

class SampleLane : public juce::Component
{
public:
    explicit SampleLane (IntersectProcessor& p);
    std::function<void()> onInteraction;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

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
    };

    std::vector<VisibleSample> buildVisibleSamples() const;
    int hitTestSample (int x) const;

    IntersectProcessor& processor;
    int dragSampleId = -1;
    int dragStartX = 0;
    int dragTargetIndex = -1;
    bool dragging = false;
};
