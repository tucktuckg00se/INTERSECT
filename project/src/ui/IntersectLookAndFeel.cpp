#include "IntersectLookAndFeel.h"
#include "BinaryData.h"

static ThemeData globalTheme = ThemeData::darkTheme();

ThemeData& getTheme() { return globalTheme; }
void setTheme (const ThemeData& t) { globalTheme = t; }

IntersectLookAndFeel::IntersectLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, getTheme().background);

    regularTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::IBMPlexSansRegular_ttf, BinaryData::IBMPlexSansRegular_ttfSize);
    boldTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::IBMPlexSansBold_ttf, BinaryData::IBMPlexSansBold_ttfSize);
}

juce::Typeface::Ptr IntersectLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.isBold())
        return boldTypeface;
    return regularTypeface;
}

void IntersectLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour& /*bgColour*/,
                                                  bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat();

    // Use the button's own colour if it has been explicitly set
    auto btnCol = button.findColour (juce::TextButton::buttonColourId);
    auto baseBg = (btnCol != juce::Colour()) ? btnCol : getTheme().button;

    g.setColour (isDown ? baseBg.brighter (0.15f)
                        : isHighlighted ? baseBg.brighter (0.08f)
                                        : baseBg);
    g.fillRect (bounds);
}

void IntersectLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                            bool /*isHighlighted*/, bool /*isDown*/)
{
    auto textCol = button.findColour (button.getToggleState()
                                       ? juce::TextButton::textColourOnId
                                       : juce::TextButton::textColourOffId);
    g.setColour (textCol.isTransparent() ? getTheme().foreground : textCol);
    g.setFont (juce::Font (12.0f));
    g.drawText (button.getButtonText(), button.getLocalBounds(),
                juce::Justification::centred);
}

void IntersectLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (getTheme().darkBar);
    g.setColour (getTheme().separator);
    g.drawRect (0, 0, width, height, 1);
}

void IntersectLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                               bool isSeparator, bool isActive, bool isHighlighted,
                                               bool isTicked, bool /*hasSubMenu*/,
                                               const juce::String& text, const juce::String& /*shortcutText*/,
                                               const juce::Drawable* /*icon*/, const juce::Colour* /*textColour*/)
{
    if (isSeparator)
    {
        g.setColour (getTheme().separator);
        g.fillRect (area.reduced (4, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (getTheme().buttonHover);
        g.fillRect (area);
    }

    g.setColour (isTicked ? getTheme().accent
                          : (isActive ? getTheme().foreground : getTheme().foreground.withAlpha (0.4f)));
    g.setFont (getPopupMenuFont());
    g.drawText (text, area.reduced (8, 0), juce::Justification::centredLeft);
}

juce::Font IntersectLookAndFeel::getPopupMenuFont()
{
    return juce::Font (12.0f);
}

void IntersectLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll (getTheme().darkBar.brighter (0.1f));
    g.setColour (getTheme().separator);
    g.drawRect (0, 0, width, height, 1);
    g.setColour (getTheme().foreground);
    g.setFont (juce::Font (11.0f));
    g.drawText (text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

juce::Rectangle<int> IntersectLookAndFeel::getTooltipBounds (const juce::String& text,
                                                              juce::Point<int> screenPos,
                                                              juce::Rectangle<int> parentArea)
{
    int w = (int) juce::Font (11.0f).getStringWidthFloat (text) + 12;
    int h = 20;
    int x = screenPos.x;
    int y = screenPos.y + 18;

    if (x + w > parentArea.getRight())
        x = parentArea.getRight() - w;
    if (y + h > parentArea.getBottom())
        y = screenPos.y - h - 4;

    return { x, y, w, h };
}
