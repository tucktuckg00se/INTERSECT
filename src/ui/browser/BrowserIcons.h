#pragma once
#include <juce_graphics/juce_graphics.h>

// Small vector icons for the browser. Inter has no glyphs for these, and non-ASCII text
// mis-renders on some toolchains, so they are drawn as paths.
namespace BrowserIcons
{
enum class Icon
{
    folder,
    folderOpen,
    audio,
    preset,
    bookmark,
    drive,
    recent,
    home,
};

inline void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    auto box = area.withSizeKeepingCentre (size, size);
    const float stroke = juce::jmax (1.0f, size * 0.09f);
    g.setColour (colour);

    switch (icon)
    {
        case Icon::folder:
        {
            auto body = box.reduced (size * 0.06f, size * 0.2f).translated (0.0f, size * 0.04f);
            juce::Path p;
            p.startNewSubPath (body.getX(), body.getY());
            p.lineTo (body.getX() + body.getWidth() * 0.38f, body.getY());
            p.lineTo (body.getX() + body.getWidth() * 0.48f, body.getY() + body.getHeight() * 0.16f);
            p.lineTo (body.getRight(), body.getY() + body.getHeight() * 0.16f);
            p.lineTo (body.getRight(), body.getBottom());
            p.lineTo (body.getX(), body.getBottom());
            p.closeSubPath();
            g.fillPath (p);
            break;
        }

        case Icon::folderOpen:
        {
            // Back of the folder with its tab, and the front flap tilted open.
            auto body = box.reduced (size * 0.06f, size * 0.18f).translated (0.0f, size * 0.03f);
            juce::Path back;
            back.startNewSubPath (body.getX(), body.getBottom());
            back.lineTo (body.getX(), body.getY());
            back.lineTo (body.getX() + body.getWidth() * 0.36f, body.getY());
            back.lineTo (body.getX() + body.getWidth() * 0.46f, body.getY() + body.getHeight() * 0.16f);
            back.lineTo (body.getRight() - body.getWidth() * 0.1f, body.getY() + body.getHeight() * 0.16f);
            back.lineTo (body.getRight() - body.getWidth() * 0.1f, body.getY() + body.getHeight() * 0.36f);
            g.strokePath (back, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path front;
            front.startNewSubPath (body.getX(), body.getBottom());
            front.lineTo (body.getX() + body.getWidth() * 0.18f, body.getY() + body.getHeight() * 0.42f);
            front.lineTo (body.getRight(), body.getY() + body.getHeight() * 0.42f);
            front.lineTo (body.getRight() - body.getWidth() * 0.18f, body.getBottom());
            front.closeSubPath();
            g.fillPath (front);
            break;
        }

        case Icon::audio:
        {
            // Five bars of different heights: a tiny waveform.
            const float heights[] = { 0.35f, 0.75f, 1.0f, 0.55f, 0.3f };
            const auto inner = box.reduced (size * 0.1f);
            const float barW = inner.getWidth() / 9.0f;
            for (int i = 0; i < 5; ++i)
            {
                const float h = inner.getHeight() * heights[i];
                g.fillRoundedRectangle (inner.getX() + barW * (float) (i * 2), inner.getCentreY() - h * 0.5f,
                                        barW, h, barW * 0.4f);
            }
            break;
        }

        case Icon::preset:
        {
            // 2x2 pads: a kit.
            const auto inner = box.reduced (size * 0.12f);
            const float gap = size * 0.1f;
            const float pad = (inner.getWidth() - gap) * 0.5f;
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 2; ++c)
                    g.fillRoundedRectangle (inner.getX() + (pad + gap) * (float) c,
                                            inner.getY() + (pad + gap) * (float) r,
                                            pad, pad, pad * 0.25f);
            break;
        }

        case Icon::bookmark:
        {
            juce::Path p;
            p.addStar (box.getCentre(), 5, size * 0.2f, size * 0.46f, 0.0f);
            g.fillPath (p);
            break;
        }

        case Icon::drive:
        {
            auto body = box.reduced (size * 0.08f, size * 0.26f);
            g.drawRoundedRectangle (body, size * 0.08f, stroke);
            g.fillEllipse (body.getRight() - size * 0.24f, body.getCentreY() - size * 0.06f, size * 0.12f, size * 0.12f);
            break;
        }

        case Icon::recent:
        {
            auto face = box.reduced (size * 0.12f);
            g.drawEllipse (face, stroke);
            const auto c = face.getCentre();
            g.drawLine (c.x, c.y, c.x, face.getY() + face.getHeight() * 0.22f, stroke);
            g.drawLine (c.x, c.y, face.getRight() - face.getWidth() * 0.25f, c.y, stroke);
            break;
        }

        case Icon::home:
        {
            // Solid house (roof + walls as one shape) with the door cut out, matching the filled
            // folder icons. Even-odd winding makes the door sub-path a hole.
            const auto inner = box.reduced (size * 0.08f);
            const float eaveY = inner.getY() + inner.getHeight() * 0.46f;
            const float wallL = inner.getX() + inner.getWidth() * 0.16f;
            const float wallR = inner.getRight() - inner.getWidth() * 0.16f;

            juce::Path p;
            p.setUsingNonZeroWinding (false);
            p.startNewSubPath (inner.getCentreX(), inner.getY());
            p.lineTo (inner.getRight(), eaveY);
            p.lineTo (wallR, eaveY);
            p.lineTo (wallR, inner.getBottom());
            p.lineTo (wallL, inner.getBottom());
            p.lineTo (wallL, eaveY);
            p.lineTo (inner.getX(), eaveY);
            p.closeSubPath();

            const float doorW = inner.getWidth() * 0.22f;
            p.addRectangle (inner.getCentreX() - doorW * 0.5f, inner.getBottom() - inner.getHeight() * 0.34f,
                            doorW, inner.getHeight() * 0.34f);
            g.fillPath (p);
            break;
        }
    }
}

/** A small filled triangle pointing up or down, used for sort direction. */
inline void drawSortArrow (juce::Graphics& g, juce::Rectangle<float> area, bool ascending, juce::Colour colour)
{
    const auto box = area.withSizeKeepingCentre (6.0f, 4.0f);
    juce::Path p;
    if (ascending)
        p.addTriangle (box.getX(), box.getBottom(), box.getRight(), box.getBottom(), box.getCentreX(), box.getY());
    else
        p.addTriangle (box.getX(), box.getY(), box.getRight(), box.getY(), box.getCentreX(), box.getBottom());
    g.setColour (colour);
    g.fillPath (p);
}

/** A small chevron pointing right, used between breadcrumb segments. */
inline void drawChevron (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto box = area.withSizeKeepingCentre (4.0f, 7.0f);
    juce::Path p;
    p.startNewSubPath (box.getX(), box.getY());
    p.lineTo (box.getRight(), box.getCentreY());
    p.lineTo (box.getX(), box.getBottom());
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}
