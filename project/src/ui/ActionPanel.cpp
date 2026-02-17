#include "ActionPanel.h"
#include "AutoChopPanel.h"
#include "IntersectLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"
#include "../audio/AudioAnalysis.h"

ActionPanel::ActionPanel (IntersectProcessor& p, WaveformView& wv)
    : processor (p), waveformView (wv)
{
    addAndMakeVisible (addSliceBtn);
    addAndMakeVisible (lazyChopBtn);
    addAndMakeVisible (dupBtn);
    addAndMakeVisible (splitBtn);
    addAndMakeVisible (deleteBtn);
    addAndMakeVisible (snapBtn);
    addAndMakeVisible (midiSelectBtn);

    // Style all buttons to match M button color scheme
    for (auto* btn : { &addSliceBtn, &lazyChopBtn, &dupBtn, &splitBtn, &deleteBtn, &snapBtn, &midiSelectBtn })
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
        if (autoChopPanel != nullptr)
        {
            // Already showing — close it
            if (auto* parent = autoChopPanel->getParentComponent())
                parent->removeChildComponent (autoChopPanel.get());
            autoChopPanel.reset();
            return;
        }

        autoChopPanel = std::make_unique<AutoChopPanel> (processor, waveformView);

        // Add to editor (top-level) so it overlays the waveform
        if (auto* editor = getTopLevelComponent())
        {
            int panelW = 340;
            int panelH = 130;
            auto wfBounds = waveformView.getBoundsInParent();
            int cx = wfBounds.getCentreX() - panelW / 2;
            int cy = wfBounds.getCentreY() - panelH / 2;
            autoChopPanel->setBounds (cx, cy, panelW, panelH);
            editor->addAndMakeVisible (*autoChopPanel);
        }
    };

    snapBtn.setTooltip ("Snap to zero-crossing (ZX)");
    snapBtn.onClick = [this] {
        bool current = processor.snapToZeroCrossing.load();
        bool newState = ! current;
        processor.snapToZeroCrossing.store (newState);
        updateSnapButtonAppearance (newState);
        repaint();
    };
    updateSnapButtonAppearance (false);

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

    midiSelectBtn.setTooltip ("Follow MIDI (auto-select slice on note)");
    midiSelectBtn.onClick = [this] {
        bool current = processor.midiSelectsSlice.load();
        bool newState = ! current;
        processor.midiSelectsSlice.store (newState);
        updateMidiButtonAppearance (newState);
        repaint();
    };
    updateMidiButtonAppearance (false);
}

ActionPanel::~ActionPanel() = default;

void ActionPanel::resized()
{
    int gap = 6;
    int btnH = getHeight();
    int narrowW = 30;  // ZX and FM buttons
    int narrowTotal = narrowW * 2 + gap;  // ZX + FM + gap between them
    int availW = getWidth() - narrowTotal - gap;
    int numBtns = 5;
    int totalGap = gap * (numBtns - 1);
    int btnW = (availW - totalGap) / numBtns;

    addSliceBtn.setBounds (0, 0, btnW, btnH);
    lazyChopBtn.setBounds (btnW + gap, 0, btnW, btnH);
    splitBtn.setBounds (2 * (btnW + gap), 0, btnW, btnH);
    dupBtn.setBounds (3 * (btnW + gap), 0, btnW, btnH);
    deleteBtn.setBounds (4 * (btnW + gap), 0, btnW, btnH);
    snapBtn.setBounds (getWidth() - narrowW * 2 - gap, 0, narrowW, btnH);
    midiSelectBtn.setBounds (getWidth() - narrowW, 0, narrowW, btnH);
}

void ActionPanel::paint (juce::Graphics& g)
{
    // Sync button appearances
    updateMidiButtonAppearance (processor.midiSelectsSlice.load());
    updateSnapButtonAppearance (processor.snapToZeroCrossing.load());

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

void ActionPanel::updateSnapButtonAppearance (bool active)
{
    if (active)
    {
        snapBtn.setColour (juce::TextButton::textColourOnId, getTheme().accent);
        snapBtn.setColour (juce::TextButton::textColourOffId, getTheme().accent);
        snapBtn.setColour (juce::TextButton::buttonColourId, getTheme().accent.withAlpha (0.2f));
    }
    else
    {
        snapBtn.setColour (juce::TextButton::textColourOnId, getTheme().foreground);
        snapBtn.setColour (juce::TextButton::textColourOffId, getTheme().foreground);
        snapBtn.setColour (juce::TextButton::buttonColourId, getTheme().button);
    }
}
