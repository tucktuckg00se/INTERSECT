#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/**
    SET BPM as a popover (see Callout): how long the selected slice or sample is, picked from a
    dropdown, with the BPM that length gives shown before anything changes. SET (Return) applies;
    CANCEL, Esc or a click outside leaves everything as it was.

    lengthLabel  e.g. "Selected slice length"
    previewBpm   the BPM a choice would set; <= 0 when it can't be worked out
    onSet        the chosen index into choices
*/
void showSetBpmCallout (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                        const juce::String& lengthLabel,
                        const juce::StringArray& choices, int initialChoice,
                        std::function<float (int choice)> previewBpm,
                        std::function<void (int choice)> onSet);
