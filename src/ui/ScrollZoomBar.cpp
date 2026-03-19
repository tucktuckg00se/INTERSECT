#include "ScrollZoomBar.h"
#include "UIHelpers.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"
#include <algorithm>
#include <cmath>

ScrollZoomBar::ScrollZoomBar (IntersectProcessor& p)
    : processor (p)
{
}

void ScrollZoomBar::paint (juce::Graphics& g)
{
    int w = getWidth();
    int h = getHeight();

    g.fillAll (getTheme().waveformBg.withAlpha (0.98f));

    g.setColour (getTheme().moduleBorder.withAlpha (0.42f));
    g.drawHorizontalLine (0, 0.0f, (float) w);

    auto sampleSnap = processor.sampleData.getSnapshot();
    int numFrames = sampleSnap ? sampleSnap->buffer.getNumSamples() : 0;
    if (numFrames <= 0 || w <= 0)
        return;

    float z = processor.zoom.load();
    float sc = processor.scroll.load();
    float viewFrac = 1.0f / z;
    float viewStart = sc * (1.0f - viewFrac);
    float viewEnd = viewStart + viewFrac;

    double sampleRate = processor.getSampleRate();
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;

    const float totalSeconds = (float) numFrames / (float) sampleRate;
    const float viewStartSeconds = viewStart * totalSeconds;
    const float viewEndSeconds = viewEnd * totalSeconds;
    const float viewSeconds = juce::jmax (0.001f, viewEndSeconds - viewStartSeconds);

    float rawStep = viewSeconds * 90.0f / (float) w;
    const float niceSteps[] = { 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f,
                                20.0f, 30.0f, 60.0f, 120.0f };
    auto stepIt = std::find_if (std::begin (niceSteps), std::end (niceSteps),
                                [rawStep] (float ns) { return ns >= rawStep; });
    float majorStep = (stepIt != std::end (niceSteps)) ? *stepIt : 120.0f;

    const float startSeconds = viewStartSeconds;
    const float endSeconds = viewEndSeconds;
    float minorStep = majorStep / 4.0f;
    float firstMinor = std::floor (startSeconds / minorStep) * minorStep;

    auto formatTime = [] (float seconds)
    {
        const int wholeSeconds = juce::jmax (0, (int) std::floor (seconds + 0.0001f));
        const int minutes = wholeSeconds / 60;
        const int secs = wholeSeconds % 60;
        return juce::String (minutes) + ":" + juce::String (secs).paddedLeft ('0', 2);
    };

    for (float sec = firstMinor; sec <= endSeconds; sec += minorStep)
    {
        float px = (sec - startSeconds) / viewSeconds * (float) w;
        int tx = (int) px;
        if (tx < 0 || tx >= w) continue;

        bool isMajor = (std::fmod (sec + 0.0001f, majorStep) < minorStep * 0.5f);

        if (isMajor)
        {
            g.setColour (getTheme().paramLabel.withAlpha (0.32f));
            g.drawVerticalLine (tx, 1.0f, (float) h * 0.35f);

            g.setFont (IntersectLookAndFeel::makeFont (7.0f));
            g.setColour (juce::Colour (0xFF404858).withAlpha (0.78f));
            juce::String label = formatTime (sec);
            int labelW = 42;
            int labelX = tx - labelW / 2;
            labelX = std::max (0, std::min (w - labelW, labelX));
            g.drawText (label, labelX, (int) (h * 0.18f), labelW, h - (int) (h * 0.18f),
                        juce::Justification::centred);
        }
        else
        {
            g.setColour (getTheme().paramLabel.withAlpha (0.15f));
            g.drawVerticalLine (tx, 1.0f, (float) h * 0.18f);
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
    float newZoom = juce::jlimit (1.0f, 16384.0f, dragStartZoom * UIHelpers::computeZoomFactor (deltaY));
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
}

void ScrollZoomBar::mouseUp (const juce::MouseEvent&)
{
    isDragging = false;
}
