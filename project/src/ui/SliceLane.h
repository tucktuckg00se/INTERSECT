#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;

class SliceLane : public juce::Component
{
public:
    explicit SliceLane (IntersectProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    IntersectProcessor& processor;
    juce::TextButton dupBtn { "D" };
    juce::TextButton midiSelectBtn { "M" };
};
