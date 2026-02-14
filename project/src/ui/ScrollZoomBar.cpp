#include "ScrollZoomBar.h"
#include "TuckersLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"

ScrollZoomBar::ScrollZoomBar (IntersectProcessor& p, WaveformView& wv)
    : processor (p), waveformView (wv)
{
    addAndMakeVisible (scrollLeft);
    addAndMakeVisible (scrollRight);
    addAndMakeVisible (zoomOut);
    addAndMakeVisible (zoomIn);

    scrollLeft.onClick = [this] {
        float sc = processor.scroll.load();
        processor.scroll.store (juce::jlimit (0.0f, 1.0f, sc - 0.05f));
        waveformView.repaint();
    };
    scrollRight.onClick = [this] {
        float sc = processor.scroll.load();
        processor.scroll.store (juce::jlimit (0.0f, 1.0f, sc + 0.05f));
        waveformView.repaint();
    };
    zoomOut.onClick = [this] {
        float z = processor.zoom.load();
        processor.zoom.store (std::max (1.0f, z / 1.5f));
        waveformView.repaint();
    };
    zoomIn.onClick = [this] {
        float z = processor.zoom.load();
        processor.zoom.store (std::min (256.0f, z * 1.5f));
        waveformView.repaint();
    };
}

juce::Rectangle<int> ScrollZoomBar::getTrackBounds() const
{
    int btnW = 22;
    int barX = btnW + 2;
    int barW = getWidth() - btnW * 4 - 12;
    return { barX, 0, barW, getHeight() };
}

juce::Rectangle<int> ScrollZoomBar::getThumbBounds() const
{
    auto track = getTrackBounds();
    float z = processor.zoom.load();
    float sc = processor.scroll.load();
    int thumbW = std::max (16, (int) (track.getWidth() / z));
    int thumbX = track.getX() + (int) (sc * (track.getWidth() - thumbW));
    return { thumbX, 2, thumbW, getHeight() - 4 };
}

void ScrollZoomBar::paint (juce::Graphics& g)
{
    auto track = getTrackBounds();

    // Scrollbar background
    g.setColour (Theme::darkBar);
    g.fillRect (track);

    // Thumb
    auto thumb = getThumbBounds();
    g.setColour (isDraggingThumb ? Theme::accent.withAlpha (0.7f)
                                 : Theme::accent.withAlpha (0.5f));
    g.fillRect (thumb);
}

void ScrollZoomBar::resized()
{
    int btnW = 22;
    int barW = getWidth() - btnW * 4 - 12;
    int h = getHeight();

    scrollLeft.setBounds (0, 0, btnW, h);
    scrollRight.setBounds (btnW + barW + 4, 0, btnW, h);
    zoomOut.setBounds (btnW + barW + btnW + 6, 0, btnW, h);
    zoomIn.setBounds (btnW + barW + btnW * 2 + 8, 0, btnW, h);
}

void ScrollZoomBar::mouseDown (const juce::MouseEvent& e)
{
    auto thumb = getThumbBounds();
    if (thumb.contains (e.getPosition()))
    {
        isDraggingThumb = true;
        dragStartScroll = processor.scroll.load();
        dragStartZoom = processor.zoom.load();
        dragStartX = e.x;
        dragStartY = e.y;
        repaint();
    }
}

void ScrollZoomBar::mouseDrag (const juce::MouseEvent& e)
{
    if (! isDraggingThumb)
        return;

    // Vertical drag: zoom (down = zoom in, up = zoom out)
    float deltaY = (float) (e.y - dragStartY);  // positive = down
    float zoomFactor = std::pow (1.01f, deltaY);
    float newZoom = juce::jlimit (1.0f, 256.0f, dragStartZoom * zoomFactor);
    processor.zoom.store (newZoom);

    // Horizontal drag: scroll
    auto track = getTrackBounds();
    int thumbW = std::max (16, (int) (track.getWidth() / newZoom));
    int trackRange = track.getWidth() - thumbW;

    if (trackRange > 0)
    {
        float deltaFrac = (float) (e.x - dragStartX) / (float) trackRange;
        float newScroll = juce::jlimit (0.0f, 1.0f, dragStartScroll + deltaFrac);
        processor.scroll.store (newScroll);
    }

    waveformView.repaint();
    repaint();
}

void ScrollZoomBar::mouseUp (const juce::MouseEvent&)
{
    isDraggingThumb = false;
    repaint();
}
