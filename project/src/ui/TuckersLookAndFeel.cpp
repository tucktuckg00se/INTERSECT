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
