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
    /** The MISSING label was clicked: relink this missing session sample. */
    std::function<void (int sampleId)> onRelinkRequested;
    /** SAVE: save the kit straight into the preset library. */
    std::function<void()> onSaveRequested;
    /** SET: open or close the settings view. */
    std::function<void()> onSettingsToggle;
    juce::String getTooltip() override;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    /** Lights the FILES button while the browser is open. */
    void setBrowserActive (bool isActive);
    /** Lights the SET button while the settings view is open. */
    void setSettingsActive (bool isActive);

private:
    IntersectProcessor& processor;
    juce::TextButton browserBtn { "FILES" };
    juce::TextButton saveBtn  { "SAVE" };
    juce::TextButton undoBtn  { "UNDO" };
    juce::TextButton redoBtn  { "REDO" };
    juce::TextButton panicBtn { "PANIC" };
    juce::TextButton settingsBtn { "SET" };

    juce::Rectangle<int> sampleInfoBounds;
};
