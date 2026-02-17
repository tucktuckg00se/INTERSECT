#include "AutoChopPanel.h"
#include "IntersectLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"
#include "../audio/AudioAnalysis.h"

AutoChopPanel::AutoChopPanel (IntersectProcessor& p, WaveformView& wv)
    : processor (p), waveformView (wv)
{
    addAndMakeVisible (sensitivitySlider);
    addAndMakeVisible (divisionsEditor);
    addAndMakeVisible (splitEqualBtn);
    addAndMakeVisible (detectBtn);
    addAndMakeVisible (cancelBtn);

    sensitivitySlider.setRange (0.0, 100.0, 1.0);
    sensitivitySlider.setValue (50.0, juce::dontSendNotification);
    sensitivitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sensitivitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 32, 20);
    sensitivitySlider.setColour (juce::Slider::trackColourId, getTheme().accent);
    sensitivitySlider.setColour (juce::Slider::thumbColourId, getTheme().foreground);
    sensitivitySlider.setColour (juce::Slider::backgroundColourId, getTheme().darkBar);
    sensitivitySlider.setColour (juce::Slider::textBoxTextColourId, getTheme().foreground);
    sensitivitySlider.setColour (juce::Slider::textBoxBackgroundColourId, getTheme().darkBar.brighter (0.15f));
    sensitivitySlider.setColour (juce::Slider::textBoxOutlineColourId, getTheme().separator);

    sensitivitySlider.onValueChange = [this] { updatePreview(); };

    divisionsEditor.setText ("16");
    divisionsEditor.setColour (juce::TextEditor::backgroundColourId, getTheme().darkBar.brighter (0.15f));
    divisionsEditor.setColour (juce::TextEditor::textColourId, getTheme().foreground);
    divisionsEditor.setColour (juce::TextEditor::outlineColourId, getTheme().separator);
    divisionsEditor.setFont (IntersectLookAndFeel::makeFont (13.0f));
    divisionsEditor.setJustification (juce::Justification::centred);

    for (auto* btn : { &splitEqualBtn, &detectBtn, &cancelBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, getTheme().button);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().foreground);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().foreground);
    }

    splitEqualBtn.onClick = [this] {
        int count = divisionsEditor.getText().getIntValue();
        if (count >= 2 && count <= 128)
        {
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdSplitSlice;
            cmd.intParam1 = count;
            processor.pushCommand (cmd);
        }
        waveformView.transientPreviewPositions.clear();
        waveformView.repaint();
        if (auto* parent = getParentComponent())
            parent->removeChildComponent (this);
    };

    detectBtn.onClick = [this] {
        if (! waveformView.transientPreviewPositions.empty())
        {
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdTransientChop;
            cmd.positions = waveformView.transientPreviewPositions;
            processor.pushCommand (cmd);
        }
        waveformView.transientPreviewPositions.clear();
        waveformView.repaint();
        if (auto* parent = getParentComponent())
            parent->removeChildComponent (this);
    };

    cancelBtn.onClick = [this] {
        waveformView.transientPreviewPositions.clear();
        waveformView.repaint();
        if (auto* parent = getParentComponent())
            parent->removeChildComponent (this);
    };

    // Generate initial preview
    updatePreview();
}

AutoChopPanel::~AutoChopPanel()
{
    waveformView.transientPreviewPositions.clear();
    waveformView.repaint();
}

void AutoChopPanel::paint (juce::Graphics& g)
{
    g.setColour (getTheme().darkBar.withAlpha (0.95f));
    g.fillRect (getLocalBounds());

    g.setColour (getTheme().separator);
    g.drawRect (getLocalBounds(), 1);

    // Labels drawn inline before their controls
    g.setFont (IntersectLookAndFeel::makeFont (11.0f));
    g.setColour (getTheme().foreground.withAlpha (0.6f));
    g.drawText ("SENS", 4, 0, 30, getHeight(), juce::Justification::centredLeft);
    g.drawText ("DIV", divisionsEditor.getX() - 26, 0, 24, getHeight(), juce::Justification::centredLeft);
}

void AutoChopPanel::resized()
{
    int h = getHeight();
    int pad = 4;
    int btnH = h - pad * 2;
    int gap = 6;

    // Layout: SENS [===slider===] [SPLIT TRANSIENTS] | DIV [16] [SPLIT EQUAL] | [CANCEL]
    // Right-to-left: place CANCEL, then work left-to-right for the rest

    int cancelW = 60;
    cancelBtn.setBounds (getWidth() - cancelW - pad, pad, cancelW, btnH);

    // Left-to-right
    int x = pad;

    // SENS label (drawn in paint at x=4, 32px wide)
    x += 34;

    // Sensitivity slider
    int sliderW = 200;
    sensitivitySlider.setBounds (x, pad, sliderW, btnH);
    x += sliderW + gap;

    // SPLIT TRANSIENTS button
    int transBtnW = 148;
    detectBtn.setBounds (x, pad, transBtnW, btnH);
    x += transBtnW + gap;

    // Separator gap between the two sections
    x += 16;

    // DIV label (drawn in paint, 26px)
    x += 26;

    // Divisions editor
    int divW = 38;
    divisionsEditor.setBounds (x, pad, divW, btnH);
    x += divW + gap;

    // SPLIT EQUAL button
    int equalBtnW = 96;
    splitEqualBtn.setBounds (x, pad, equalBtnW, btnH);
}

void AutoChopPanel::updatePreview()
{
    int sel = processor.sliceManager.selectedSlice;
    if (sel < 0 || sel >= processor.sliceManager.getNumSlices()
        || ! processor.sampleData.isLoaded())
    {
        waveformView.transientPreviewPositions.clear();
        waveformView.repaint();
        return;
    }

    const auto& s = processor.sliceManager.getSlice (sel);
    float sens = (float) sensitivitySlider.getValue() / 100.0f;

    auto positions = AudioAnalysis::detectTransients (
        processor.sampleData.getBuffer(), s.startSample, s.endSample, sens);

    if (processor.snapToZeroCrossing.load())
    {
        for (auto& p : positions)
            p = AudioAnalysis::findNearestZeroCrossing (
                processor.sampleData.getBuffer(), p);
    }

    waveformView.transientPreviewPositions = std::move (positions);
    waveformView.repaint();
}
