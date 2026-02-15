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

    juce::TextButton addSliceBtn  { "ADD" };
    juce::TextButton lazyChopBtn  { "LAZY" };
    juce::TextButton dupBtn       { "COPY" };
    juce::TextButton splitBtn     { "AUTO" };
    juce::TextButton deleteBtn    { "DEL" };
};
