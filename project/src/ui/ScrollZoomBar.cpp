#include "ScrollZoomBar.h"
#include "IntersectLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"
#include <cmath>

ScrollZoomBar::ScrollZoomBar (IntersectProcessor& p, WaveformView& wv)
    : processor (p), waveformView (wv)
{
}

void ScrollZoomBar::paint (juce::Graphics& g)
{
    int w = getWidth();
    int h = getHeight();

    // Slightly lighter background so it reads as a ruler even when empty
    g.fillAll (getTheme().darkBar.brighter (0.04f));

    // Top edge line
    g.setColour (getTheme().separator);
    g.drawHorizontalLine (0, 0.0f, (float) w);

    int numFrames = processor.sampleData.getNumFrames();
    if (numFrames <= 0 || w <= 0)
        return;

    float z = processor.zoom.load();
    float sc = processor.scroll.load();
    float viewFrac = 1.0f / z;
    float viewStart = sc * (1.0f - viewFrac);
    float viewEnd = viewStart + viewFrac;

    // Choose major tick spacing — aim for ~80-150px between labelled ticks
    float viewPercent = viewFrac * 100.0f;
    float rawStep = viewPercent * 80.0f / (float) w;
    // Nice values spanning deep zoom to full view
    const float niceSteps[] = { 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f,
                                1.0f, 2.0f, 5.0f, 10.0f, 25.0f, 50.0f };
    float majorStep = 50.0f;
    for (float ns : niceSteps)
    {
        if (ns >= rawStep) { majorStep = ns; break; }
    }

    float startPct = viewStart * 100.0f;
    float endPct = viewEnd * 100.0f;

    // Minor ticks: 4 subdivisions between each major tick
    float minorStep = majorStep / 4.0f;
    float firstMinor = std::floor (startPct / minorStep) * minorStep;

    for (float pct = firstMinor; pct <= endPct; pct += minorStep)
    {
        float frac = pct / 100.0f;
        float px = (frac - viewStart) / viewFrac * (float) w;
        int tx = (int) px;
        if (tx < 0 || tx >= w) continue;

        // Is this a major tick?
        bool isMajor = (std::fmod (pct + 0.0001f, majorStep) < minorStep * 0.5f);

        if (isMajor)
        {
            g.setColour (getTheme().foreground.withAlpha (0.3f));
            g.drawVerticalLine (tx, 1.0f, (float) h * 0.5f);

            // Label
            g.setFont (IntersectLookAndFeel::makeFont (10.0f));
            g.setColour (getTheme().foreground.withAlpha (0.45f));
            juce::String label;
            if (majorStep >= 1.0f)
                label = juce::String ((int) std::round (pct)) + "%";
            else
                label = juce::String (pct, 1) + "%";
            int labelW = 36;
            int labelX = tx - labelW / 2;
            labelX = std::max (0, std::min (w - labelW, labelX));
            g.drawText (label, labelX, (int) (h * 0.45f), labelW, (int) (h * 0.55f),
                         juce::Justification::centred);
        }
        else
        {
            g.setColour (getTheme().foreground.withAlpha (0.15f));
            g.drawVerticalLine (tx, 1.0f, (float) h * 0.3f);
        }
    }
}

void ScrollZoomBar::mouseDown (const juce::MouseEvent& e)
{
    isDragging = true;
    dragStartZoom = processor.zoom.load();
    dragStartX = e.x;
    dragStartY = e.y;

    // Record the sample fraction under the cursor so zoom can pivot here
    int w = getWidth();
    float z = dragStartZoom;
    float sc = processor.scroll.load();
    float viewFrac = 1.0f / z;
    float viewStart = sc * (1.0f - viewFrac);

    dragAnchorPixelFrac = (w > 0) ? (float) e.x / (float) w : 0.5f;
    dragAnchorFrac = viewStart + dragAnchorPixelFrac * viewFrac;
    dragAnchorFrac = juce::jlimit (0.0f, 1.0f, dragAnchorFrac);
}

void ScrollZoomBar::mouseDrag (const juce::MouseEvent& e)
{
    if (! isDragging)
        return;

    int w = getWidth();
    if (w <= 0) return;

    // Vertical drag: zoom
    float deltaY = (float) (e.y - dragStartY);
    float zoomFactor = std::pow (1.01f, deltaY);
    float newZoom = juce::jlimit (1.0f, 2048.0f, dragStartZoom * zoomFactor);
    processor.zoom.store (newZoom);

    float newViewFrac = 1.0f / newZoom;

    // Horizontal drag offset in sample-fraction space
    float hDragFrac = -(float) (e.x - dragStartX) / (float) w * newViewFrac;

    // Anchor: keep dragAnchorFrac at the original pixel fraction, then add horizontal offset
    float newViewStart = dragAnchorFrac - dragAnchorPixelFrac * newViewFrac + hDragFrac;

    // Convert viewStart to scroll: viewStart = scroll * (1 - viewFrac)
    float maxScroll = 1.0f - newViewFrac;
    if (maxScroll > 0.0f)
    {
        float newScroll = juce::jlimit (0.0f, 1.0f, newViewStart / maxScroll);
        processor.scroll.store (newScroll);
    }

    waveformView.repaint();
    repaint();
}

void ScrollZoomBar::mouseUp (const juce::MouseEvent&)
{
    isDragging = false;
    repaint();
}
