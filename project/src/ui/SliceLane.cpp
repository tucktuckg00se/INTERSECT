#include "SliceLane.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"

SliceLane::SliceLane (IntersectProcessor& p) : processor (p)
{
    addAndMakeVisible (midiSelectBtn);
    midiSelectBtn.setTooltip ("MIDI selects slice");
    midiSelectBtn.onClick = [this] {
        bool current = processor.midiSelectsSlice.load();
        bool newState = ! current;
        processor.midiSelectsSlice.store (newState);
        updateMidiButtonAppearance (newState);
        repaint();
    };
    updateMidiButtonAppearance (false);
}

void SliceLane::resized()
{
    int btnW = 20;
    midiSelectBtn.setBounds (getWidth() - btnW - 2, 2, btnW, getHeight() - 4);
}

void SliceLane::paint (juce::Graphics& g)
{
    g.fillAll (Theme::darkBar);

    int numFrames = processor.sampleData.getNumFrames();
    if (numFrames <= 0)
        return;

    float z = processor.zoom.load();
    float sc = processor.scroll.load();
    int visLen = (int) (numFrames / z);
    int visStart = (int) (sc * (numFrames - visLen));

    int sel = processor.sliceManager.selectedSlice;
    int num = processor.sliceManager.getNumSlices();
    int w = getWidth();
    int h = getHeight();

    // Collect visible slice info for two-pass rendering (selected slice drawn last)
    struct SliceInfo { int idx; int x1; int x2; bool selected; juce::Colour col; };
    std::vector<SliceInfo> visibleSlices;

    for (int i = 0; i < num; ++i)
    {
        const auto& s = processor.sliceManager.getSlice (i);
        if (! s.active) continue;
        if (visLen <= 0) continue;

        int x1 = (int) ((float) (s.startSample - visStart) / visLen * w);
        int x2 = (int) ((float) (s.endSample - visStart) / visLen * w);
        x1 = std::max (0, x1);
        x2 = std::min (w, x2);
        if (x2 - x1 < 2) continue;

        visibleSlices.push_back ({ i, x1, x2, (i == sel), s.colour });
    }

    // Sort: non-selected first, selected last (so selected draws on top)
    std::stable_sort (visibleSlices.begin(), visibleSlices.end(),
                      [] (const SliceInfo& a, const SliceInfo& b) { return !a.selected && b.selected; });

    // Pass 1: Draw bars and borders in selection order (z-order)
    for (const auto& si : visibleSlices)
    {
        int sw = si.x2 - si.x1;

        // Bar fill
        g.setColour (si.selected ? si.col.withAlpha (0.55f) : si.col.withAlpha (0.25f));
        g.fillRect (si.x1, 1, sw, h - 2);

        // Border for selected
        if (si.selected)
        {
            g.setColour (si.col.withAlpha (0.9f));
            g.drawRect (si.x1, 1, sw, h - 2, 1);
        }
    }

    // Re-sort by x-position for correct label overlap avoidance
    std::sort (visibleSlices.begin(), visibleSlices.end(),
               [] (const SliceInfo& a, const SliceInfo& b) { return a.x1 < b.x1; });

    // Pass 2: Draw labels in left-to-right order
    std::vector<int> labelEnds;  // right edge of each drawn label

    for (const auto& si : visibleSlices)
    {
        int sw = si.x2 - si.x1;

        // Slice number label — left-aligned, with overlap avoidance
        if (sw > 14)
        {
            juce::String label = juce::String (si.idx + 1);
            g.setFont (juce::Font (9.0f).boldened());
            int labelW = (int) g.getCurrentFont().getStringWidthFloat (label) + 6;
            int labelX = si.x1 + 3;

            // Push right until it clears all previously drawn labels
            for (int end : labelEnds)
            {
                if (labelX < end)
                    labelX = end + 1;
            }

            // Only draw if the label fits within the visible area
            if (labelX + labelW < w)
            {
                g.setColour (si.selected ? juce::Colours::white.withAlpha (0.9f) : si.col.withAlpha (0.7f));
                g.drawText (label, labelX, 0, labelW, h, juce::Justification::centredLeft);
                labelEnds.push_back (labelX + labelW);
            }
        }
    }

    // Sync M button appearance with current state
    updateMidiButtonAppearance (processor.midiSelectsSlice.load());

    // Bottom separator line
    g.setColour (Theme::separator);
    g.drawHorizontalLine (h - 1, 0.0f, (float) w);
}

void SliceLane::mouseDown (const juce::MouseEvent& e)
{
    int numFrames = processor.sampleData.getNumFrames();
    if (numFrames <= 0)
        return;

    float z = processor.zoom.load();
    float sc = processor.scroll.load();
    int visLen = (int) (numFrames / z);
    int visStart = (int) (sc * (numFrames - visLen));
    int w = getWidth();
    int num = processor.sliceManager.getNumSlices();

    // Collect all overlapping slice indices at click position
    std::vector<int> overlapping;

    for (int i = 0; i < num; ++i)
    {
        const auto& s = processor.sliceManager.getSlice (i);
        if (! s.active) continue;

        if (visLen <= 0) continue;
        int x1 = (int) ((float) (s.startSample - visStart) / visLen * w);
        int x2 = (int) ((float) (s.endSample - visStart) / visLen * w);

        if (e.x >= x1 && e.x < x2)
            overlapping.push_back (i);
    }

    if (! overlapping.empty())
    {
        int current = processor.sliceManager.selectedSlice;

        // If current selection is in the list, cycle to the next one
        auto it = std::find (overlapping.begin(), overlapping.end(), current);
        if (it != overlapping.end())
        {
            ++it;
            if (it == overlapping.end())
                it = overlapping.begin();
            processor.sliceManager.selectedSlice = *it;
        }
        else
        {
            processor.sliceManager.selectedSlice = overlapping.front();
        }
    }
}

void SliceLane::updateMidiButtonAppearance (bool active)
{
    if (active)
    {
        midiSelectBtn.setColour (juce::TextButton::textColourOnId, Theme::accent);
        midiSelectBtn.setColour (juce::TextButton::textColourOffId, Theme::accent);
        midiSelectBtn.setColour (juce::TextButton::buttonColourId, Theme::accent.withAlpha (0.2f));
    }
    else
    {
        midiSelectBtn.setColour (juce::TextButton::textColourOnId, Theme::foreground);
        midiSelectBtn.setColour (juce::TextButton::textColourOffId, Theme::foreground);
        midiSelectBtn.setColour (juce::TextButton::buttonColourId, Theme::button);
    }
}
