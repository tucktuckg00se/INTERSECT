#include "IntersectLookAndFeel.h"
#include "BinaryData.h"

static ThemeData globalTheme = ThemeData::darkTheme();

namespace
{
float measureTextWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getWidth();
}
}

ThemeData& getTheme() { return globalTheme; }
void setTheme (const ThemeData& t) { globalTheme = t; }

juce::Typeface::Ptr IntersectLookAndFeel::sRegularTypeface;
juce::Typeface::Ptr IntersectLookAndFeel::sBoldTypeface;
float IntersectLookAndFeel::sMenuScale = 1.0f;

IntersectLookAndFeel::IntersectLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, getTheme().background);

    regularTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::Inter_24ptRegular_ttf, BinaryData::Inter_24ptRegular_ttfSize);
    boldTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::Inter_24ptBold_ttf, BinaryData::Inter_24ptBold_ttfSize);

    sRegularTypeface = regularTypeface;
    sBoldTypeface = boldTypeface;
}

juce::Font IntersectLookAndFeel::makeFont (float pointSize, bool bold)
{
    auto tf = bold ? sBoldTypeface : sRegularTypeface;
    if (tf != nullptr)
        return juce::Font (juce::FontOptions (tf).withPointHeight (pointSize));
    return juce::Font (juce::FontOptions().withHeight (pointSize));
}

juce::Font IntersectLookAndFeel::fitFontToWidth (const juce::String& text, float maxPointSize,
                                                 float minPointSize, int width, bool bold)
{
    auto font = makeFont (maxPointSize, bold);
    if (width <= 0 || text.isEmpty())
        return font;

    auto size = maxPointSize;
    while (size > minPointSize && measureTextWidth (font, text) > (float) width)
    {
        size -= 0.25f;
        font = makeFont (size, bold);
    }

    return font;
}

void IntersectLookAndFeel::drawShellButton (juce::Graphics& g,
                                            juce::Rectangle<float> bounds,
                                            const juce::Colour& fill,
                                            const juce::Colour& outline,
                                            float cornerRadius)
{
    auto buttonBounds = bounds.reduced (0.5f, 1.0f);
    g.setColour (fill);
    g.fillRoundedRectangle (buttonBounds, cornerRadius);
    g.setColour (outline);
    g.drawRoundedRectangle (buttonBounds, cornerRadius, 1.0f);
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
    const auto baseFill = button.findColour (juce::TextButton::buttonColourId).isTransparent()
        ? getTheme().button
        : button.findColour (juce::TextButton::buttonColourId);
    const auto textColour = button.findColour (button.getToggleState()
        ? juce::TextButton::textColourOnId
        : juce::TextButton::textColourOffId);

    auto fill = baseFill;
    auto outline = juce::Colour (0xFF181C24);

    if (isHighlighted)
    {
        fill = fill.brighter (0.08f);
        outline = juce::Colour (0xFF283040);
    }
    if (isDown)
        fill = fill.brighter (0.14f);

    if (button.getToggleState())
        outline = outline.interpolatedWith (textColour, 0.25f);

    drawShellButton (g, bounds, fill, outline, 4.0f);
}

void IntersectLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                            bool /*isHighlighted*/, bool /*isDown*/)
{
    auto textCol = button.findColour (button.getToggleState()
                                       ? juce::TextButton::textColourOnId
                                       : juce::TextButton::textColourOffId);
    g.setColour (textCol.isTransparent() ? getTheme().foreground : textCol);
    auto font = fitFontToWidth (button.getButtonText(),
                                juce::jlimit (8.5f, 11.0f, button.getHeight() * 0.44f),
                                7.0f,
                                button.getWidth() - 12,
                                false);
    g.setFont (font);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds(),
                      juce::Justification::centred, 1);
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
    g.drawText (text, area.reduced ((int) (8 * sMenuScale), 0), juce::Justification::centredLeft);
}

void IntersectLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g,
                                                        const juce::Rectangle<int>& area,
                                                        const juce::String& sectionName)
{
    g.setFont (getPopupMenuFont().boldened());
    g.setColour (getTheme().foreground);
    g.drawFittedText (sectionName,
                      area.getX() + (int) (12 * sMenuScale), area.getY(),
                      area.getWidth() - (int) (16 * sMenuScale),
                      (int) ((float) area.getHeight() * 0.8f),
                      juce::Justification::bottomLeft, 1);
}

juce::Font IntersectLookAndFeel::getPopupMenuFont()
{
    return makeFont (15.0f * sMenuScale);
}

void IntersectLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll (getTheme().darkBar.brighter (0.1f));
    g.setColour (getTheme().separator);
    g.drawRect (0, 0, width, height, 1);
    g.setColour (getTheme().foreground);
    g.setFont (makeFont (14.0f));
    g.drawText (text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

juce::Rectangle<int> IntersectLookAndFeel::getTooltipBounds (const juce::String& text,
                                                              juce::Point<int> screenPos,
                                                              juce::Rectangle<int> parentArea)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (makeFont (14.0f), text, 0.0f, 0.0f);
    int w = juce::roundToInt (glyphs.getBoundingBox (0, -1, true).getWidth()) + 14;
    int h = 24;
    int x = screenPos.x;
    int y = screenPos.y + 18;

    if (x + w > parentArea.getRight())
        x = parentArea.getRight() - w;
    if (y + h > parentArea.getBottom())
        y = screenPos.y - h - 4;

    return { x, y, w, h };
}
