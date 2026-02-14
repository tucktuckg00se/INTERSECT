#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;
class WaveformView;

class ActionPanel : public juce::Component
{
public:
    ActionPanel (IntersectProcessor& p, WaveformView& wv);
    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    IntersectProcessor& processor;
    WaveformView& waveformView;

    juce::TextButton addSliceBtn  { "+SLC" };
    juce::TextButton lazyChopBtn  { "LZY" };
    juce::TextButton dupBtn       { "DUP" };
    juce::TextButton deleteBtn    { "DEL" };
};
