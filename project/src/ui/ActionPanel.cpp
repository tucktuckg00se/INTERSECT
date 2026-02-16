#include "ActionPanel.h"
#include "IntersectLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"

ActionPanel::ActionPanel (IntersectProcessor& p, WaveformView& wv)
    : processor (p), waveformView (wv)
{
    addAndMakeVisible (addSliceBtn);
    addAndMakeVisible (lazyChopBtn);
    addAndMakeVisible (dupBtn);
    addAndMakeVisible (splitBtn);
    addAndMakeVisible (deleteBtn);
    addAndMakeVisible (midiSelectBtn);

    // Style all buttons to match M button color scheme
    for (auto* btn : { &addSliceBtn, &lazyChopBtn, &dupBtn, &splitBtn, &deleteBtn, &midiSelectBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, getTheme().button);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().foreground);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().foreground);
    }

    addSliceBtn.onClick = [this] {
        waveformView.sliceDrawMode = ! waveformView.sliceDrawMode;
        waveformView.setMouseCursor (waveformView.sliceDrawMode
            ? juce::MouseCursor::IBeamCursor
            : juce::MouseCursor::NormalCursor);
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

    splitBtn.onClick = [this] {
        auto* aw = new juce::AlertWindow ("Split Slice", "Number of slices:",
                                           juce::MessageBoxIconType::NoIcon);
        aw->setColour (juce::AlertWindow::backgroundColourId, getTheme().darkBar);
        aw->setColour (juce::AlertWindow::textColourId, getTheme().foreground);
        aw->setColour (juce::AlertWindow::outlineColourId, getTheme().separator);
        aw->addTextEditor ("count", "16");
        if (auto* te = aw->getTextEditor ("count"))
        {
            te->setColour (juce::TextEditor::backgroundColourId, getTheme().darkBar.brighter (0.15f));
            te->setColour (juce::TextEditor::textColourId, getTheme().foreground);
            te->setColour (juce::TextEditor::outlineColourId, getTheme().separator);
        }
        aw->addButton ("OK", 1);
        aw->addButton ("Cancel", 0);
        if (auto* topLvl = getTopLevelComponent())
            aw->centreAroundComponent (topLvl, aw->getWidth(), aw->getHeight());
        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, aw] (int result) {
                if (result == 1)
                {
                    int count = aw->getTextEditorContents ("count").getIntValue();
                    if (count >= 2 && count <= 128)
                    {
                        IntersectProcessor::Command cmd;
                        cmd.type = IntersectProcessor::CmdSplitSlice;
                        cmd.intParam1 = count;
                        processor.pushCommand (cmd);
                    }
                }
                delete aw;
            }));
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

void ActionPanel::resized()
{
    int gap = 6;
    int btnH = getHeight();
    int mBtnW = 24;  // narrow M button
    int availW = getWidth() - mBtnW - gap;
    int numBtns = 5;
    int totalGap = gap * (numBtns - 1);
    int btnW = (availW - totalGap) / numBtns;

    addSliceBtn.setBounds (0, 0, btnW, btnH);
    lazyChopBtn.setBounds (btnW + gap, 0, btnW, btnH);
    splitBtn.setBounds (2 * (btnW + gap), 0, btnW, btnH);
    dupBtn.setBounds (3 * (btnW + gap), 0, btnW, btnH);
    deleteBtn.setBounds (4 * (btnW + gap), 0, btnW, btnH);
    midiSelectBtn.setBounds (getWidth() - mBtnW, 0, mBtnW, btnH);
}

void ActionPanel::paint (juce::Graphics& g)
{
    // Sync M button appearance
    updateMidiButtonAppearance (processor.midiSelectsSlice.load());

    // Highlight +SLC if in draw mode
    if (waveformView.sliceDrawMode)
    {
        g.setColour (getTheme().accent.withAlpha (0.25f));
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
        lazyChopBtn.setButtonText ("LAZY");
    }
}

void ActionPanel::updateMidiButtonAppearance (bool active)
{
    if (active)
    {
        midiSelectBtn.setColour (juce::TextButton::textColourOnId, getTheme().accent);
        midiSelectBtn.setColour (juce::TextButton::textColourOffId, getTheme().accent);
        midiSelectBtn.setColour (juce::TextButton::buttonColourId, getTheme().accent.withAlpha (0.2f));
    }
    else
    {
        midiSelectBtn.setColour (juce::TextButton::textColourOnId, getTheme().foreground);
        midiSelectBtn.setColour (juce::TextButton::textColourOffId, getTheme().foreground);
        midiSelectBtn.setColour (juce::TextButton::buttonColourId, getTheme().button);
    }
}
