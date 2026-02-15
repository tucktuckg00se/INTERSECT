#include "WaveformView.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"

WaveformView::WaveformView (IntersectProcessor& p) : processor (p) {}

int WaveformView::pixelToSample (int px) const
{
    int numFrames = processor.sampleData.getNumFrames();
    float z = processor.zoom.load();
    float sc = processor.scroll.load();
    int visLen = (int) (numFrames / z);
    int visStart = (int) (sc * (numFrames - visLen));
    return visStart + (int) ((float) px / getWidth() * visLen);
}

int WaveformView::sampleToPixel (int sample) const
{
    int numFrames = processor.sampleData.getNumFrames();
    float z = processor.zoom.load();
    float sc = processor.scroll.load();
    int visLen = (int) (numFrames / z);
    int visStart = (int) (sc * (numFrames - visLen));
    if (visLen <= 0) return 0;
    return (int) ((float) (sample - visStart) / visLen * getWidth());
}

void WaveformView::rebuildCacheIfNeeded()
{
    float z = processor.zoom.load();
    float sc = processor.scroll.load();
    int w = getWidth();
    int numFrames = processor.sampleData.getNumFrames();
    if (z != prevZoom || sc != prevScroll || w != prevWidth || numFrames != prevNumFrames)
    {
        if (processor.sampleData.isLoaded())
            cache.rebuild (processor.sampleData.getBuffer(), numFrames, z, sc, w);
        prevZoom = z;
        prevScroll = sc;
        prevWidth = w;
        prevNumFrames = numFrames;
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().waveformBg);

    // Grid lines
    int cy = getHeight() / 2;
    g.setColour (getTheme().gridLine.withAlpha (0.5f));
    g.drawHorizontalLine (cy, 0.0f, (float) getWidth());
    g.setColour (getTheme().gridLine.withAlpha (0.2f));
    g.drawHorizontalLine (getHeight() / 4, 0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() * 3 / 4, 0.0f, (float) getWidth());

    if (processor.sampleData.isLoaded())
    {
        rebuildCacheIfNeeded();
        drawWaveform (g);
        drawSlices (g);
        drawPlaybackCursors (g);
    }
    else
    {
        g.setColour (getTheme().foreground.withAlpha (0.25f));
        g.setFont (juce::Font (18.0f));
        g.drawText ("DROP AUDIO FILE", getLocalBounds(), juce::Justification::centred);
    }
}

void WaveformView::drawWaveform (juce::Graphics& g)
{
    g.setColour (getTheme().waveformOrange.withAlpha (0.9f));
    int cy = getHeight() / 2;
    float scale = getHeight() * 0.48f;

    auto& peaks = cache.getPeaks();
    for (int px = 0; px < cache.getNumPeaks() && px < getWidth(); ++px)
    {
        float yMax = cy - peaks[(size_t) px].maxVal * scale;
        float yMin = cy - peaks[(size_t) px].minVal * scale;
        g.drawVerticalLine (px, yMax, yMin);
    }
}

void WaveformView::drawSlices (juce::Graphics& g)
{
    int sel = processor.sliceManager.selectedSlice;
    int num = processor.sliceManager.getNumSlices();

    for (int i = 0; i < num; ++i)
    {
        const auto& s = processor.sliceManager.getSlice (i);
        if (! s.active) continue;

        int x1 = std::max (0, sampleToPixel (s.startSample));
        int x2 = std::min (getWidth(), sampleToPixel (s.endSample));
        int sw = x2 - x1;
        if (sw <= 0) continue;

        if (i == sel)
        {
            // Selected: purple overlay
            g.setColour (getTheme().purpleOverlay.withAlpha (0.22f));
            g.fillRect (x1, 0, sw, getHeight());

            // White markers with triangle handles at bottom
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawVerticalLine (x1, 0.0f, (float) getHeight());
            g.drawVerticalLine (x2 - 1, 0.0f, (float) getHeight());

            // Triangle handles at bottom for start
            juce::Path triS;
            triS.addTriangle ((float) x1, (float) getHeight(),
                              (float) x1 + 7.0f, (float) getHeight(),
                              (float) x1, (float) getHeight() - 9.0f);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillPath (triS);

            // Triangle handles at bottom for end
            juce::Path triE;
            triE.addTriangle ((float) (x2 - 1), (float) getHeight(),
                              (float) (x2 - 1) - 7.0f, (float) getHeight(),
                              (float) (x2 - 1), (float) getHeight() - 9.0f);
            g.fillPath (triE);

            // "S" and "E" labels near handles
            g.setFont (juce::Font (8.0f).boldened());
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawText ("S", x1 + 2, getHeight() - 22, 10, 10, juce::Justification::centredLeft);
            g.drawText ("E", x2 - 12, getHeight() - 22, 10, 10, juce::Justification::centredRight);

            // Label
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::Font (10.0f).boldened());
            g.drawText ("Slice " + juce::String (i + 1), x1 + 3, 3, 60, 12,
                         juce::Justification::centredLeft);
        }
        else
        {
            // Non-selected: thin vertical edge lines only (no overlay fill)
            g.setColour (s.colour.withAlpha (0.30f));
            g.drawVerticalLine (x1, 0.0f, (float) getHeight());
            g.drawVerticalLine (x2 - 1, 0.0f, (float) getHeight());
        }
    }
}

void WaveformView::drawPlaybackCursors (juce::Graphics& g)
{
    for (int i = 0; i < VoicePool::kMaxVoices; ++i)
    {
        float pos = processor.voicePool.voicePositions[i].load (std::memory_order_relaxed);
        if (pos > 0.0f)
        {
            int px = sampleToPixel ((int) pos);
            if (px >= 0 && px < getWidth())
            {
                if (i == VoicePool::kMaxVoices - 1 && processor.lazyChop.isActive())
                    g.setColour (juce::Colour (0xFFCC4444));  // red for preview
                else
                    g.setColour (getTheme().accent.withAlpha (0.7f));  // yellow

                g.drawVerticalLine (px, 0.0f, (float) getHeight());
            }
        }
    }
}

void WaveformView::resized()
{
    prevWidth = -1;  // force cache rebuild
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (! processor.sampleData.isLoaded())
        return;

    // Middle-mouse drag: scroll+zoom (like ScrollZoomBar)
    if (e.mods.isMiddleButtonDown())
    {
        midDragging = true;
        midDragStartZoom = processor.zoom.load();
        midDragStartX = e.x;
        midDragStartY = e.y;

        int w = getWidth();
        float z = midDragStartZoom;
        float sc = processor.scroll.load();
        float viewFrac = 1.0f / z;
        float viewStart = sc * (1.0f - viewFrac);

        midDragAnchorPixelFrac = (w > 0) ? (float) e.x / (float) w : 0.5f;
        midDragAnchorFrac = juce::jlimit (0.0f, 1.0f, viewStart + midDragAnchorPixelFrac * viewFrac);
        return;
    }

    int samplePos = std::max (0, std::min (pixelToSample (e.x), processor.sampleData.getNumFrames()));

    if (sliceDrawMode)
    {
        drawStart = samplePos;
        dragMode = DrawSlice;
        return;
    }

    // Check slice edges (6px hot zone) — only for already-selected slice
    int sel = processor.sliceManager.selectedSlice;
    int num = processor.sliceManager.getNumSlices();

    if (sel >= 0 && sel < num)
    {
        const auto& s = processor.sliceManager.getSlice (sel);
        if (s.active)
        {
            int x1 = sampleToPixel (s.startSample);
            int x2 = sampleToPixel (s.endSample);

            if (std::abs (e.x - x1) < 6)
            {
                dragMode = DragEdgeLeft;
                dragSliceIdx = sel;
                return;
            }
            if (std::abs (e.x - x2) < 6)
            {
                dragMode = DragEdgeRight;
                dragSliceIdx = sel;
                return;
            }

            if (e.x > x1 && e.x < x2)
            {
                dragMode = MoveSlice;
                dragSliceIdx = sel;
                dragOffset = samplePos - s.startSample;
                dragSliceLen = s.endSample - s.startSample;
                return;
            }
        }
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (! processor.sampleData.isLoaded())
        return;

    // Middle-mouse drag: scroll+zoom
    if (midDragging)
    {
        int w = getWidth();
        if (w <= 0) return;

        float deltaY = (float) (e.y - midDragStartY);
        float zoomFactor = std::pow (1.01f, deltaY);
        float newZoom = juce::jlimit (1.0f, 2048.0f, midDragStartZoom * zoomFactor);
        processor.zoom.store (newZoom);

        float newViewFrac = 1.0f / newZoom;
        float hDragFrac = -(float) (e.x - midDragStartX) / (float) w * newViewFrac;
        float newViewStart = midDragAnchorFrac - midDragAnchorPixelFrac * newViewFrac + hDragFrac;

        float maxScroll = 1.0f - newViewFrac;
        if (maxScroll > 0.0f)
            processor.scroll.store (juce::jlimit (0.0f, 1.0f, newViewStart / maxScroll));

        prevWidth = -1;
        repaint();
        return;
    }

    int samplePos = std::max (0, std::min (pixelToSample (e.x), processor.sampleData.getNumFrames()));

    if (dragMode == DragEdgeLeft && dragSliceIdx >= 0)
    {
        auto& s = processor.sliceManager.getSlice (dragSliceIdx);
        s.startSample = std::min (samplePos, s.endSample - 64);
        repaint();
    }
    else if (dragMode == DragEdgeRight && dragSliceIdx >= 0)
    {
        auto& s = processor.sliceManager.getSlice (dragSliceIdx);
        s.endSample = std::max (samplePos, s.startSample + 64);
        repaint();
    }
    else if (dragMode == MoveSlice && dragSliceIdx >= 0)
    {
        int newStart = samplePos - dragOffset;
        int newEnd = newStart + dragSliceLen;

        // Clamp to sample bounds
        int maxLen = processor.sampleData.getNumFrames();
        if (newStart < 0) { newStart = 0; newEnd = dragSliceLen; }
        if (newEnd > maxLen) { newEnd = maxLen; newStart = maxLen - dragSliceLen; }

        auto& s = processor.sliceManager.getSlice (dragSliceIdx);
        s.startSample = newStart;
        s.endSample = newEnd;
        repaint();
    }
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (midDragging)
    {
        midDragging = false;
        return;
    }

    if (dragMode == DrawSlice)
    {
        int endPos = std::max (0, std::min (pixelToSample (e.x), processor.sampleData.getNumFrames()));
        if (std::abs (endPos - drawStart) > 64)
        {
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdCreateSlice;
            cmd.intParam1 = drawStart;
            cmd.intParam2 = endPos;
            processor.pushCommand (cmd);
            sliceDrawMode = false;
        }
        // If click without dragging (< 64 samples), keep draw mode active
    }
    else if (dragMode == DragEdgeLeft || dragMode == DragEdgeRight || dragMode == MoveSlice)
    {
        processor.sliceManager.rebuildMidiMap();
    }

    dragMode = None;
    dragSliceIdx = -1;
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isShiftDown())
    {
        // Scroll
        float sc = processor.scroll.load();
        sc -= w.deltaY * 0.05f;
        processor.scroll.store (juce::jlimit (0.0f, 1.0f, sc));
    }
    else
    {
        // Cursor-anchored zoom
        int width = getWidth();
        float oldZoom = processor.zoom.load();
        float oldViewFrac = 1.0f / oldZoom;
        float oldScroll = processor.scroll.load();
        float oldViewStart = oldScroll * (1.0f - oldViewFrac);

        // Sample fraction under cursor
        float cursorPixelFrac = (width > 0) ? (float) e.x / (float) width : 0.5f;
        float anchorFrac = oldViewStart + cursorPixelFrac * oldViewFrac;

        // Apply zoom change
        float newZoom = oldZoom;
        if (w.deltaY > 0)
            newZoom = std::min (2048.0f, oldZoom * 1.2f);
        else
            newZoom = std::max (1.0f, oldZoom / 1.2f);
        processor.zoom.store (newZoom);

        // Recompute scroll so anchorFrac stays at same pixel position
        float newViewFrac = 1.0f / newZoom;
        float newViewStart = anchorFrac - cursorPixelFrac * newViewFrac;
        float maxScroll = 1.0f - newViewFrac;
        if (maxScroll > 0.0f)
            processor.scroll.store (juce::jlimit (0.0f, 1.0f, newViewStart / maxScroll));
        else
            processor.scroll.store (0.0f);
    }
    prevWidth = -1;  // force cache rebuild
    repaint();
}

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".ogg" || ext == ".aiff" || ext == ".flac" || ext == ".mp3")
            return true;
    }
    return false;
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty())
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdLoadFile;
        cmd.fileParam = juce::File (files[0]);
        processor.pushCommand (cmd);
        processor.zoom.store (1.0f);
        processor.scroll.store (0.0f);
        prevWidth = -1;
    }
}
