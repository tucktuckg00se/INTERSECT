#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "IntersectLookAndFeel.h"

/**
    The bottom bar shared by the full views (FILES, STEMS, SET): a line along the top, the view's
    own items on the left and CLOSE at the far right. One definition keeps the three identical.
*/
namespace FullViewBar
{
constexpr int kHeight = 34;
constexpr int kCloseWidth = 60;

/** The area inside the bar that controls are laid out in. */
inline juce::Rectangle<int> content (juce::Rectangle<int> bar) { return bar.reduced (8, 4); }

inline void paintTopLine (juce::Graphics& g, juce::Rectangle<int> bar)
{
    g.setColour (getTheme().surface4.withAlpha (0.85f));
    g.fillRect (bar.withHeight (1));
}

/** Filled button style used in the bar (CLOSE, START, ...). No keyboard focus, so Esc keeps
    reaching the view. */
inline void styleButton (juce::TextButton& button)
{
    button.setWantsKeyboardFocus (false);
    button.setColour (juce::TextButton::buttonColourId, getTheme().surface4.withAlpha (0.95f));
    button.setColour (juce::TextButton::textColourOnId, getTheme().text2.withAlpha (0.88f));
    button.setColour (juce::TextButton::textColourOffId, getTheme().text2.withAlpha (0.88f));
}
} // namespace FullViewBar
