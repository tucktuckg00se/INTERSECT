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
    setColour (juce::ResizableWindow::backgroundColourId, getTheme().surface1);

    regularTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::Inter_24ptRegular_ttf, BinaryData::Inter_24ptRegular_ttfSize);
    boldTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::Inter_24ptBold_ttf, BinaryData::Inter_24ptBold_ttfSize);

    sRegularTypeface = regularTypeface;
    sBoldTypeface = boldTypeface;
}

IntersectLookAndFeel::~IntersectLookAndFeel()
{
    if (sRegularTypeface == regularTypeface)
        sRegularTypeface = nullptr;

    if (sBoldTypeface == boldTypeface)
        sBoldTypeface = nullptr;

    regularTypeface = nullptr;
    boldTypeface = nullptr;
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
    const bool outlineOnly = static_cast<bool> (button.getProperties()[outlineOnlyButtonProperty]);
    const auto baseFill = button.findColour (juce::TextButton::buttonColourId).isTransparent()
        ? (outlineOnly ? juce::Colours::transparentBlack : getTheme().surface4)
        : button.findColour (juce::TextButton::buttonColourId);
    const auto textColour = button.findColour (button.getToggleState()
        ? juce::TextButton::textColourOnId
        : juce::TextButton::textColourOffId);

    auto fill = outlineOnly ? juce::Colours::transparentBlack : baseFill;
    auto outline = outlineOnly ? baseFill : juce::Colour (0xFF181C24);

    if (isHighlighted)
    {
        if (! outlineOnly)
            fill = fill.brighter (0.08f);

        outline = outlineOnly ? outline.brighter (0.06f)
                              : juce::Colour (0xFF283040);
    }
    if (isDown)
    {
        if (! outlineOnly)
            fill = fill.brighter (0.14f);

        outline = outlineOnly ? outline.brighter (0.14f)
                              : outline;
    }

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
    g.setColour (textCol.isTransparent() ? getTheme().text2 : textCol);
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
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRect (bounds.translated (0.0f, 2.0f));

    g.setColour (getTheme().surface2.brighter (0.03f).withAlpha (0.985f));
    g.fillRect (bounds);

    g.setColour (getTheme().surface1.withAlpha (0.35f));
    g.drawRect (bounds.reduced (0.5f), 1.0f);

    g.setColour (getTheme().surface5.withAlpha (0.9f));
    g.drawRect (bounds, 1.0f);
}

void IntersectLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                               bool isSeparator, bool isActive, bool isHighlighted,
                                               bool isTicked, bool hasSubMenu,
                                               const juce::String& text, const juce::String& /*shortcutText*/,
                                               const juce::Drawable* /*icon*/, const juce::Colour* /*textColour*/)
{
    if (isSeparator)
    {
        auto line = area.reduced ((int) std::round (12.0f * sMenuScale), 0);
        line = line.withHeight (1).withY (area.getCentreY());
        g.setColour (getTheme().surface5.withAlpha (0.9f));
        g.fillRect (line);
        return;
    }

    auto row = area.reduced ((int) std::round (6.0f * sMenuScale),
                             (int) std::round (2.0f * sMenuScale));
    const float highlightRadius = 4.0f * sMenuScale;

    if (isHighlighted && isActive)
    {
        auto fill = getTheme().surface4.interpolatedWith (getTheme().accent, 0.14f);
        g.setColour (fill.withAlpha (0.96f));
        g.fillRoundedRectangle (row.toFloat(), highlightRadius);
        g.setColour (getTheme().accent.withAlpha (0.28f));
        g.drawRoundedRectangle (row.toFloat(), highlightRadius, 1.0f);
    }

    auto glyphBounds = row.removeFromLeft ((int) std::round (18.0f * sMenuScale));
    auto arrowBounds = hasSubMenu ? row.removeFromRight ((int) std::round (14.0f * sMenuScale)) : juce::Rectangle<int>();
    auto textBounds = row.reduced ((int) std::round (6.0f * sMenuScale), 0);

    if (isTicked)
    {
        auto dotBounds = glyphBounds.withSizeKeepingCentre ((int) std::round (8.0f * sMenuScale),
                                                            (int) std::round (8.0f * sMenuScale));
        g.setColour (getTheme().accent.withAlpha (isActive ? 1.0f : 0.45f));
        g.fillEllipse (dotBounds.toFloat());
    }

    auto textColour = isActive ? getTheme().text2
                               : getTheme().text0.withAlpha (0.62f);
    if (isTicked && isActive)
        textColour = isHighlighted ? getTheme().text2 : getTheme().accent.brighter (0.1f);

    if (hasSubMenu && arrowBounds.getWidth() > 0)
    {
        juce::Path arrow;
        const float x = (float) arrowBounds.getX() + 4.0f * sMenuScale;
        const float y = (float) arrowBounds.getCentreY();
        const float w = 4.0f * sMenuScale;
        const float h = 5.0f * sMenuScale;
        arrow.startNewSubPath (x, y - h);
        arrow.lineTo (x + w, y);
        arrow.lineTo (x, y + h);
        g.setColour (textColour.withAlpha (isActive ? 0.9f : 0.5f));
        g.strokePath (arrow, juce::PathStrokeType (1.2f));
    }

    g.setColour (textColour);
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, textBounds, juce::Justification::centredLeft, 1);
}

void IntersectLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g,
                                                        const juce::Rectangle<int>& area,
                                                        const juce::String& sectionName)
{
    auto bounds = area.reduced ((int) std::round (12.0f * sMenuScale), 0);
    auto labelFont = makeFont (8.75f * sMenuScale, true);
    const auto labelText = sectionName.toUpperCase();
    const int labelWidth = juce::roundToInt (measureTextWidth (labelFont, labelText));

    g.setFont (labelFont);
    g.setColour (getTheme().text0.brighter (0.08f).withAlpha (0.92f));
    g.drawText (labelText, bounds.removeFromLeft (labelWidth + (int) std::round (8.0f * sMenuScale)),
                juce::Justification::bottomLeft, true);

    auto ruleBounds = bounds.withHeight (1).withY (area.getBottom() - (int) std::round (5.0f * sMenuScale));
    if (ruleBounds.getWidth() > 0)
    {
        g.setColour (getTheme().surface5.withAlpha (0.8f));
        g.fillRect (ruleBounds);
    }
}

juce::Font IntersectLookAndFeel::getPopupMenuFont()
{
    return makeFont (12.0f * sMenuScale);
}

void IntersectLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& textEditor)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (textEditor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (bounds, 3.0f);
}

void IntersectLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& textEditor)
{
    if (! textEditor.isEnabled())
        return;

    const bool focused = textEditor.hasKeyboardFocus (true) && ! textEditor.isReadOnly();
    auto outline = textEditor.findColour (focused ? juce::TextEditor::focusedOutlineColourId
                                                  : juce::TextEditor::outlineColourId);

    if (outline.isTransparent())
        return;

    g.setColour (outline.withAlpha (focused ? 0.92f : 0.72f));
    g.drawRoundedRectangle (juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f),
                            3.0f, 1.0f);
}

void IntersectLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll (getTheme().surface2.brighter (0.1f));
    g.setColour (getTheme().surface5);
    g.drawRect (0, 0, width, height, 1);
    g.setColour (getTheme().text2);
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
