#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;
class WaveformView;

class ScrollZoomBar : public juce::Component
{
public:
    ScrollZoomBar (IntersectProcessor& p, WaveformView& wv);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> getThumbBounds() const;
    juce::Rectangle<int> getTrackBounds() const;

    IntersectProcessor& processor;
    WaveformView& waveformView;

    juce::TextButton scrollLeft  { "<" };
    juce::TextButton scrollRight { ">" };
    juce::TextButton zoomOut     { "-" };
    juce::TextButton zoomIn      { "+" };

    bool isDraggingThumb = false;
    float dragStartScroll = 0.0f;
    float dragStartZoom = 1.0f;
    int   dragStartX = 0;
    int   dragStartY = 0;
};
