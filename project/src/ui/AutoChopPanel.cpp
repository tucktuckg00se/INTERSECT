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
    sensitivitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 36, 20);
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
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setColour (getTheme().separator);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

    g.setFont (IntersectLookAndFeel::makeFont (14.0f, true));
    g.setColour (getTheme().foreground);
    g.drawText ("Auto Chop", 12, 8, 120, 18, juce::Justification::centredLeft);

    // Separator between header and content
    g.setColour (getTheme().separator.withAlpha (0.5f));
    g.drawHorizontalLine (30, 12.0f, (float) getWidth() - 12.0f);
}

void AutoChopPanel::resized()
{
    int w = getWidth();
    int pad = 12;
    int contentW = w - pad * 2;
    int halfW = (contentW - 8) / 2;  // 8px gap between columns
    int leftX = pad;
    int rightX = pad + halfW + 8;
    int btnH = 24;
    int rowH = 22;

    // Left column: Sensitivity + Detect Transients
    sensitivitySlider.setBounds (leftX, 36, halfW, rowH);
    detectBtn.setBounds         (leftX, 36 + rowH + 6, halfW, btnH);

    // Right column: Divisions + Split Equal
    divisionsEditor.setBounds   (rightX, 36, halfW, rowH);
    splitEqualBtn.setBounds     (rightX, 36 + rowH + 6, halfW, btnH);

    // Cancel centered at bottom
    int cancelW = 80;
    cancelBtn.setBounds ((w - cancelW) / 2, 36 + rowH + 6 + btnH + 10, cancelW, btnH);
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
