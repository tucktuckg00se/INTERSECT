#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class IntersectProcessor;
class WaveformView;

class AutoChopPanel : public juce::Component
{
public:
    AutoChopPanel (IntersectProcessor& p, WaveformView& wv);
    ~AutoChopPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    struct ParamCell
    {
        juce::Rectangle<int> bounds;
        juce::String label;
        float value;
        float minVal, maxVal;
        float step;
        juce::String suffix;
    };

    void updatePreview();
    int hitTestCell (juce::Point<int> pos) const;
    void showTextEditor (ParamCell& cell);
    void dismissTextEditor();

    IntersectProcessor& processor;
    WaveformView& waveformView;

    ParamCell sensCell;
    ParamCell minCell;
    ParamCell divCell;

    juce::TextButton splitEqualBtn { "SPLIT EQUAL" };
    juce::TextButton detectBtn     { "SPLIT TRANSIENTS" };
    juce::TextButton cancelBtn     { "CANCEL" };

    int activeDragCell = -1;
    int dragStartY = 0;
    float dragStartValue = 0.0f;

    std::unique_ptr<juce::TextEditor> textEditor;
};
