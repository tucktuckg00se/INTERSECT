#include "AutoChopPanel.h"
#include "IntersectLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"
#include <algorithm>

AutoChopPanel::AutoChopPanel (IntersectProcessor& p, WaveformView& wv)
    : processor (p),
      waveformView (wv),
      sensCell { {}, "SENS", 50.0f, 0.0f, 100.0f, 1.0f, "" },
      minCell  { {}, "MIN",  100.0f, 20.0f, 500.0f, 1.0f, "ms" },
      divCell  { {}, "DIV",  16.0f, 2.0f, 128.0f, 1.0f, "" }
{
    addAndMakeVisible (splitEqualBtn);
    addAndMakeVisible (detectBtn);
    addAndMakeVisible (cancelBtn);

    for (auto* btn : { &splitEqualBtn, &detectBtn, &cancelBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, getTheme().surface4);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().text2);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().text2);
    }

    splitEqualBtn.setTooltip ("Split equal");
    detectBtn.setTooltip ("Split transients");
    cancelBtn.setTooltip ("Close auto chop");

    splitEqualBtn.onClick = [this] {
        int count = (int) divCell.value;
        if (count >= 2 && count <= 128)
        {
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdSplitSlice;
            cmd.intParam1 = count;
            cmd.sliceIdx = processor.sliceManager.selectedSlice.load();
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
            cmd.sliceIdx = processor.sliceManager.selectedSlice.load();
            cmd.numPositions = 0;
            for (int pos : waveformView.transientPreviewPositions)
                if (cmd.numPositions < (int) cmd.positions.size())
                    cmd.positions[(size_t) cmd.numPositions++] = pos;
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

    // Hide action controls until ODF is ready (keep cancel visible)
    splitEqualBtn.setVisible (false);
    detectBtn.setVisible (false);

    // Kick off async ODF computation instead of blocking
    startODFComputation();
}

AutoChopPanel::~AutoChopPanel()
{
    stopTimer();
    dismissTextEditor();

    if (odfThread != nullptr)
    {
        odfThread->signalThreadShouldExit();
        odfThread->stopThread (500);
        odfThread.reset();
    }

    waveformView.transientPreviewPositions.clear();
    waveformView.repaint();
}

void AutoChopPanel::paint (juce::Graphics& g)
{
    g.setColour (getTheme().surface2.withAlpha (0.95f));
    g.fillRect (getLocalBounds());

    g.setColour (getTheme().surface5);
    g.drawRect (getLocalBounds(), 1);

    // Show analyzing indicator while ODF is computing
    if (! odfReady)
    {
        g.setFont (IntersectLookAndFeel::makeFont (10.0f));
        g.setColour (getTheme().text2.withAlpha (0.5f));
        g.drawText ("Analyzing...", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Draw each param cell
    auto drawCell = [&] (const ParamCell& cell)
    {
        // Background
        g.setColour (getTheme().surface3);
        g.fillRoundedRectangle (cell.bounds.toFloat(), 3.0f);

        auto content = cell.bounds.reduced (4, 0);

        // Label
        g.setFont (IntersectLookAndFeel::makeFont (9.0f, true));
        g.setColour (getTheme().text2.withAlpha (0.6f));
        g.drawText (cell.label, content.getX(), content.getY() + 1,
                    content.getWidth(), 10, juce::Justification::centredLeft);

        // Value
        juce::String valueText = juce::String ((int) cell.value) + cell.suffix;
        g.setFont (IntersectLookAndFeel::makeFont (11.0f));
        g.setColour (getTheme().text1);
        g.drawText (valueText, content.getX(), content.getY() + 11,
                    content.getWidth(), content.getHeight() - 12, juce::Justification::centredLeft);
    };

    drawCell (sensCell);
    drawCell (minCell);
    drawCell (divCell);
}

void AutoChopPanel::resized()
{
    int h = getHeight();
    int pad = 4;
    int btnH = h - pad * 2;
    int gap = 6;

    int cancelW = 60;
    cancelBtn.setBounds (getWidth() - cancelW - pad, pad, cancelW, btnH);

    int x = pad;

    // SENS cell
    sensCell.bounds = { x, pad, 52, btnH };
    x += 52 + gap;

    // MIN cell
    minCell.bounds = { x, pad, 56, btnH };
    x += 56 + gap;

    // SPLIT TRANSIENTS button
    int transBtnW = 148;
    detectBtn.setBounds (x, pad, transBtnW, btnH);
    x += transBtnW + gap + 16;

    // DIV cell
    divCell.bounds = { x, pad, 48, btnH };
    x += 48 + gap;

    // SPLIT EQUAL button
    int equalBtnW = 96;
    splitEqualBtn.setBounds (x, pad, equalBtnW, btnH);
}

int AutoChopPanel::hitTestCell (juce::Point<int> pos) const
{
    if (sensCell.bounds.contains (pos)) return 0;
    if (minCell.bounds.contains (pos))  return 1;
    if (divCell.bounds.contains (pos))  return 2;
    return -1;
}

void AutoChopPanel::mouseDown (const juce::MouseEvent& e)
{
    dismissTextEditor();
    activeDragCell = -1;

    int idx = hitTestCell (e.getPosition());
    if (idx < 0)
        return;

    activeDragCell = idx;
    dragStartY = e.y;

    ParamCell* cell = (idx == 0) ? &sensCell : (idx == 1) ? &minCell : &divCell;
    dragStartValue = cell->value;
}

void AutoChopPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (activeDragCell < 0)
        return;

    ParamCell* cell = (activeDragCell == 0) ? &sensCell
                    : (activeDragCell == 1) ? &minCell
                                            : &divCell;

    float deltaY = (float) (dragStartY - e.y);
    float range = cell->maxVal - cell->minVal;
    float sensitivity = e.mods.isShiftDown() ? cell->step : (range / 200.0f);

    float newValue = dragStartValue + deltaY * sensitivity;
    newValue = juce::jlimit (cell->minVal, cell->maxVal, std::round (newValue / cell->step) * cell->step);
    cell->value = newValue;

    // Debounced live preview for SENS and MIN
    if (activeDragCell <= 1 && odfReady)
    {
        if (! debounceScheduled)
        {
            debounceScheduled = true;
            startTimer (40); // ~25Hz update rate
        }
    }

    repaint();
}

void AutoChopPanel::mouseUp (const juce::MouseEvent&)
{
    // Flush any pending debounced preview on mouse up
    if (activeDragCell <= 1 && debounceScheduled && odfReady)
    {
        stopTimer();
        debounceScheduled = false;
        updatePreviewFromCachedODF();
    }

    activeDragCell = -1;
}

void AutoChopPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    int idx = hitTestCell (e.getPosition());
    if (idx < 0)
        return;

    ParamCell* cell = (idx == 0) ? &sensCell : (idx == 1) ? &minCell : &divCell;
    showTextEditor (*cell);
}

void AutoChopPanel::showTextEditor (ParamCell& cell)
{
    dismissTextEditor();
    textEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*textEditor);

    auto valueBounds = cell.bounds.reduced (4, 0).withTrimmedTop (11).expanded (1, 1);
    textEditor->setBounds (valueBounds);
    textEditor->setFont (IntersectLookAndFeel::makeFont (11.0f));
    textEditor->setColour (juce::TextEditor::backgroundColourId, getTheme().surface2.brighter (0.12f).withAlpha (0.98f));
    textEditor->setColour (juce::TextEditor::textColourId, getTheme().text1.brighter (0.3f));
    textEditor->setColour (juce::TextEditor::outlineColourId, getTheme().surface5.withAlpha (0.85f));
    textEditor->setColour (juce::TextEditor::focusedOutlineColourId, getTheme().accent.withAlpha (0.9f));
    textEditor->setColour (juce::TextEditor::highlightColourId, getTheme().accent.withAlpha (0.25f));
    textEditor->setJustification (juce::Justification::centredLeft);
    textEditor->setBorder (juce::BorderSize<int> (1, 4, 1, 4));
    textEditor->setIndents (0, 0);
    textEditor->setEscapeAndReturnKeysConsumed (true);
    textEditor->setText (juce::String ((int) cell.value), false);
    textEditor->selectAll();
    textEditor->grabKeyboardFocus();

    juce::Component::SafePointer<AutoChopPanel> safeThis (this);
    ParamCell* cellPtr = &cell;

    auto dismissEditorLater = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr)
                return;
            safeThis->dismissTextEditor();
            safeThis->repaint();
        });
    };

    textEditor->onReturnKey = [safeThis, cellPtr]
    {
        if (safeThis == nullptr || safeThis->textEditor == nullptr)
            return;

        float newValue = safeThis->textEditor->getText().getFloatValue();
        newValue = juce::jlimit (cellPtr->minVal, cellPtr->maxVal,
                                 std::round (newValue / cellPtr->step) * cellPtr->step);

        juce::MessageManager::callAsync ([safeThis, cellPtr, newValue]
        {
            if (safeThis == nullptr)
                return;
            safeThis->dismissTextEditor();
            cellPtr->value = newValue;
            // Update preview for SENS/MIN cells
            if (cellPtr == &safeThis->sensCell || cellPtr == &safeThis->minCell)
                safeThis->updatePreviewFromCachedODF();
            safeThis->repaint();
        });
    };

    textEditor->onEscapeKey = [safeThis, dismissEditorLater]
    {
        if (safeThis == nullptr)
            return;
        dismissEditorLater();
    };

    textEditor->onFocusLost = [safeThis, dismissEditorLater]
    {
        if (safeThis == nullptr)
            return;
        dismissEditorLater();
    };
}

void AutoChopPanel::dismissTextEditor()
{
    if (textEditor == nullptr)
        return;

    textEditor->onReturnKey = nullptr;
    textEditor->onEscapeKey = nullptr;
    textEditor->onFocusLost = nullptr;
    textEditor.reset();
}

void AutoChopPanel::startODFComputation()
{
    odfReady = false;
    repaint();

    auto sampleSnap = processor.sampleData.getSnapshot();
    const auto& ui = processor.getUiSliceSnapshot();
    int sel = ui.selectedSlice;
    if (sel < 0 || sel >= ui.numSlices || sampleSnap == nullptr)
    {
        waveformView.transientPreviewPositions.clear();
        waveformView.repaint();
        return;
    }

    const auto& s = ui.slices[(size_t) sel];
    int sliceStart = s.startSample;
    int sliceEnd   = s.endSample;

    const double sampleRate = sampleSnap->decodedSampleRate > 0.0 ? sampleSnap->decodedSampleRate : 44100.0;

    // Hold onto the snapshot so the buffer stays alive for the thread
    odfBufferSnapshot = std::make_shared<juce::AudioBuffer<float>> (sampleSnap->buffer);
    cachedSliceStart = sliceStart;
    cachedSliceEnd   = sliceEnd;

    // Clean up any previous thread
    if (odfThread != nullptr)
    {
        odfThread->signalThreadShouldExit();
        odfThread->stopThread (500);
    }

    odfThread = std::make_unique<ODFThread> (*odfBufferSnapshot, sliceStart, sliceEnd, sampleRate);
    odfThread->startThread (juce::Thread::Priority::background);

    // Poll for completion
    startTimer (50);
}

void AutoChopPanel::timerCallback()
{
    // Check if debounced slider drag needs flushing
    if (debounceScheduled && odfReady)
    {
        stopTimer();
        debounceScheduled = false;
        updatePreviewFromCachedODF();
        return;
    }

    // Check if background ODF computation finished
    if (odfThread != nullptr && ! odfThread->isThreadRunning())
    {
        stopTimer();
        cachedODF = std::move (odfThread->result);
        odfThread.reset();
        odfReady = true;

        splitEqualBtn.setVisible (true);
        detectBtn.setVisible (true);
        cancelBtn.setVisible (true);

        updatePreviewFromCachedODF();
        repaint(); // redraw to show controls instead of "Analyzing..."
    }
}

void AutoChopPanel::updatePreviewFromCachedODF()
{
    if (! odfReady)
        return;

    auto sampleSnap = processor.sampleData.getSnapshot();
    if (sampleSnap == nullptr)
    {
        waveformView.transientPreviewPositions.clear();
        waveformView.repaint();
        return;
    }

    float sens = sensCell.value * 0.1f;
    float minMs = minCell.value;
    const double sampleRate = sampleSnap->decodedSampleRate > 0.0 ? sampleSnap->decodedSampleRate : 44100.0;

    auto positions = AudioAnalysis::pickTransientsFromODF (
        cachedODF, sampleSnap->buffer, sens, sampleRate, minMs);

    if (processor.snapToZeroCrossing.load())
    {
        int sliceStart = cachedSliceStart;
        int sliceEnd = cachedSliceEnd;
        std::transform (positions.begin(), positions.end(), positions.begin(),
                        [sampleSnap, sliceStart, sliceEnd] (int p) {
                            int snapped = AudioAnalysis::findNearestZeroCrossing (
                                sampleSnap->buffer, p);
                            return juce::jlimit (sliceStart + 1, sliceEnd - 1, snapped);
                        });

        std::sort (positions.begin(), positions.end());
        int minDist = (int) std::round (sampleRate * (double) minMs / 1000.0);
        std::vector<int> sanitized;
        int lastPos = sliceStart - minDist;
        for (int p : positions)
        {
            if (p - lastPos >= minDist)
            {
                sanitized.push_back (p);
                lastPos = p;
            }
        }
        positions = std::move (sanitized);
    }

    if (positions.size() > 128)
        positions.resize (128);

    waveformView.transientPreviewPositions = std::move (positions);
    waveformView.repaint();
}

void AutoChopPanel::updatePreview()
{
    auto sampleSnap = processor.sampleData.getSnapshot();
    const auto& ui = processor.getUiSliceSnapshot();
    int sel = ui.selectedSlice;
    if (sel < 0 || sel >= ui.numSlices || sampleSnap == nullptr)
    {
        waveformView.transientPreviewPositions.clear();
        waveformView.repaint();
        return;
    }

    const auto& s = ui.slices[(size_t) sel];

    // If slice bounds changed, need to recompute ODF
    if (s.startSample != cachedSliceStart || s.endSample != cachedSliceEnd)
    {
        startODFComputation();
        return;
    }

    // Otherwise just re-pick from cached ODF
    updatePreviewFromCachedODF();
}
