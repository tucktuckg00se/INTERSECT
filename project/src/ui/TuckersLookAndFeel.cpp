#include "TuckersLookAndFeel.h"

IntersectLookAndFeel::IntersectLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Theme::background);
}

void IntersectLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour& /*bgColour*/,
                                                  bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    g.setColour (isDown ? Theme::buttonHover.brighter (0.1f)
                        : isHighlighted ? Theme::buttonHover
                                        : Theme::button);
    g.fillRect (bounds);
}

void IntersectLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                            bool /*isHighlighted*/, bool /*isDown*/)
{
    g.setColour (Theme::foreground);
    g.setFont (juce::Font (12.0f));
    g.drawText (button.getButtonText(), button.getLocalBounds(),
                juce::Justification::centred);
}

void IntersectLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (Theme::darkBar);
    g.setColour (Theme::separator);
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
        g.setColour (Theme::separator);
        g.fillRect (area.reduced (4, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (Theme::buttonHover);
        g.fillRect (area);
    }

    g.setColour (isTicked ? Theme::accent
                          : (isActive ? Theme::foreground : Theme::foreground.withAlpha (0.4f)));
    g.setFont (getPopupMenuFont());
    g.drawText (text, area.reduced (8, 0), juce::Justification::centredLeft);
}

juce::Font IntersectLookAndFeel::getPopupMenuFont()
{
    return juce::Font (12.0f);
}
