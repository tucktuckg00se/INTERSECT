#include "SliceListPanel.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"

SliceListPanel::SliceListPanel (IntersectProcessor& p) : processor (p) {}

void SliceListPanel::paint (juce::Graphics& g)
{
    int num = processor.sliceManager.getNumSlices();
    int sel = processor.sliceManager.selectedSlice;

    // Label
    g.setFont (juce::Font (9.0f));
    g.setColour (getTheme().foreground.withAlpha (0.4f));
    g.drawText ("SLICES", 0, 0, kBtnW, 10, juce::Justification::centredLeft);

    int y = 12;
    int maxVisible = (getHeight() - 24) / (kBtnH + 2);

    g.setFont (juce::Font (11.0f));

    for (int i = 0; i < num && i < maxVisible; ++i)
    {
        int btnY = y + i * (kBtnH + 2);

        if (i == sel)
            g.setColour (getTheme().slicePinkSel);
        else
            g.setColour (getTheme().slicePink.withAlpha (0.7f));

        g.fillRect (0, btnY, kBtnW, kBtnH);

        g.setColour (juce::Colours::white.withAlpha (i == sel ? 1.0f : 0.8f));
        g.drawText ("Slice " + juce::String (i + 1), 0, btnY, kBtnW, kBtnH,
                     juce::Justification::centred);
    }

    if (num > maxVisible)
    {
        g.setColour (getTheme().foreground.withAlpha (0.4f));
        g.setFont (juce::Font (10.0f));
        g.drawText ("+" + juce::String (num - maxVisible) + " more",
                     0, y + maxVisible * (kBtnH + 2), kBtnW, 14,
                     juce::Justification::centred);
    }
}

void SliceListPanel::mouseDown (const juce::MouseEvent& e)
{
    int y = 12;
    int num = processor.sliceManager.getNumSlices();
    int maxVisible = (getHeight() - 24) / (kBtnH + 2);

    for (int i = 0; i < num && i < maxVisible; ++i)
    {
        int btnY = y + i * (kBtnH + 2);
        if (e.x >= 0 && e.x < kBtnW &&
            e.y >= btnY && e.y < btnY + kBtnH)
        {
            processor.sliceManager.selectedSlice = i;
            getParentComponent()->repaint();
            return;
        }
    }
}
