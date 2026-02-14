#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;

class SliceListPanel : public juce::Component
{
public:
    explicit SliceListPanel (IntersectProcessor& p);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    static constexpr int kBtnW = 90;
    static constexpr int kBtnH = 26;

    IntersectProcessor& processor;
};
