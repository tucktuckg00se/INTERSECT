#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/**
    A small "are you sure?" popover just below anchorArea (in anchor's coordinates), right edges
    aligned: the message and CANCEL / confirm buttons. It is added to the plugin editor, so it
    scales with the UI and never opens a separate window (issue #41). Return confirms; Esc or a
    click outside cancels; keyboard focus goes back where it was.
*/
void showConfirmCallout (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                         const juce::String& message, const juce::String& confirmText,
                         std::function<void()> onConfirm);
