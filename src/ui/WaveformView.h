#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "WaveformCache.h"

class IntersectProcessor;

class WaveformView : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
{
public:
    explicit WaveformView (IntersectProcessor& p);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void modifierKeysChanged (const juce::ModifierKeys& mods) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void rebuildCacheIfNeeded();
    bool hasActiveSlicePreview() const noexcept;
    bool getActiveSlicePreview (int& sliceIdx, int& startSample, int& endSample) const;
    bool isInteracting() const noexcept;

    void setSliceDrawMode (bool active);
    bool isSliceDrawModeActive() const noexcept { return sliceDrawMode; }
    void showOverlayHint (const juce::String& text, int durationMs, bool stickyUntilAction = false);
    void clearOverlayHint();

    bool altModeActive = false;
    bool shiftPreviewActive = false;
    std::vector<int> transientPreviewPositions;

private:
    struct ViewState
    {
        int numFrames = 0;
        int visibleStart = 0;
        int visibleLen = 0;
        int width = 0;
        float samplesPerPixel = 1.0f;
        bool valid = false;
    };

    enum DragMode
    {
        None,
        DragEdgeLeft,
        DragEdgeRight,
        DragLoopLeft,
        DragLoopRight,
        DrawSlice,
        MoveSlice,
        DuplicateSlice
    };

    enum class HoveredHandle { None, SliceLeft, SliceRight, LoopLeft, LoopRight };
    HoveredHandle hoveredHandle = HoveredHandle::None;

    int pixelToSample (int px) const;
    int sampleToPixel (int sample) const;
    ViewState buildViewState (const SampleData::SnapshotPtr& sampleSnap) const;
    void syncAltStateFromMods (const juce::ModifierKeys& mods);
    void timerCallback() override;

    void drawWaveform (juce::Graphics& g);
    void drawSlices (juce::Graphics& g);
    void drawFadeRegions (juce::Graphics& g);
    void drawPlaybackCursors (juce::Graphics& g);
    void paintDrawSlicePreview (juce::Graphics& g);
    void paintLazyChopOverlay (juce::Graphics& g);
    void paintTransientMarkers (juce::Graphics& g);
    void paintOverlayHint (juce::Graphics& g);

    // Aggregates all cache-invalidation inputs; rebuild is skipped when unchanged.
    struct CacheKey
    {
        int visibleStart = 0, visibleLen = 0, width = 0, numFrames = 0;
        const void* samplePtr = nullptr;
        bool operator== (const CacheKey&) const = default;
    };

    IntersectProcessor& processor;
    WaveformCache cache;
    CacheKey prevCacheKey;
    bool sliceDrawMode = false;
    mutable ViewState cachedPaintViewState;   // valid only between paint() start and end
    mutable bool paintViewStateActive = false; // true only during paint(); guards cachedPaintViewState

    DragMode dragMode = None;
    int dragSliceIdx = -1;
    int drawStart = 0;
    int drawEnd = 0;
    bool drawStartedFromAlt = false;
    int dragOffset = 0;    // for MoveSlice: offset from mouse to slice start
    int dragSliceLen = 0;  // for MoveSlice: original slice length
    int dragPreviewStart = 0; // for edge/move drags: preview start sample
    int dragPreviewEnd = 0;   // for edge/move drags: preview end sample
    int dragLoopPreviewStart = 0;
    int dragLoopPreviewEnd = 0;
    int ghostStart = 0;    // for DuplicateSlice: ghost overlay start sample
    int ghostEnd   = 0;    // for DuplicateSlice: ghost overlay end sample

    // Middle-mouse drag (scroll+zoom like ScrollZoomBar)
    bool midDragging = false;
    float midDragStartZoom = 1.0f;
    float midDragAnchorFrac = 0.0f;
    float midDragAnchorPixelFrac = 0.0f;
    int   midDragStartX = 0;
    int   midDragStartY = 0;

    juce::String overlayHintText;
    bool overlayHintSticky = false;
};
