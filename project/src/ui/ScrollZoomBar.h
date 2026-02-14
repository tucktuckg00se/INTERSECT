#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;
class WaveformView;

class ScrollZoomBar : public juce::Component
{
public:
    ScrollZoomBar (IntersectProcessor& p, WaveformView& wv);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    IntersectProcessor& processor;
    WaveformView& waveformView;

    bool isDragging = false;
    float dragStartZoom = 1.0f;
    float dragAnchorFrac = 0.0f;   // sample fraction under cursor at drag start
    float dragAnchorPixelFrac = 0.0f; // cursor x as fraction of width at drag start
    int   dragStartX = 0;
    int   dragStartY = 0;
};
