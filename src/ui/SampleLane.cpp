#include "SampleLane.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
float measureTextWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getWidth();
}
}

SampleLane::SampleLane (IntersectProcessor& p) : processor (p) {}

std::vector<SampleLane::VisibleSample> SampleLane::buildVisibleSamples() const
{
    std::vector<VisibleSample> out;
    const auto sampleSnap = processor.sampleData.getSnapshot();
    if (sampleSnap == nullptr)
        return out;

    const int numFrames = sampleSnap->buffer.getNumSamples();
    if (numFrames <= 0 || getWidth() <= 0)
        return out;

    const auto& ui = processor.getUiSliceSnapshot();
    const float z = std::max (1.0f, processor.zoom.load());
    const float sc = processor.scroll.load();
    const int visLen = juce::jlimit (1, numFrames, (int) ((float) numFrames / z));
    const int maxStart = juce::jmax (0, numFrames - visLen);
    const int visStart = juce::jlimit (0, maxStart, (int) (sc * (float) maxStart));
    const int w = getWidth();
    const int selectedId = ui.selectedSessionSampleId;

    out.reserve ((size_t) ui.numSessionSamples);
    for (int i = 0; i < ui.numSessionSamples; ++i)
    {
        const auto& sample = ui.sessionSamples[(size_t) i];
        const int start = sample.startSample;
        const int end = sample.startSample + sample.numFrames;
        int x1 = (int) ((float) (start - visStart) / (float) visLen * (float) w);
        int x2 = (int) ((float) (end - visStart) / (float) visLen * (float) w);
        x1 = juce::jlimit (0, w, x1);
        x2 = juce::jlimit (0, w, x2);
        if (x2 - x1 < 2)
            continue;

        VisibleSample visible;
        visible.sampleId = sample.sampleId;
        visible.index = i;
        visible.x1 = x1;
        visible.x2 = x2;
        visible.selected = sample.sampleId == selectedId;
        visible.colour = getTheme().slicePalette[(15 - (i % 16) + 16) % 16];
        visible.label = sample.fileName.toString();
        out.push_back (std::move (visible));
    }

    return out;
}

int SampleLane::hitTestSample (int x) const
{
    const auto visible = buildVisibleSamples();
    for (const auto& sample : visible)
        if (x >= sample.x1 && x < sample.x2)
            return sample.sampleId;
    return -1;
}

void SampleLane::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().surface0);

    const auto visible = buildVisibleSamples();
    const int h = getHeight();

    g.setColour (getTheme().surface3.withAlpha (0.78f));
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    for (int pass = 0; pass < 2; ++pass)
    {
        const bool drawSelected = (pass == 1);
        for (const auto& sample : visible)
        {
            if (sample.selected != drawSelected)
                continue;

            const int blockW = sample.x2 - sample.x1;
            g.setColour ((sample.selected ? sample.colour.darker (0.78f)
                                          : sample.colour.darker (0.82f)).withAlpha (0.92f));
            g.fillRect (sample.x1, 0, blockW, h);
        }
    }

    std::array<int, 64> labelOrder {};
    int labelOrderCount = 0;
    for (int i = 0; i < (int) visible.size() && i < (int) labelOrder.size(); ++i)
    {
        int pos = labelOrderCount;
        while (pos > 0 && visible[(size_t) labelOrder[(size_t) (pos - 1)]].x1 > visible[(size_t) i].x1)
        {
            labelOrder[(size_t) pos] = labelOrder[(size_t) (pos - 1)];
            --pos;
        }
        labelOrder[(size_t) pos] = i;
        ++labelOrderCount;
    }

    std::array<int, 64> labelEnds {};
    int labelEndCount = 0;
    g.setFont (IntersectLookAndFeel::makeFont (8.5f, true));

    for (int oi = 0; oi < labelOrderCount; ++oi)
    {
        const auto& sample = visible[(size_t) labelOrder[(size_t) oi]];
        const int blockW = sample.x2 - sample.x1;
        if (blockW <= 14)
            continue;

        juce::String label = blockW < 42 ? juce::String (sample.index + 1) : sample.label;
        const int labelW = juce::roundToInt (std::ceil (measureTextWidth (g.getCurrentFont(), label))) + 6;
        int labelX = sample.x1 + 3;
        for (int li = 0; li < labelEndCount; ++li)
        {
            const int end = labelEnds[(size_t) li];
            if (labelX < end)
                labelX = end + 1;
        }

        const int maxLabelRight = juce::jmin (sample.x2, getWidth());
        const int availableLabelW = juce::jmax (1, maxLabelRight - labelX - 2);
        if (availableLabelW >= 8)
        {
            g.setColour (sample.selected ? getTheme().text2.withAlpha (0.9f)
                                         : sample.colour.withAlpha (0.7f));
            g.drawFittedText (label, labelX, 0, availableLabelW, h, juce::Justification::centredLeft, 1, 1.0f);
            if (labelEndCount < (int) labelEnds.size())
                labelEnds[(size_t) labelEndCount++] = labelX + juce::jmin (labelW, availableLabelW);
        }
    }

    if (dragging && dragTargetIndex >= 0)
    {
        int insertX = getWidth() - 1;
        if (dragTargetIndex < (int) visible.size())
            insertX = visible[(size_t) dragTargetIndex].x1;
        g.setColour (getTheme().accent.withAlpha (0.95f));
        g.fillRect (insertX - 1, 1, 3, juce::jmax (1, h - 2));
    }

    g.setColour (getTheme().surface3.withAlpha (0.8f));
    g.drawHorizontalLine (h - 1, 0.0f, (float) getWidth());
}

void SampleLane::mouseDown (const juce::MouseEvent& e)
{
    if (onInteraction != nullptr)
        onInteraction();

    dragSampleId = hitTestSample (e.x);
    dragStartX = e.x;
    dragTargetIndex = -1;
    dragging = false;

    if (dragSampleId >= 0)
    {
        processor.selectedSessionSampleId.store (dragSampleId, std::memory_order_relaxed);
        processor.markUiSnapshotDirty();
        repaint();
    }
}

void SampleLane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSampleId < 0)
        return;

    if (! dragging && std::abs (e.x - dragStartX) > 4)
        dragging = true;
    if (! dragging)
        return;

    const auto visible = buildVisibleSamples();
    dragTargetIndex = (int) visible.size();
    for (const auto& sample : visible)
    {
        if (e.x < (sample.x1 + sample.x2) / 2)
        {
            dragTargetIndex = sample.index;
            break;
        }
    }
    repaint();
}

void SampleLane::mouseUp (const juce::MouseEvent&)
{
    if (dragging && dragSampleId >= 0 && dragTargetIndex >= 0)
        processor.reorderSessionSampleAsync (dragSampleId, dragTargetIndex);

    dragSampleId = -1;
    dragTargetIndex = -1;
    dragging = false;
    repaint();
}
