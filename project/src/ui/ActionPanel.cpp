#include "ActionPanel.h"
#include "TuckersLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"

ActionPanel::ActionPanel (IntersectProcessor& p, WaveformView& wv)
    : processor (p), waveformView (wv)
{
    addAndMakeVisible (addSliceBtn);
    addAndMakeVisible (deleteBtn);
    addAndMakeVisible (lazyChopBtn);

    addSliceBtn.onClick = [this] {
        waveformView.sliceDrawMode = ! waveformView.sliceDrawMode;
        repaint();
    };

    deleteBtn.onClick = [this] {
        int sel = processor.sliceManager.selectedSlice;
        if (sel >= 0)
        {
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdDeleteSlice;
            cmd.intParam1 = sel;
            processor.pushCommand (cmd);
        }
    };

    lazyChopBtn.onClick = [this] {
        IntersectProcessor::Command cmd;
        if (processor.lazyChop.isActive())
            cmd.type = IntersectProcessor::CmdLazyChopStop;
        else
            cmd.type = IntersectProcessor::CmdLazyChopStart;
        processor.pushCommand (cmd);
        repaint();
    };
}

void ActionPanel::resized()
{
    int gap = 6;
    int numBtns = 3;
    int totalGap = gap * (numBtns - 1);
    int btnW = (getWidth() - totalGap) / numBtns;
    int btnH = getHeight();

    addSliceBtn.setBounds (0, 0, btnW, btnH);
    deleteBtn.setBounds (btnW + gap, 0, btnW, btnH);
    lazyChopBtn.setBounds (2 * (btnW + gap), 0, btnW, btnH);
}

void ActionPanel::paint (juce::Graphics& g)
{
    // Highlight +SLICE if in draw mode
    if (waveformView.sliceDrawMode)
    {
        g.setColour (Theme::accent.withAlpha (0.25f));
        g.fillRect (addSliceBtn.getBounds());
    }

    // Highlight LAZY CHOP if active
    if (processor.lazyChop.isActive())
    {
        lazyChopBtn.setButtonText ("STOP CHOP");
        g.setColour (juce::Colours::red.withAlpha (0.25f));
        g.fillRect (lazyChopBtn.getBounds());
    }
    else
    {
        lazyChopBtn.setButtonText ("LAZY CHOP");
    }
}
