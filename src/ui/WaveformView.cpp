#include "WaveformView.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../audio/AudioAnalysis.h"

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

        // Draw slice preview while dragging in +SLC mode
        if (dragMode == DrawSlice)
        {
            int x1 = sampleToPixel (std::min (drawStart, drawEnd));
            int x2 = sampleToPixel (std::max (drawStart, drawEnd));
            if (x2 > x1)
            {
                g.setColour (getTheme().accent.withAlpha (0.2f));
                g.fillRect (x1, 0, x2 - x1, getHeight());
                g.setColour (getTheme().accent.withAlpha (0.6f));
                g.drawVerticalLine (x1, 0.0f, (float) getHeight());
                g.drawVerticalLine (x2, 0.0f, (float) getHeight());
            }
        }

        // Draw lazy chop preview: highlight between chopPos and playhead
        if (processor.lazyChop.isActive() && processor.lazyChop.isPlaying()
            && processor.lazyChop.getChopPos() >= 0)
        {
            int previewIdx = LazyChopEngine::getPreviewVoiceIndex();
            float playhead = processor.voicePool.voicePositions[previewIdx].load (std::memory_order_relaxed);
            if (playhead > 0.0f)
            {
                int chopSample = processor.lazyChop.getChopPos();
                int headSample = (int) playhead;
                int x1 = sampleToPixel (std::min (chopSample, headSample));
                int x2 = sampleToPixel (std::max (chopSample, headSample));
                if (x2 > x1)
                {
                    g.setColour (juce::Colour (0xFFCC4444).withAlpha (0.15f));
                    g.fillRect (x1, 0, x2 - x1, getHeight());
                    g.setColour (juce::Colour (0xFFCC4444).withAlpha (0.5f));
                    g.drawVerticalLine (sampleToPixel (chopSample), 0.0f, (float) getHeight());
                }
            }
        }

        // Draw transient preview lines
        if (! transientPreviewPositions.empty())
        {
            g.setColour (getTheme().accent.withAlpha (0.6f));
            float dashLengths[] = { 4.0f, 3.0f };
            for (int pos : transientPreviewPositions)
            {
                int px = sampleToPixel (pos);
                if (px >= 0 && px < getWidth())
                {
                    juce::Path dashPath;
                    dashPath.startNewSubPath ((float) px, 0.0f);
                    dashPath.lineTo ((float) px, (float) getHeight());
                    juce::PathStrokeType stroke (1.0f);
                    juce::Path dashedPath;
                    stroke.createDashedStroke (dashedPath, dashPath, dashLengths, 2);
                    g.fillPath (dashedPath);
                }
            }
        }

        drawPlaybackCursors (g);
    }
    else
    {
        g.setColour (getTheme().foreground.withAlpha (0.25f));
        g.setFont (IntersectLookAndFeel::makeFont (22.0f));
        g.drawText ("DROP AUDIO FILE", getLocalBounds(), juce::Justification::centred);
    }
}

void WaveformView::drawWaveform (juce::Graphics& g)
{
    g.setColour (getTheme().waveform.withAlpha (0.9f));
    int cy = getHeight() / 2;
    float scale = getHeight() * 0.48f;

    auto& peaks = cache.getPeaks();
    int numPeaks = std::min (cache.getNumPeaks(), getWidth());

    // Determine if we're in sub-sample zoom (peaks are single points, not ranges)
    int numFrames = processor.sampleData.getNumFrames();
    float z = processor.zoom.load();
    int visLen = (int) (numFrames / z);
    float samplesPerPixel = (visLen > 0 && getWidth() > 0) ? (float) visLen / (float) getWidth() : 1.0f;

    if (samplesPerPixel < 1.0f)
    {
        // Sub-sample zoom: draw connected lines between sample points
        juce::Path path;
        bool pathStarted = false;

        for (int px = 0; px < numPeaks; ++px)
        {
            float y = (float) cy - peaks[(size_t) px].maxVal * scale;
            if (! pathStarted)
            {
                path.startNewSubPath ((float) px, y);
                pathStarted = true;
            }
            else
            {
                path.lineTo ((float) px, y);
            }
        }

        g.strokePath (path, juce::PathStrokeType (1.5f));

        // Draw sample dots when zoomed in very far (> 8 pixels per sample)
        if (samplesPerPixel < 0.125f)
        {
            float dotR = 2.5f;
            for (int px = 0; px < numPeaks; ++px)
            {
                float exactPos = (float) pixelToSample (0) + px * samplesPerPixel;
                float fractional = exactPos - std::floor (exactPos);
                if (fractional < samplesPerPixel)
                {
                    float y = (float) cy - peaks[(size_t) px].maxVal * scale;
                    g.fillEllipse ((float) px - dotR, y - dotR, dotR * 2.0f, dotR * 2.0f);
                }
            }
        }
    }
    else
    {
        // Normal zoom: draw vertical lines for peak ranges
        for (int px = 0; px < numPeaks; ++px)
        {
            float yMax = cy - peaks[(size_t) px].maxVal * scale;
            float yMin = cy - peaks[(size_t) px].minVal * scale;
            g.drawVerticalLine (px, yMax, yMin);
        }
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
            g.setColour (getTheme().selectionOverlay.withAlpha (0.22f));
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
            g.setFont (IntersectLookAndFeel::makeFont (10.0f, true));
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawText ("S", x1 + 2, getHeight() - 24, 12, 12, juce::Justification::centredLeft);
            g.drawText ("E", x2 - 14, getHeight() - 24, 12, 12, juce::Justification::centredRight);

            // Label
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (IntersectLookAndFeel::makeFont (13.0f, true));
            g.drawText ("Slice " + juce::String (i + 1), x1 + 3, 3, 70, 14,
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
    int previewIdx = LazyChopEngine::getPreviewVoiceIndex();
    for (int i = 0; i < VoicePool::kMaxVoices; ++i)
    {
        float pos = processor.voicePool.voicePositions[i].load (std::memory_order_relaxed);
        if (pos > 0.0f)
        {
            int px = sampleToPixel ((int) pos);
            if (px >= 0 && px < getWidth())
            {
                if (i == previewIdx && processor.lazyChop.isActive())
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
        drawEnd = samplePos;
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
                IntersectProcessor::Command gestureCmd;
                gestureCmd.type = IntersectProcessor::CmdBeginGesture;
                processor.pushCommand (gestureCmd);
                dragMode = DragEdgeLeft;
                dragSliceIdx = sel;
                return;
            }
            if (std::abs (e.x - x2) < 6)
            {
                IntersectProcessor::Command gestureCmd;
                gestureCmd.type = IntersectProcessor::CmdBeginGesture;
                processor.pushCommand (gestureCmd);
                dragMode = DragEdgeRight;
                dragSliceIdx = sel;
                return;
            }

            if (e.x > x1 && e.x < x2)
            {
                IntersectProcessor::Command gestureCmd;
                gestureCmd.type = IntersectProcessor::CmdBeginGesture;
                processor.pushCommand (gestureCmd);
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

    if (dragMode == DrawSlice)
    {
        drawEnd = samplePos;
        repaint();
        return;
    }

    if (dragMode == DragEdgeLeft && dragSliceIdx >= 0)
    {
        if (processor.snapToZeroCrossing.load() && processor.sampleData.isLoaded())
            samplePos = AudioAnalysis::findNearestZeroCrossing (processor.sampleData.getBuffer(), samplePos);
        auto& s = processor.sliceManager.getSlice (dragSliceIdx);
        s.startSample = std::min (samplePos, s.endSample - 64);
        repaint();
    }
    else if (dragMode == DragEdgeRight && dragSliceIdx >= 0)
    {
        if (processor.snapToZeroCrossing.load() && processor.sampleData.isLoaded())
            samplePos = AudioAnalysis::findNearestZeroCrossing (processor.sampleData.getBuffer(), samplePos);
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
        if (processor.snapToZeroCrossing.load() && processor.sampleData.isLoaded())
        {
            drawStart = AudioAnalysis::findNearestZeroCrossing (processor.sampleData.getBuffer(), drawStart);
            endPos = AudioAnalysis::findNearestZeroCrossing (processor.sampleData.getBuffer(), endPos);
        }
        if (std::abs (endPos - drawStart) > 64)
        {
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdCreateSlice;
            cmd.intParam1 = drawStart;
            cmd.intParam2 = endPos;
            processor.pushCommand (cmd);
            sliceDrawMode = false;
            setMouseCursor (juce::MouseCursor::NormalCursor);
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
    if (w.deltaX != 0.0f)
    {
        float sc = processor.scroll.load();
        sc -= w.deltaX * 0.05f;
        processor.scroll.store (juce::jlimit (0.0f, 1.0f, sc));
        prevWidth = -1;
        repaint();
        return;
    }

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
