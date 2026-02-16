#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;

class HeaderBar : public juce::Component
{
public:
    explicit HeaderBar (IntersectProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    struct HeaderCell
    {
        int x, y, w, h;
        juce::String paramId;
        float minVal, maxVal, step;
        bool isChoice;
        bool isBoolean;
        bool isReadOnly = false;
        bool isSetBpm = false;
    };

    std::vector<HeaderCell> headerCells;

    void showTextEditor (const HeaderCell& cell);
    void showSetBpmPopup (bool forSampleDefault);
    void showThemePopup();
    void adjustScale (float delta);
    void openFileBrowser();
    void openRelinkBrowser();

    IntersectProcessor& processor;
    juce::TextButton loadBtn { "LOAD" };
    juce::TextButton themeBtn { "UI" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Drag state
    int activeDragCell = -1;
    float dragStartValue = 0.0f;
    int dragStartY = 0;

    // Text editor overlay
    std::unique_ptr<juce::TextEditor> textEditor;

    // Sample info area bounds for click-to-relink
    juce::Rectangle<int> sampleInfoBounds;
};
