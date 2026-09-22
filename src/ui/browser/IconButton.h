#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BrowserIcons.h"
#include "../IntersectLookAndFeel.h"

/** A toolbar button that draws a BrowserIcons icon, with the same outline styling as the text
    buttons around it (background comes from IntersectLookAndFeel::drawButtonBackground). */
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, BrowserIcons::Icon iconIn)
        : juce::Button (name), icon (iconIn)
    {
        getProperties().set (IntersectLookAndFeel::outlineOnlyButtonProperty, true);
    }

    void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId),
                                               shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        auto colour = findColour (juce::TextButton::textColourOffId);
        if (colour.isTransparent())
            colour = getTheme().text2;
        if (! isEnabled())
            colour = colour.withMultipliedAlpha (0.4f);

        const float inset = (float) juce::jmin (getWidth(), getHeight()) * 0.22f;
        BrowserIcons::draw (g, icon, getLocalBounds().toFloat().reduced (inset), colour);
    }

private:
    BrowserIcons::Icon icon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};
