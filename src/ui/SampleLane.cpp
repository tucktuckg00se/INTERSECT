#include "SampleLane.h"
#include "StemExportPanel.h"
#include "IntersectLookAndFeel.h"
#include "WaveformView.h"
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

bool isStemJobRunningForSample (const IntersectProcessor::UiSliceSnapshot& ui, int sampleId)
{
    return ui.stemJobSourceSampleId == sampleId
           && ui.stemJobState != StemJobState::idle
           && ui.stemJobState != StemJobState::completed
           && ui.stemJobState != StemJobState::failed
           && ui.stemJobState != StemJobState::cancelled;
}
}

SampleLane::SampleLane (IntersectProcessor& p, WaveformView& wv) : processor (p), waveformView (wv) {}

SampleLane::~SampleLane()
{
    dismissStemExportPanel();
}

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
        const int blockW = x2 - x1;
        const int buttonH = juce::jmax (12, getHeight() - 6);
        const int buttonY = (getHeight() - buttonH) / 2;
        const int deleteW = juce::jmin (16, juce::jmax (12, blockW / 6));
        const int stemsW = juce::jmin (44, juce::jmax (18, blockW / 3));
        int right = x2 - 3;
        visible.deleteBounds = { right - deleteW, buttonY, deleteW, buttonH };
        right = visible.deleteBounds.getX() - 2;
        visible.stemsBounds = { right - stemsW, buttonY, stemsW, buttonH };
        visible.deleteBounds = visible.deleteBounds.getIntersection ({ x1 + 1, buttonY, juce::jmax (1, blockW - 2), buttonH });
        visible.stemsBounds = visible.stemsBounds.getIntersection ({ x1 + 1, buttonY, juce::jmax (1, blockW - 2), buttonH });
        out.push_back (std::move (visible));
    }

    return out;
}

int SampleLane::hitTestSample (juce::Point<int> pos) const
{
    const auto visible = buildVisibleSamples();
    for (const auto& sample : visible)
        if (sample.x1 <= pos.x && pos.x < sample.x2 && pos.y >= 0 && pos.y < getHeight())
            return sample.sampleId;
    return -1;
}

void SampleLane::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().surface0);

    const auto visible = buildVisibleSamples();
    const auto& ui = processor.getUiSliceSnapshot();
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

        const int maxLabelRight = juce::jmin (sample.stemsBounds.getX() - 2, getWidth());
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

    auto drawButton = [&] (const juce::Rectangle<int>& bounds,
                           const juce::String& text,
                           juce::Colour base,
                           juce::Colour textColour)
    {
        g.setColour (base);
        g.fillRoundedRectangle (bounds.toFloat(), 2.0f);
        g.setColour (textColour);
        g.setFont (IntersectLookAndFeel::makeFont (8.0f, true));
        g.drawFittedText (text, bounds, juce::Justification::centred, 1, 0.9f);
    };

    for (const auto& sample : visible)
    {
        const bool jobRunning = isStemJobRunningForSample (ui, sample.sampleId);
        const bool compactButton = sample.stemsBounds.getWidth() < 34;
        drawButton (sample.stemsBounds,
                    compactButton ? (jobRunning ? "C" : "S") : (jobRunning ? "CANCEL" : "STEMS"),
                    (jobRunning ? getTheme().accent : getTheme().surface4).withAlpha (0.92f),
                    getTheme().text2.withAlpha (0.92f));
        drawButton (sample.deleteBounds,
                    "X",
                    juce::Colours::black.withAlpha (0.18f),
                    getTheme().text2.withAlpha (0.86f));
    }

    // Draw stem separation progress bar on the source sample
    {
        const auto stemState = ui.stemJobState;
        if (stemState == StemJobState::preparing
            || stemState == StemJobState::separating
            || stemState == StemJobState::writing)
        {
            for (const auto& sample : visible)
            {
                if (sample.sampleId == ui.stemJobSourceSampleId)
                {
                    const int blockW = sample.x2 - sample.x1;
                    const int barH = 3;
                    const int fillW = juce::jmax (1, juce::roundToInt (ui.stemJobProgress * (float) blockW));
                    g.setColour (getTheme().accent.withAlpha (0.4f));
                    g.fillRect (sample.x1, h - barH, blockW, barH);
                    g.setColour (getTheme().accent.withAlpha (0.9f));
                    g.fillRect (sample.x1, h - barH, fillW, barH);
                    break;
                }
            }
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

    const auto visible = buildVisibleSamples();
    const auto pos = e.getPosition();
    const int hitId = hitTestSample (pos);
    const auto it = std::find_if (visible.begin(), visible.end(),
                                  [hitId] (const VisibleSample& sample) { return sample.sampleId == hitId; });

    if (it != visible.end() && it->deleteBounds.contains (pos))
    {
        processor.selectedSessionSampleId.store (hitId, std::memory_order_relaxed);
        processor.markUiSnapshotDirty();
        processor.deleteSessionSampleAsync (hitId);
        repaint();
        return;
    }

    if (it != visible.end() && it->stemsBounds.contains (pos))
    {
        processor.selectedSessionSampleId.store (hitId, std::memory_order_relaxed);
        processor.markUiSnapshotDirty();
        repaint();

        const auto& ui = processor.getUiSliceSnapshot();
        const bool jobRunning = isStemJobRunningForSample (ui, hitId);

        if (jobRunning)
        {
            processor.cancelStemSeparation();
        }
        else
        {
            if (stemExportPanel != nullptr)
                dismissStemExportPanel();
            else
                showStemExportPanel (hitId);
        }
        return;
    }

    dragSampleId = hitId;
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

void SampleLane::showStemExportPanel (int sampleId)
{
    dismissStemExportPanel();
    stemExportPanel = std::make_unique<StemExportPanel> (processor, sampleId);

    if (auto* editor = waveformView.getParentComponent())
    {
        auto wfBounds = waveformView.getBoundsInParent();
        int panelH = 72;
        int panelX = wfBounds.getX();
        int panelW = wfBounds.getWidth();
        int panelY = wfBounds.getY();
        stemExportPanel->setBounds (panelX, panelY, panelW, panelH);
        editor->addAndMakeVisible (*stemExportPanel);
    }
}

void SampleLane::dismissStemExportPanel()
{
    if (stemExportPanel == nullptr)
        return;

    if (auto* parent = stemExportPanel->getParentComponent())
        parent->removeChildComponent (stemExportPanel.get());

    stemExportPanel.reset();
}
