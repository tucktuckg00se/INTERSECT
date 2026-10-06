#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class IntersectProcessor;

/** A text button that reports right-clicks and starts a drag-out when pressed and moved. */
class MidiExportButton : public juce::TextButton
{
public:
    std::function<void(const juce::MouseEvent&)> onDragStart;
    std::function<void()> onRightClick;

    explicit MidiExportButton (const juce::String& text) : juce::TextButton (text) {}

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            if (onRightClick != nullptr)
                onRightClick();
            return;
        }

        downPos = e.getPosition();
        dragStarted = false;
        juce::TextButton::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // A small movement threshold keeps plain clicks from starting a drag.
        if (! dragStarted && e.getPosition().getDistanceFrom (downPos) >= 4.0f)
        {
            dragStarted = true;
            if (onDragStart != nullptr)
                onDragStart (e);
        }

        juce::TextButton::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        juce::TextButton::mouseUp (e);
    }

private:
    juce::Point<int> downPos {};
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
    /** MIDI button: drag started outside the window (export a .mid file). */
    std::function<void(const juce::MouseEvent&)> onMidiDragStart;
    /** MIDI button: right-click (save the kit as a .mid file via browser). */
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
