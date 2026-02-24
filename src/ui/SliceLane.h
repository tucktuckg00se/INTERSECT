#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;
class WaveformView;

class SliceLane : public juce::Component
{
public:
    explicit SliceLane (IntersectProcessor& p);
    void setWaveformView (WaveformView* view) { waveformView = view; }
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    IntersectProcessor& processor;
    WaveformView* waveformView = nullptr;
};
