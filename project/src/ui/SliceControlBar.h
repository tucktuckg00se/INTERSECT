#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;

class SliceControlBar : public juce::Component
{
public:
    explicit SliceControlBar (IntersectProcessor& p);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    struct ParamCell
    {
        int x, y, w, h;
        uint32_t lockBit;
        int fieldId;       // SliceParamField enum value
        float minVal, maxVal, step;
        bool isBoolean;    // for ping-pong toggle
        bool isChoice;     // for algorithm popup
        bool isSetBpm = false;
    };

    std::vector<ParamCell> cells;

    void drawParamCell (juce::Graphics& g, int x, int y, const juce::String& label,
                        const juce::String& value, bool locked, uint32_t lockBit,
                        int fieldId, float minVal, float maxVal, float step,
                        bool isBoolean, bool isChoice, int& outWidth);
    void drawLockIcon (juce::Graphics& g, int x, int y, bool locked);
    void showTextEditor (const ParamCell& cell, float currentValue);
    void showSetBpmPopup();

    IntersectProcessor& processor;

    // Drag state
    int activeDragCell = -1;
    float dragStartValue = 0.0f;
    int dragStartY = 0;

    // Root note cell (editable when no slices exist)
    juce::Rectangle<int> rootNoteArea;
    bool draggingRootNote = false;

    // Text editor overlay
    std::unique_ptr<juce::TextEditor> textEditor;
};
