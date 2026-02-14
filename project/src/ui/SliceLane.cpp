#include "SliceLane.h"
#include "TuckersLookAndFeel.h"
#include "../PluginProcessor.h"

SliceLane::SliceLane (IntersectProcessor& p) : processor (p)
{
    addAndMakeVisible (dupBtn);
    dupBtn.setTooltip ("Duplicate selected slice");
    dupBtn.onClick = [this] {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdDuplicateSlice;
        processor.pushCommand (cmd);
        repaint();
    };

    addAndMakeVisible (midiSelectBtn);
    midiSelectBtn.setTooltip ("MIDI selects slice");
    midiSelectBtn.onClick = [this] {
        bool current = processor.midiSelectsSlice.load();
        processor.midiSelectsSlice.store (! current);
        repaint();
    };
}

void SliceLane::resized()
{
    int btnW = 20;
    midiSelectBtn.setBounds (getWidth() - btnW - 2, 2, btnW, getHeight() - 4);
    dupBtn.setBounds (getWidth() - btnW * 2 - 4, 2, btnW, getHeight() - 4);
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

    for (int i = 0; i < num; ++i)
    {
        const auto& s = processor.sliceManager.getSlice (i);
        if (! s.active) continue;

        if (visLen <= 0) continue;
        int x1 = (int) ((float) (s.startSample - visStart) / visLen * w);
        int x2 = (int) ((float) (s.endSample - visStart) / visLen * w);

        // Clip to visible area
        x1 = std::max (0, x1);
        x2 = std::min (w, x2);
        int sw = x2 - x1;
        if (sw < 2) continue;

        bool selected = (i == sel);

        // Bar fill
        auto col = s.colour;
        g.setColour (selected ? col.withAlpha (0.55f) : col.withAlpha (0.25f));
        g.fillRect (x1, 1, sw, h - 2);

        // Border for selected
        if (selected)
        {
            g.setColour (col.withAlpha (0.9f));
            g.drawRect (x1, 1, sw, h - 2, 1);
        }

        // Slice number label
        if (sw > 14)
        {
            g.setColour (selected ? juce::Colours::white.withAlpha (0.9f) : col.withAlpha (0.7f));
            g.setFont (juce::Font (9.0f).boldened());
            g.drawText (juce::String (i + 1), x1, 0, sw, h, juce::Justification::centred);
        }
    }

    // Highlight M button if active
    if (processor.midiSelectsSlice.load())
    {
        g.setColour (Theme::accent.withAlpha (0.3f));
        g.fillRect (midiSelectBtn.getBounds());
    }

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
