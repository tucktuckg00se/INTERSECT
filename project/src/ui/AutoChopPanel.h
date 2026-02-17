#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;
class WaveformView;

class AutoChopPanel : public juce::Component
{
public:
    AutoChopPanel (IntersectProcessor& p, WaveformView& wv);
    ~AutoChopPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void updatePreview();

    IntersectProcessor& processor;
    WaveformView& waveformView;

    juce::Slider sensitivitySlider;
    juce::TextEditor divisionsEditor;
    juce::TextButton splitEqualBtn { "Split Equal" };
    juce::TextButton detectBtn     { "Detect Transients" };
    juce::TextButton cancelBtn     { "Cancel" };
};
