#include "ActionPanel.h"
#include "IntersectLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"

ActionPanel::ActionPanel (IntersectProcessor& p, WaveformView& wv)
    : processor (p), waveformView (wv)
{
    addAndMakeVisible (loadBtn);
    addAndMakeVisible (addSliceBtn);
    addAndMakeVisible (lazyChopBtn);
    addAndMakeVisible (dupBtn);
    addAndMakeVisible (deleteBtn);

    loadBtn.onClick = [this] {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Load Audio File",
            juce::File(),
            "*.wav;*.ogg;*.aiff;*.flac;*.mp3");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    IntersectProcessor::Command cmd;
                    cmd.type = IntersectProcessor::CmdLoadFile;
                    cmd.fileParam = result;
                    processor.pushCommand (cmd);
                    processor.zoom.store (1.0f);
                    processor.scroll.store (0.0f);
                }
            });
    };

    addSliceBtn.onClick = [this] {
        waveformView.sliceDrawMode = ! waveformView.sliceDrawMode;
        repaint();
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

    dupBtn.onClick = [this] {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdDuplicateSlice;
        processor.pushCommand (cmd);
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
}

void ActionPanel::resized()
{
    int gap = 6;
    int numBtns = 5;
    int totalGap = gap * (numBtns - 1);
    int btnW = (getWidth() - totalGap) / numBtns;
    int btnH = getHeight();

    loadBtn.setBounds (0, 0, btnW, btnH);
    addSliceBtn.setBounds (btnW + gap, 0, btnW, btnH);
    lazyChopBtn.setBounds (2 * (btnW + gap), 0, btnW, btnH);
    dupBtn.setBounds (3 * (btnW + gap), 0, btnW, btnH);
    deleteBtn.setBounds (4 * (btnW + gap), 0, btnW, btnH);
}

void ActionPanel::paint (juce::Graphics& g)
{
    // Highlight +SLC if in draw mode
    if (waveformView.sliceDrawMode)
    {
        g.setColour (Theme::accent.withAlpha (0.25f));
        g.fillRect (addSliceBtn.getBounds());
    }

    // Highlight LZY if active
    if (processor.lazyChop.isActive())
    {
        lazyChopBtn.setButtonText ("STOP");
        g.setColour (juce::Colours::red.withAlpha (0.25f));
        g.fillRect (lazyChopBtn.getBounds());
    }
    else
    {
        lazyChopBtn.setButtonText ("LZY");
    }
}
