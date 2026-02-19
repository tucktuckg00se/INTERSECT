#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

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
    void toggleAutoChop();
    bool isAutoChopOpen() const { return autoChopPanel != nullptr; }

private:
    IntersectProcessor& processor;
    WaveformView& waveformView;

    void updateMidiButtonAppearance (bool active);
    void updateSnapButtonAppearance (bool active);

    juce::TextButton addSliceBtn  { "ADD" };
    juce::TextButton lazyChopBtn  { "LAZY" };
    juce::TextButton dupBtn       { "COPY" };
    juce::TextButton splitBtn     { "AUTO" };
    juce::TextButton deleteBtn    { "DEL" };
    juce::TextButton snapBtn      { "ZX" };
    juce::TextButton midiSelectBtn { "FM" };

    std::unique_ptr<AutoChopPanel> autoChopPanel;
};
