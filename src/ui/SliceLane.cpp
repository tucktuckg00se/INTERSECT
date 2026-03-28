#include "SliceLane.h"
#include "IntersectLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"
#include <algorithm>
#include <array>

SliceLane::SliceLane (IntersectProcessor& p) : processor (p) {}

void SliceLane::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().surface0);

    auto sampleSnap = processor.sampleData.getSnapshot();
    int numFrames = sampleSnap ? sampleSnap->buffer.getNumSamples() : 0;
    if (numFrames <= 0)
        return;

    const auto& ui = processor.getUiSliceSnapshot();
    const float z = std::max (1.0f, processor.zoom.load());
    const float sc = processor.scroll.load();
    const int visLen = juce::jlimit (1, numFrames, (int) (numFrames / z));
    const int maxStart = juce::jmax (0, numFrames - visLen);
    const int visStart = juce::jlimit (0, maxStart, (int) (sc * (float) maxStart));

    int sel = ui.selectedSlice;
    int num = ui.numSlices;
    int w = getWidth();
    int h = getHeight();
    int previewIdx = -1;
    int previewStart = 0;
    int previewEnd = 0;
    const bool hasPreview = waveformView != nullptr
        && waveformView->getActiveSlicePreview (previewIdx, previewStart, previewEnd);

    // Collect visible slice info without per-frame heap allocations.
    struct SliceInfo { int idx; int x1; int x2; bool selected; juce::Colour col; };
    std::array<SliceInfo, SliceManager::kMaxSlices> visibleSlices {};
    int visibleCount = 0;

    for (int i = 0; i < num; ++i)
    {
        const auto& s = ui.slices[(size_t) i];
        if (! s.active) continue;
        int startSample = s.startSample;
        int endSample = s.endSample;
        if (hasPreview && i == previewIdx)
        {
            startSample = previewStart;
            endSample = previewEnd;
        }

        int x1 = (int) ((float) (startSample - visStart) / visLen * w);
        int x2 = (int) ((float) (endSample - visStart) / visLen * w);
        x1 = std::max (0, x1);
        x2 = std::min (w, x2);
        if (x2 - x1 < 2) continue;

        if (visibleCount < SliceManager::kMaxSlices)
            visibleSlices[(size_t) visibleCount++] = { i, x1, x2, (i == sel), s.colour };
    }

    // Pass 1: Draw non-selected first, selected last (z-order) without sorting.
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool drawSelected = (pass == 1);
        for (int i = 0; i < visibleCount; ++i)
        {
            const auto& si = visibleSlices[(size_t) i];
            if (si.selected != drawSelected)
                continue;

            int sw = si.x2 - si.x1;
            g.setColour ((si.selected ? si.col.darker (0.78f) : si.col.darker (0.82f)).withAlpha (0.92f));
            g.fillRect (si.x1, 0, sw, h);

            if (si.selected)
            {
                g.setColour (si.col.withAlpha (0.95f));
                g.fillRect (si.x1, 0, sw, 2);
                g.fillRect (si.x1, h - 2, sw, 2);
            }
            else
            {
                g.setColour (si.col.withAlpha (0.45f));
                g.fillRect (si.x1, 0, sw, 1);
            }
        }
    }

    // Build left-to-right label order by x position using insertion sort on indices.
    std::array<int, SliceManager::kMaxSlices> labelOrder {};
    int labelOrderCount = 0;
    for (int i = 0; i < visibleCount; ++i)
    {
        int pos = labelOrderCount;
        while (pos > 0 && visibleSlices[(size_t) labelOrder[(size_t) (pos - 1)]].x1 > visibleSlices[(size_t) i].x1)
        {
            labelOrder[(size_t) pos] = labelOrder[(size_t) (pos - 1)];
            --pos;
        }
        labelOrder[(size_t) pos] = i;
        ++labelOrderCount;
    }

    std::array<int, SliceManager::kMaxSlices> labelEnds {};
    int labelEndCount = 0;
    for (int oi = 0; oi < labelOrderCount; ++oi)
    {
        const auto& si = visibleSlices[(size_t) labelOrder[(size_t) oi]];
        int sw = si.x2 - si.x1;
        if (sw > 14)
        {
            juce::String label = juce::String (si.idx + 1);
            g.setFont (IntersectLookAndFeel::makeFont (12.0f, true));
            int labelW = g.getCurrentFont().getStringWidth (label) + 6;
            int labelX = si.x1 + 3;
            for (int li = 0; li < labelEndCount; ++li)
            {
                int end = labelEnds[(size_t) li];
                if (labelX < end)
                    labelX = end + 1;
            }
            if (labelX + labelW <= si.x2 && labelX + labelW < w)
            {
                g.setColour (si.selected ? getTheme().text2.withAlpha (0.9f) : si.col.withAlpha (0.7f));
                g.drawText (label, labelX, 0, labelW, h, juce::Justification::centredLeft);
                if (labelEndCount < SliceManager::kMaxSlices)
                    labelEnds[(size_t) labelEndCount++] = labelX + labelW;
            }
        }
    }

    g.setColour (getTheme().surface3.withAlpha (0.8f));
    g.drawHorizontalLine (h - 1, 0.0f, (float) w);
}

void SliceLane::mouseDown (const juce::MouseEvent& e)
{
    auto sampleSnap = processor.sampleData.getSnapshot();
    int numFrames = sampleSnap ? sampleSnap->buffer.getNumSamples() : 0;
    if (numFrames <= 0)
        return;

    const auto& ui = processor.getUiSliceSnapshot();
    const float z = std::max (1.0f, processor.zoom.load());
    const float sc = processor.scroll.load();
    const int visLen = juce::jlimit (1, numFrames, (int) (numFrames / z));
    const int maxStart = juce::jmax (0, numFrames - visLen);
    const int visStart = juce::jlimit (0, maxStart, (int) (sc * (float) maxStart));
    int w = getWidth();
    int num = ui.numSlices;

    // Collect all overlapping slice indices at click position
    std::vector<int> overlapping;

    for (int i = 0; i < num; ++i)
    {
        const auto& s = ui.slices[(size_t) i];
        if (! s.active) continue;

        int x1 = (int) ((float) (s.startSample - visStart) / visLen * w);
        int x2 = (int) ((float) (s.endSample - visStart) / visLen * w);

        if (e.x >= x1 && e.x < x2)
            overlapping.push_back (i);
    }

    if (! overlapping.empty())
    {
        int current = ui.selectedSlice;
        int target = current;

        // If current selection is in the list, cycle to the next one
        auto it = std::find (overlapping.begin(), overlapping.end(), current);
        if (it != overlapping.end())
        {
            ++it;
            if (it == overlapping.end())
                it = overlapping.begin();
            target = *it;
        }
        else
        {
            target = overlapping.front();
        }

        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdSelectSlice;
        cmd.intParam1 = target;
        processor.pushCommand (cmd);
    }
}
