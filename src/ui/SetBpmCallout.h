#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/** The span SET BPM measures, as the user picked it: some bars of a time signature, or one 1/N note. */
struct SetBpmLength
{
    enum class Mode { bars, note };

    static constexpr int kMaxSignature = 64;
    static constexpr int kMaxBars = 256;
    static constexpr int kMaxNoteDenominator = 128;

    Mode mode = Mode::bars;
    int sigNumerator = 4;
    int sigDenominator = 4;
    int bars = 1;
    int noteDenominator = 4;   // NOTE: 1/4 note

    /** The length in 4/4 bars (what SET BPM and CmdStretch take), or 0 when a number is out of range.
        1 bar of 7/8 = 0.875; a 1/6 note (quarter-note triplet) = 1/6. */
    float inFourFourBars() const noexcept;
};

/**
    SET BPM as a popover (see Callout): how long the selected slice or sample is, as BAR (time signature
    and a number of bars) or NOTE (1/N), with the BPM it gives shown before anything changes. The main
    number is focused and selected, so typing replaces it; Tab moves on, Up/Down and the wheel step.
    SET (Return) applies; CANCEL, Esc or a click outside leaves everything as it was.

    lengthLabel  e.g. "Selected slice length"
    previewBpm   the BPM a length in 4/4 bars would set; <= 0 when it can't be worked out
    onSet        the length picked (also what the popover should open on next time)
*/
void showSetBpmCallout (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                        const juce::String& lengthLabel,
                        const SetBpmLength& initial,
                        std::function<float (float bars)> previewBpm,
                        std::function<void (const SetBpmLength&)> onSet);
