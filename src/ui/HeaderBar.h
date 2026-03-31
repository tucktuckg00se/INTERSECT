#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class IntersectProcessor;

class HeaderBar : public juce::Component,
                  public juce::TooltipClient
{
public:
    explicit HeaderBar (IntersectProcessor& p);
    juce::String getTooltip() override;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void showSettingsPopup();
    void adjustScale (float delta);
    void openFileBrowser();
    void openRelinkBrowser();

    IntersectProcessor& processor;
    juce::TextButton undoBtn  { "UNDO" };
    juce::TextButton redoBtn  { "REDO" };
    juce::TextButton panicBtn { "PANIC" };
    juce::TextButton loadBtn  { "LOAD" };
    juce::TextButton settingsBtn { "SET" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> sampleInfoBounds;
};
