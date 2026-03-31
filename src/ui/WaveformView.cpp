#include "WaveformView.h"
#include "UIHelpers.h"
#include "IntersectLookAndFeel.h"
#include "../Constants.h"
#include "../PluginProcessor.h"
#include "../audio/AudioAnalysis.h"

WaveformView::WaveformView (IntersectProcessor& p) : processor (p) {}

void WaveformView::setSliceDrawMode (bool active)
{
    sliceDrawMode = active;
    setMouseCursor (active ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
    if (! active && overlayHintSticky)
        clearOverlayHint();
}

void WaveformView::showOverlayHint (const juce::String& text, int durationMs, bool stickyUntilAction)
{
    overlayHintText = text.trim();
    overlayHintSticky = stickyUntilAction;

    stopTimer();
    if (overlayHintText.isNotEmpty() && ! overlayHintSticky)
        startTimer (juce::jmax (100, durationMs));

    repaint();
}

void WaveformView::clearOverlayHint()
{
    overlayHintText.clear();
    overlayHintSticky = false;
    stopTimer();
    repaint();
}

void WaveformView::timerCallback()
{
    clearOverlayHint();
}

bool WaveformView::hasActiveSlicePreview() const noexcept
{
    if (dragSliceIdx >= 0)
        return dragMode == DragEdgeLeft || dragMode == DragEdgeRight || dragMode == MoveSlice;

    IntersectProcessor::MidiBoundaryPreviewState preview;
    return processor.getMidiBoundaryPreviewState (preview);
}

bool WaveformView::getActiveSlicePreview (int& sliceIdx, int& startSample, int& endSample) const
{
    if (dragSliceIdx >= 0
        && (dragMode == DragEdgeLeft || dragMode == DragEdgeRight || dragMode == MoveSlice))
    {
        sliceIdx = dragSliceIdx;
        startSample = dragPreviewStart;
        endSample = dragPreviewEnd;
        return true;
    }

    IntersectProcessor::MidiBoundaryPreviewState preview;
    if (! processor.getMidiBoundaryPreviewState (preview))
        return false;

    sliceIdx = preview.sliceIdx;
    startSample = preview.startSample;
    endSample = preview.endSample;
    return true;
}

bool WaveformView::isInteracting() const noexcept
{
    IntersectProcessor::MidiBoundaryPreviewState preview;
    return dragMode != None || midDragging || shiftPreviewActive
        || processor.getMidiBoundaryPreviewState (preview);
}

WaveformView::ViewState WaveformView::buildViewState (const SampleData::SnapshotPtr& sampleSnap) const
{
    ViewState state;
    if (sampleSnap == nullptr)
        return state;

    const int numFrames = sampleSnap->buffer.getNumSamples();
    const int width = getWidth();
    if (numFrames <= 0 || width <= 0)
        return state;

    const float z = std::max (1.0f, processor.zoom.load());
    const float sc = processor.scroll.load();
    const int visibleLen = juce::jlimit (1, numFrames, (int) (numFrames / z));
    const int maxStart = juce::jmax (0, numFrames - visibleLen);
    const int visibleStart = juce::jlimit (0, maxStart, (int) (sc * (float) maxStart));

    state.numFrames = numFrames;
    state.visibleStart = visibleStart;
    state.visibleLen = visibleLen;
    state.width = width;
    state.samplesPerPixel = (float) visibleLen / (float) width;
    state.valid = true;
    return state;
}

int WaveformView::pixelToSample (int px) const
{
    if (paintViewStateActive && cachedPaintViewState.valid)
    {
        return cachedPaintViewState.visibleStart
            + (int) ((float) px / (float) cachedPaintViewState.width * cachedPaintViewState.visibleLen);
    }

    const auto state = buildViewState (processor.sampleData.getSnapshot());
    if (! state.valid)
        return 0;
    return state.visibleStart + (int) ((float) px / (float) state.width * state.visibleLen);
}

int WaveformView::sampleToPixel (int sample) const
{
    if (paintViewStateActive && cachedPaintViewState.valid)
    {
        return (int) ((float) (sample - cachedPaintViewState.visibleStart)
                      / (float) cachedPaintViewState.visibleLen
                      * (float) cachedPaintViewState.width);
    }

    const auto state = buildViewState (processor.sampleData.getSnapshot());
    if (! state.valid)
        return 0;
    return (int) ((float) (sample - state.visibleStart) / (float) state.visibleLen * (float) state.width);
}

void WaveformView::rebuildCacheIfNeeded()
{
    auto sampleSnap = processor.sampleData.getSnapshot();
    const auto view = buildViewState (sampleSnap);
    if (! view.valid)
        return;

    const CacheKey key { view.visibleStart, view.visibleLen, view.width, view.numFrames, sampleSnap.get() };
    if (key == prevCacheKey)
        return;

    cache.rebuild (sampleSnap->buffer, sampleSnap->peakMipmaps,
                   view.numFrames, processor.zoom.load(), processor.scroll.load(), view.width);
    prevCacheKey = key;
}

void WaveformView::paint (juce::Graphics& g)
{
    auto sampleSnap = processor.sampleData.getSnapshot();
    g.fillAll (getTheme().surface0);

    int cy = getHeight() / 2;
    g.setColour (getTheme().surface4.withAlpha (0.5f));
    g.drawHorizontalLine (cy, 0.0f, (float) getWidth());
    g.setColour (getTheme().surface4.withAlpha (0.2f));
    g.drawHorizontalLine (getHeight() / 4, 0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() * 3 / 4, 0.0f, (float) getWidth());

    if (sampleSnap != nullptr)
    {
        cachedPaintViewState = buildViewState (sampleSnap);
        paintViewStateActive = cachedPaintViewState.valid;
        rebuildCacheIfNeeded();
        drawWaveform (g);
        drawSlices (g);
        drawFadeRegions (g);
        paintDrawSlicePreview (g);
        paintLazyChopOverlay (g);
        paintTransientMarkers (g);
        drawPlaybackCursors (g);
        paintViewStateActive = false;
    }
    else
    {
        paintViewStateActive = false;
        g.setColour (getTheme().text2.withAlpha (0.25f));
        g.setFont (IntersectLookAndFeel::makeFont (22.0f));
        g.drawText ("DROP AUDIO FILE", getLocalBounds(), juce::Justification::centred);
    }

    paintOverlayHint (g);
}

void WaveformView::paintDrawSlicePreview (juce::Graphics& g)
{
    // Draw active slice region while dragging in +SLC mode
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

    // Draw ghost overlay for Ctrl-drag duplicate
    if (dragMode == DuplicateSlice)
    {
        int gx1 = sampleToPixel (ghostStart);
        int gx2 = sampleToPixel (ghostEnd);
        if (gx2 > gx1)
        {
            g.setColour (getTheme().accent.withAlpha (0.15f));
            g.fillRect (gx1, 0, gx2 - gx1, getHeight());
            juce::Path p;
            p.addRectangle ((float) gx1, 0.5f, (float)(gx2 - gx1), (float) getHeight() - 1.0f);
            float dl[] = { 4.0f, 4.0f };
            juce::PathStrokeType pst (1.0f);
            juce::Path dashed;
            pst.createDashedStroke (dashed, p, dl, 2);
            g.setColour (getTheme().accent.withAlpha (0.75f));
            g.strokePath (dashed, pst);
        }
    }
}

void WaveformView::paintLazyChopOverlay (juce::Graphics& g)
{
    if (! (processor.lazyChop.isActive() && processor.lazyChop.isPlaying()
           && processor.lazyChop.getChopPos() >= 0))
        return;

    int previewIdx = LazyChopEngine::getPreviewVoiceIndex();
    float playhead = processor.voicePool.voicePositions[previewIdx].load (std::memory_order_relaxed);
    if (playhead <= 0.0f)
        return;

    int chopSample = processor.lazyChop.getChopPos();
    int headSample = (int) playhead;
    int x1 = sampleToPixel (std::min (chopSample, headSample));
    int x2 = sampleToPixel (std::max (chopSample, headSample));
    if (x2 > x1)
    {
        g.setColour (getTheme().color5.withAlpha (0.15f));
        g.fillRect (x1, 0, x2 - x1, getHeight());
        g.setColour (getTheme().color5.withAlpha (0.5f));
        g.drawVerticalLine (sampleToPixel (chopSample), 0.0f, (float) getHeight());
    }
}

void WaveformView::paintTransientMarkers (juce::Graphics& g)
{
    if (transientPreviewPositions.empty())
        return;

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

void WaveformView::paintOverlayHint (juce::Graphics& g)
{
    if (overlayHintText.isEmpty())
        return;

    g.setFont (IntersectLookAndFeel::makeFont (12.0f));
    juce::GlyphArrangement ga;
    ga.addLineOfText (g.getCurrentFont(), overlayHintText, 0.0f, 0.0f);
    const int textW = juce::roundToInt (ga.getBoundingBox (0, -1, true).getWidth()) + 24;
    const int maxW = juce::jmax (120, getWidth() - 16);
    const int bannerW = juce::jlimit (120, maxW, textW);
    const int bannerH = 24;
    const int bannerX = juce::jmax (4, (getWidth() - bannerW) / 2);
    const int bannerY = juce::jmax (4, getHeight() - bannerH - 6);
    auto banner = juce::Rectangle<float> ((float) bannerX, (float) bannerY, (float) bannerW, (float) bannerH);

    g.setColour (getTheme().surface2.withAlpha (0.94f));
    g.fillRoundedRectangle (banner, 4.0f);
    g.setColour (getTheme().surface5.withAlpha (0.85f));
    g.drawRoundedRectangle (banner.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (getTheme().text2.withAlpha (0.9f));
    g.drawFittedText (overlayHintText, banner.toNearestInt().reduced (8, 2), juce::Justification::centred, 1);
}

void WaveformView::drawWaveform (juce::Graphics& g)
{
    int cy = getHeight() / 2;
    float scale = getHeight() * UILayout::waveformVerticalScale;

    auto& peaks = cache.getPeaks();
    int numPeaks = std::min (cache.getNumPeaks(), getWidth());

    if (numPeaks <= 0)
        return;

    float samplesPerPixel = 1.0f;
    if (paintViewStateActive && cachedPaintViewState.valid)
    {
        samplesPerPixel = cachedPaintViewState.samplesPerPixel;
    }
    else
    {
        const auto view = buildViewState (processor.sampleData.getSnapshot());
        if (view.valid)
            samplesPerPixel = view.samplesPerPixel;
    }

    if (samplesPerPixel < 1.0f)
    {
        // Sub-sample zoom: draw connected lines between sample points
        g.setColour (getTheme().waveform.withAlpha (0.9f));
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
        // Normal zoom: filled path waveform
        g.setColour (getTheme().waveform);

        juce::Path fillPath;

        // Top envelope (max peaks, left to right)
        fillPath.startNewSubPath (0.0f, (float) cy - peaks[0].maxVal * scale);
        for (int px = 1; px < numPeaks; ++px)
        {
            fillPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].maxVal * scale);
        }

        // Bottom envelope (min peaks, right to left)
        for (int px = numPeaks - 1; px >= 0; --px)
        {
            fillPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].minVal * scale);
        }

        fillPath.closeSubPath();
        g.fillPath (fillPath);

        // When zoomed in close (few samples per pixel), the fill becomes
        // sub-pixel thin and disappears. Draw a centre-line to keep the
        // waveform visible through the transition zone.
        if (samplesPerPixel < 8.0f)
        {
            juce::Path midPath;
            float midY0 = (float) cy - (peaks[0].maxVal + peaks[0].minVal) * 0.5f * scale;
            midPath.startNewSubPath (0.0f, midY0);
            for (int px = 1; px < numPeaks; ++px)
            {
                float mid = (peaks[(size_t) px].maxVal + peaks[(size_t) px].minVal) * 0.5f;
                midPath.lineTo ((float) px, (float) cy - mid * scale);
            }
            g.strokePath (midPath, juce::PathStrokeType (1.5f));
        }
    }
}

void WaveformView::drawSlices (juce::Graphics& g)
{
    const auto& ui = processor.getUiSliceSnapshot();
    int sel = ui.selectedSlice;
    int num = ui.numSlices;
    int previewIdx = -1;
    int previewStart = 0;
    int previewEnd = 0;
    const bool previewActive = getActiveSlicePreview (previewIdx, previewStart, previewEnd);

    for (int i = 0; i < num; ++i)
    {
        const auto& s = ui.slices[(size_t) i];
        if (! s.active) continue;

        int drawStartSample = s.startSample;
        int drawEndSample = s.endSample;
        if (previewActive && previewIdx == i)
        {
            drawStartSample = previewStart;
            drawEndSample = previewEnd;
        }

        int x1 = std::max (0, sampleToPixel (drawStartSample));
        int x2 = std::min (getWidth(), sampleToPixel (drawEndSample));
        int sw = x2 - x1;
        if (sw <= 0) continue;

        if (i == sel || (previewActive && previewIdx == i))
        {
            g.setColour (s.colour.withAlpha (0.05f));
            g.fillRect (x1, 0, sw, getHeight());

            g.setColour (s.colour.withAlpha (0.42f));
            g.drawVerticalLine (x1, 0.0f, (float) getHeight());
            g.drawVerticalLine (x2 - 1, 0.0f, (float) getHeight());

            auto handleHeight = hoveredEdge == HoveredEdge::Left || hoveredEdge == HoveredEdge::Right ? 30.0f : 24.0f;
            auto handleY = (float) getHeight() - handleHeight;
            g.fillRoundedRectangle ((float) x1 - 2.0f, handleY, 5.0f, handleHeight, 2.0f);
            g.fillRoundedRectangle ((float) x2 - 3.0f, handleY, 5.0f, handleHeight, 2.0f);

            g.setColour (s.colour.withAlpha (0.92f));
            g.setFont (IntersectLookAndFeel::makeFont (9.0f, true));
            g.drawText ("Slice " + juce::String (i + 1), x1, 7, sw, 12, juce::Justification::centredTop);
        }
        else
        {
            g.setColour (s.colour.withAlpha (0.12f));
            g.drawVerticalLine (x1, 0.0f, (float) getHeight());
            g.drawVerticalLine (x2 - 1, 0.0f, (float) getHeight());
        }
    }
}

void WaveformView::drawFadeRegions (juce::Graphics& g)
{
    const auto& ui = processor.getUiSliceSnapshot();
    const int sel = ui.selectedSlice;
    if (sel < 0 || sel >= ui.numSlices) return;

    const auto& s = ui.slices[(size_t) sel];
    if (! s.active) return;

    // Also only show if loop mode is active
    const auto globals = GlobalParamSnapshot::loadFrom (processor.apvts, ui.rootNote);

    const float resolvedCrossfade = (s.lockMask & kLockCrossfade) ? s.crossfadePct : globals.crossfadePct;
    if (resolvedCrossfade <= 0.0f) return;

    const int loopMode = (s.lockMask & kLockLoop) ? s.loopMode : globals.loopMode;
    if (loopMode == 0) return;

    const int sliceLen = s.endSample - s.startSample;
    if (sliceLen <= 0) return;

    const bool reverse = (s.lockMask & kLockReverse) ? s.reverse : globals.reverse;
    const bool pingPong = (loopMode == 2);
    const int bufferEnd = processor.sampleData.getNumFrames();

    int fadeLen = crossfadePercentToSamples (resolvedCrossfade, sliceLen, pingPong);
    if (! pingPong)
    {
        const int preStartAvail = s.startSample;
        const int postEndAvail  = juce::jmax (0, bufferEnd - s.endSample);
        fadeLen = reverse ? juce::jmin (fadeLen, postEndAvail)
                          : juce::jmin (fadeLen, preStartAvail);
    }
    if (fadeLen <= 0) return;

    const float h = (float) getHeight();
    const auto fadeColour = s.colour.withAlpha (0.18f);
    const auto fillFadeTriangle = [&] (int sampleA, int sampleB, int boundarySample)
    {
        const int x1 = sampleToPixel (sampleA);
        const int x2 = sampleToPixel (sampleB);
        const int xb = sampleToPixel (boundarySample);
        if (x2 <= x1 || x2 <= 0 || x1 >= getWidth())
            return;

        juce::Path triangle;
        triangle.startNewSubPath ((float) x1, h);
        triangle.lineTo ((float) x2, h);
        triangle.lineTo ((float) xb, 0.0f);
        triangle.closeSubPath();

        g.setColour (fadeColour);
        g.fillPath (triangle);
    };

    if (pingPong)
    {
        fillFadeTriangle (s.startSample, s.startSample + fadeLen, s.startSample);
        fillFadeTriangle (s.endSample - fadeLen, s.endSample, s.endSample);
        return;
    }

    if (reverse)
    {
        fillFadeTriangle (s.startSample, s.startSample + fadeLen, s.startSample);
        fillFadeTriangle (s.endSample, s.endSample + fadeLen, s.endSample);
        return;
    }

    fillFadeTriangle (s.startSample - fadeLen, s.startSample, s.startSample);
    fillFadeTriangle (s.endSample - fadeLen, s.endSample, s.endSample);
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
                    g.setColour (getTheme().color5);
                else
                    g.setColour (getTheme().accent.withAlpha (0.7f));  // yellow

                g.drawVerticalLine (px, 0.0f, (float) getHeight());
            }
        }
    }

    // Draw crossfade source cursor for selected slice voices
    const auto& ui = processor.getUiSliceSnapshot();
    const int sel = ui.selectedSlice;
    if (sel >= 0 && sel < ui.numSlices)
    {
        const auto& s = ui.slices[(size_t) sel];
        if (s.active)
        {
            for (int i = 0; i < VoicePool::kMaxVoices; ++i)
            {
                if (processor.voicePool.getVoice (i).sliceIdx != sel) continue;
                float xfPos = processor.voicePool.xfadeSourcePositions[i].load (std::memory_order_relaxed);
                if (xfPos > 0.0f)
                {
                    int px = sampleToPixel ((int) xfPos);
                    if (px >= 0 && px < getWidth())
                    {
                        g.setColour (s.colour);
                        g.drawVerticalLine (px, 0.0f, (float) getHeight());
                    }
                }
            }
        }
    }
}

void WaveformView::resized()
{
    prevCacheKey = {};  // force cache rebuild
}

void WaveformView::syncAltStateFromMods (const juce::ModifierKeys& mods)
{
    const bool alt = mods.isAltDown();
    if (alt == altModeActive)
        return;

    altModeActive = alt;
    hoveredEdge = HoveredEdge::None;

    if (alt)
        setMouseCursor (juce::MouseCursor::IBeamCursor);
    else if (dragMode != DrawSlice)
        setMouseCursor (juce::MouseCursor::NormalCursor);

    repaint();
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    syncAltStateFromMods (e.mods);

    auto sampleSnap = processor.sampleData.getSnapshot();
    if (sampleSnap == nullptr) return;
    const auto& ui = processor.getUiSliceSnapshot();
    int sel = ui.selectedSlice;
    int num = ui.numSlices;
    HoveredEdge newEdge = HoveredEdge::None;

    if (sel >= 0 && sel < num && ! sliceDrawMode && ! altModeActive)
    {
        const auto& s = ui.slices[(size_t) sel];
        if (s.active)
        {
            int x1 = sampleToPixel (s.startSample);
            int x2 = sampleToPixel (s.endSample);
            if      (std::abs (e.x - x1) < 6) newEdge = HoveredEdge::Left;
            else if (std::abs (e.x - x2) < 6) newEdge = HoveredEdge::Right;
        }
    }
    if (altModeActive)
        setMouseCursor (juce::MouseCursor::IBeamCursor);
    else if (sliceDrawMode)
        setMouseCursor (juce::MouseCursor::IBeamCursor);
    else
        setMouseCursor (newEdge != HoveredEdge::None
            ? juce::MouseCursor::LeftRightResizeCursor
            : juce::MouseCursor::NormalCursor);

    if (newEdge != hoveredEdge) { hoveredEdge = newEdge; repaint(); }
}

void WaveformView::mouseEnter (const juce::MouseEvent& e) { mouseMove (e); }

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    if (hoveredEdge != HoveredEdge::None) { hoveredEdge = HoveredEdge::None; repaint(); }
}

void WaveformView::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    syncAltStateFromMods (mods);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    syncAltStateFromMods (e.mods);

    auto sampleSnap = processor.sampleData.getSnapshot();
    if (sampleSnap == nullptr)
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

    int samplePos = std::max (0, std::min (pixelToSample (e.x), sampleSnap->buffer.getNumSamples()));

    // Shift+click: preview audio from pointer position
    if (e.mods.isShiftDown() && ! sliceDrawMode && ! altModeActive
        && ! processor.lazyChop.isActive())
    {
        shiftPreviewActive = true;
        processor.shiftPreviewRequest.store (samplePos, std::memory_order_relaxed);
        return;
    }

    if (sliceDrawMode || altModeActive)
    {
        if (overlayHintSticky)
            clearOverlayHint();

        drawStart = samplePos;
        drawEnd = samplePos;
        drawStartedFromAlt = (! sliceDrawMode && e.mods.isAltDown());
        dragMode = DrawSlice;
        return;
    }

    // Check slice edges (6px hot zone) — only for already-selected slice
    const auto& ui = processor.getUiSliceSnapshot();
    int sel = ui.selectedSlice;
    int num = ui.numSlices;

    if (sel >= 0 && sel < num)
    {
        const auto& s = ui.slices[(size_t) sel];
        if (s.active)
        {
            auto tryBeginMouseBoundsDrag = [&] (DragMode newMode) -> bool
            {
                int expectedOwner = 0;
                if (! processor.liveDragOwner.compare_exchange_strong (expectedOwner, 1,
                                                                       std::memory_order_acq_rel,
                                                                       std::memory_order_acquire)
                    && expectedOwner != 1)
                {
                    return false;
                }

                processor.liveDragBoundsStart.store (s.startSample, std::memory_order_relaxed);
                processor.liveDragBoundsEnd.store   (s.endSample,   std::memory_order_relaxed);
                processor.liveDragSliceIdx.store    (sel,           std::memory_order_release);

                IntersectProcessor::Command gestureCmd;
                gestureCmd.type = IntersectProcessor::CmdBeginGesture;
                processor.pushCommand (gestureCmd);
                dragMode = newMode;
                dragSliceIdx = sel;
                dragPreviewStart = s.startSample;
                dragPreviewEnd = s.endSample;
                return true;
            };

            int x1 = sampleToPixel (s.startSample);
            int x2 = sampleToPixel (s.endSample);

            if (std::abs (e.x - x1) < 6)
            {
                if (tryBeginMouseBoundsDrag (DragEdgeLeft))
                    return;
            }
            if (std::abs (e.x - x2) < 6)
            {
                if (tryBeginMouseBoundsDrag (DragEdgeRight))
                    return;
            }

            if (e.x > x1 && e.x < x2)
            {
                dragSliceIdx = sel;
                dragOffset   = samplePos - s.startSample;
                dragSliceLen = s.endSample - s.startSample;

                if (e.mods.isCtrlDown())
                {
                    IntersectProcessor::Command gestureCmd;
                    gestureCmd.type = IntersectProcessor::CmdBeginGesture;
                    processor.pushCommand (gestureCmd);
                    dragMode   = DuplicateSlice;
                    ghostStart = s.startSample;
                    ghostEnd   = s.endSample;
                }
                else
                {
                    int expectedOwner = 0;
                    if (! processor.liveDragOwner.compare_exchange_strong (expectedOwner, 1,
                                                                           std::memory_order_acq_rel,
                                                                           std::memory_order_acquire)
                        && expectedOwner != 1)
                    {
                        return;
                    }

                    IntersectProcessor::Command gestureCmd;
                    gestureCmd.type = IntersectProcessor::CmdBeginGesture;
                    processor.pushCommand (gestureCmd);

                    dragMode = MoveSlice;
                    dragPreviewStart = s.startSample;
                    dragPreviewEnd = s.endSample;
                    processor.liveDragBoundsStart.store (dragPreviewStart, std::memory_order_relaxed);
                    processor.liveDragBoundsEnd.store   (dragPreviewEnd,   std::memory_order_relaxed);
                    processor.liveDragSliceIdx.store    (dragSliceIdx,     std::memory_order_release);
                }
                return;
            }
        }
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    syncAltStateFromMods (e.mods);

    auto sampleSnap = processor.sampleData.getSnapshot();
    if (sampleSnap == nullptr)
        return;

    // Middle-mouse drag: scroll+zoom
    if (midDragging)
    {
        int w = getWidth();
        if (w <= 0) return;

        float deltaY = (float) (e.y - midDragStartY);
        float newZoom = juce::jlimit (1.0f, 16384.0f, midDragStartZoom * UIHelpers::computeZoomFactor (deltaY));
        processor.zoom.store (newZoom);

        float newViewFrac = 1.0f / newZoom;
        float hDragFrac = -(float) (e.x - midDragStartX) / (float) w * newViewFrac;
        float newViewStart = midDragAnchorFrac - midDragAnchorPixelFrac * newViewFrac + hDragFrac;

        float maxScroll = 1.0f - newViewFrac;
        if (maxScroll > 0.0f)
            processor.scroll.store (juce::jlimit (0.0f, 1.0f, newViewStart / maxScroll));

        prevCacheKey = {};
        return;
    }

    int samplePos = std::max (0, std::min (pixelToSample (e.x), sampleSnap->buffer.getNumSamples()));

    if (dragMode == DrawSlice)
    {
        drawEnd = samplePos;
        return;
    }

    if (dragMode == DragEdgeLeft && dragSliceIdx >= 0)
    {
        if (processor.snapToZeroCrossing.load())
            samplePos = AudioAnalysis::findNearestZeroCrossing (sampleSnap->buffer, samplePos);
        dragPreviewStart = std::min (samplePos, dragPreviewEnd - kMinSliceLengthSamples);
    }
    else if (dragMode == DragEdgeRight && dragSliceIdx >= 0)
    {
        if (processor.snapToZeroCrossing.load())
            samplePos = AudioAnalysis::findNearestZeroCrossing (sampleSnap->buffer, samplePos);
        dragPreviewEnd = std::max (samplePos, dragPreviewStart + kMinSliceLengthSamples);
    }
    else if (dragMode == MoveSlice && dragSliceIdx >= 0)
    {
        int newStart = samplePos - dragOffset;
        int newEnd = newStart + dragSliceLen;

        // Clamp to sample bounds
        int maxLen = sampleSnap->buffer.getNumSamples();
        if (newStart < 0) { newStart = 0; newEnd = dragSliceLen; }
        if (newEnd > maxLen) { newEnd = maxLen; newStart = maxLen - dragSliceLen; }

        dragPreviewStart = newStart;
        dragPreviewEnd = newEnd;
    }

    // Push live bounds to the audio engine so note-ons during drag use the
    // current edge position. Written before the idx store (release) so the
    // audio thread sees consistent values after its acquire load on idx.
    if ((dragMode == DragEdgeLeft || dragMode == DragEdgeRight || dragMode == MoveSlice)
        && dragSliceIdx >= 0)
    {
        if (processor.liveDragOwner.load (std::memory_order_acquire) == 2)
            return;

        processor.liveDragBoundsStart.store (dragPreviewStart, std::memory_order_relaxed);
        processor.liveDragBoundsEnd.store   (dragPreviewEnd,   std::memory_order_relaxed);
        processor.liveDragOwner.store       (1,                std::memory_order_release);
        processor.liveDragSliceIdx.store    (dragSliceIdx,     std::memory_order_release);
    }

    if (dragMode == DuplicateSlice && dragSliceIdx >= 0)
    {
        int maxLen   = sampleSnap->buffer.getNumSamples();
        int newStart = juce::jlimit (0, maxLen - dragSliceLen, samplePos - dragOffset);
        ghostStart   = newStart;
        ghostEnd     = newStart + dragSliceLen;
    }
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    syncAltStateFromMods (e.mods);

    auto sampleSnap = processor.sampleData.getSnapshot();

    // Stop shift preview
    if (shiftPreviewActive)
    {
        shiftPreviewActive = false;
        processor.shiftPreviewRequest.store (-1, std::memory_order_relaxed);
        return;
    }

    if (midDragging)
    {
        midDragging = false;
        return;
    }

    if (dragMode == DrawSlice)
    {
        const bool altStillDown = e.mods.isAltDown();
        const int maxFrames = sampleSnap ? sampleSnap->buffer.getNumSamples() : 0;
        int endPos = std::max (0, std::min (pixelToSample (e.x), maxFrames));
        if (sampleSnap != nullptr && processor.snapToZeroCrossing.load())
        {
            drawStart = AudioAnalysis::findNearestZeroCrossing (sampleSnap->buffer, drawStart);
            endPos = AudioAnalysis::findNearestZeroCrossing (sampleSnap->buffer, endPos);
        }
        if (std::abs (endPos - drawStart) >= kMinSliceLengthSamples)
        {
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdCreateSlice;
            cmd.intParam1 = drawStart;
            cmd.intParam2 = endPos;
            processor.pushCommand (cmd);
            if (! altModeActive)
                setSliceDrawMode (false);
        }

        if (drawStartedFromAlt && ! altStillDown)
            setSliceDrawMode (false);

        // If click without dragging (< min slice length), keep draw mode active
    }
    else if (dragMode == DragEdgeLeft || dragMode == DragEdgeRight || dragMode == MoveSlice)
    {
        if (dragSliceIdx >= 0)
        {
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdSetSliceBounds;
            cmd.intParam1 = dragSliceIdx;
            cmd.intParam2 = dragPreviewStart;
            cmd.positions[0] = dragPreviewEnd;
            cmd.numPositions = 1;
            processor.pushCommand (cmd);
        }
    }
    else if (dragMode == DuplicateSlice)
    {
        if (sampleSnap != nullptr && processor.snapToZeroCrossing.load())
        {
            ghostStart = AudioAnalysis::findNearestZeroCrossing (sampleSnap->buffer, ghostStart);
            ghostEnd   = ghostStart + dragSliceLen;
        }
        IntersectProcessor::Command cmd;
        cmd.type      = IntersectProcessor::CmdDuplicateSlice;
        cmd.intParam1 = ghostStart;
        cmd.intParam2 = ghostEnd;
        cmd.sliceIdx  = dragSliceIdx;
        processor.pushCommand (cmd);
    }

    // Deactivate live drag so the audio thread stops overriding slice bounds.
    // Must happen before dragSliceIdx is cleared so there's no window where
    // a stale idx could re-activate on the next block.
    processor.liveDragSliceIdx.store (-1, std::memory_order_release);
    processor.liveDragOwner.store    (0,  std::memory_order_release);

    dragMode = None;
    dragSliceIdx = -1;
    dragPreviewStart = 0;
    dragPreviewEnd = 0;
    drawStartedFromAlt = false;
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (w.deltaX != 0.0f)
    {
        float sc = processor.scroll.load();
        sc -= w.deltaX * 0.05f;
        processor.scroll.store (juce::jlimit (0.0f, 1.0f, sc));
        prevCacheKey = {};
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

        // Sample fraction under cursor
        float cursorPixelFrac = (width > 0) ? (float) e.x / (float) width : 0.5f;

        // Apply zoom change
        float newZoom = (w.deltaY > 0)
            ? std::min (16384.0f, oldZoom * 1.2f)
            : std::max (1.0f, oldZoom / 1.2f);
        processor.zoom.store (newZoom);

        // Recompute scroll so anchorFrac stays at same pixel position
        float newViewFrac = 1.0f / newZoom;
        float maxScroll = 1.0f - newViewFrac;
        if (maxScroll > 0.0f)
        {
            float oldViewStart = oldScroll * (1.0f - oldViewFrac);
            float anchorFrac = oldViewStart + cursorPixelFrac * oldViewFrac;
            float newViewStart = anchorFrac - cursorPixelFrac * newViewFrac;
            processor.scroll.store (juce::jlimit (0.0f, 1.0f, newViewStart / maxScroll));
        }
        else
            processor.scroll.store (0.0f);
    }
    prevCacheKey = {};  // force cache rebuild
}

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".ogg" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".mp3")
            return true;
    }
    return false;
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty())
    {
        processor.loadFileAsync (juce::File (files[0]));
        processor.zoom.store (1.0f);
        processor.scroll.store (0.0f);
        prevCacheKey = {};
    }
}
