#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class IntersectProcessor;

/** A text button that also starts a drag-out when pressed and moved. */
class MidiExportButton : public juce::TextButton
{
public:
    std::function<void(const juce::MouseEvent&)> onDragStart;

    using juce::TextButton::TextButton;

    /** True while handling the release of a press that turned into a drag (onClick should ignore it). */
    bool lastPressWasDrag() const { return dragStarted; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        juce::TextButton::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // A small movement threshold keeps plain clicks from starting a drag.
        if (! dragStarted && e.getDistanceFromDragStart() >= 4)
        {
            dragStarted = true;
            if (onDragStart != nullptr)
                onDragStart (e);
        }

        juce::TextButton::mouseDrag (e);
    }

private:
    bool dragStarted = false;
};

class HeaderBar : public juce::Component,
                  public juce::TooltipClient
{
public:
    explicit HeaderBar (IntersectProcessor& p);
    std::function<void()> onBrowserToggle;
    /** SAVE: save the kit straight into the preset library. */
    std::function<void()> onSaveRequested;
    /** MIDI button: drag started (export the slices as a .mid file to drop in a DAW). */
    std::function<void(const juce::MouseEvent&)> onMidiDragStart;
    /** MIDI button: click (save the slices as a .mid file via a file browser). */
    std::function<void()> onMidiSaveRequested;
    /** The MIDI export button (drag source for external drags). */
    juce::Component* getMidiButton() { return &midiBtn; }
    juce::String getTooltip() override;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void adjustScale (float delta);
    /** Lights the FILES button while the browser is open. */
    void setBrowserActive (bool isActive);

private:
    void showSettingsPopup();
    void openRelinkBrowser();

    IntersectProcessor& processor;
    juce::TextButton browserBtn { "FILES" };
    juce::TextButton saveBtn  { "SAVE" };
    MidiExportButton midiBtn  { "MIDI" };
    juce::TextButton undoBtn  { "UNDO" };
    juce::TextButton redoBtn  { "REDO" };
    juce::TextButton panicBtn { "PANIC" };
    juce::TextButton settingsBtn { "SET" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> sampleInfoBounds;
};
