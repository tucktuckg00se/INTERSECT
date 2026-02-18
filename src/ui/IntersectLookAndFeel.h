#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ThemeData.h"

ThemeData& getTheme();
void setTheme (const ThemeData& t);

class IntersectLookAndFeel : public juce::LookAndFeel_V4
{
public:
    IntersectLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override;
    juce::Font getPopupMenuFont() override;

    static void setMenuScale (float s) { sMenuScale = s; }
    static float getMenuScale() { return sMenuScale; }

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override;

    static juce::Font makeFont (float pointSize, bool bold = false);

private:
    static juce::Typeface::Ptr sRegularTypeface;
    static juce::Typeface::Ptr sBoldTypeface;
    static float sMenuScale;

    juce::Typeface::Ptr regularTypeface;
    juce::Typeface::Ptr boldTypeface;
};
