#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class IntersectProcessor;

class HeaderBar : public juce::Component,
                  public juce::TooltipClient
{
public:
    explicit HeaderBar (IntersectProcessor& p);
    std::function<void()> onBrowserToggle;
    juce::String getTooltip() override;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void adjustScale (float delta);

private:
    void showSettingsPopup();
    void openRelinkBrowser();

    IntersectProcessor& processor;
    juce::TextButton browserBtn { "FILES" };
    juce::TextButton undoBtn  { "UNDO" };
    juce::TextButton redoBtn  { "REDO" };
    juce::TextButton panicBtn { "PANIC" };
    juce::TextButton settingsBtn { "SET" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> sampleInfoBounds;
};
