#include "ActionPanel.h"
#include "AutoChopPanel.h"
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
    addAndMakeVisible (snapBtn);
    addAndMakeVisible (midiSelectBtn);

    for (auto* btn : { &addSliceBtn, &lazyChopBtn, &dupBtn, &splitBtn, &deleteBtn,
                       &snapBtn, &midiSelectBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().foreground);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().foreground);
    }

    addSliceBtn.onClick = [this] { triggerAddSliceMode(); };
    lazyChopBtn.onClick = [this] { triggerLazyChop(); };
    dupBtn.onClick = [this] { triggerDuplicateSlice(); };
    splitBtn.onClick = [this] { triggerAutoChop(); };

    addSliceBtn.setTooltip ("Add Slice (Shift+A / hold Alt)");
    lazyChopBtn.setTooltip ("Lazy Chop (Shift+Z)");
    dupBtn.setTooltip ("Duplicate Slice (Shift+D)");
    splitBtn.setTooltip ("Auto Chop (Shift+C)");
    deleteBtn.setTooltip ("Delete Slice (Del)");

    snapBtn.setTooltip ("Snap to Zero-Crossing (Shift+X)");
    snapBtn.onClick = [this] { toggleSnapToZeroCrossing(); };
    updateSnapButtonAppearance (false);

    deleteBtn.onClick = [this] { triggerDeleteSelectedSlice(); };

    midiSelectBtn.setTooltip ("Follow MIDI (Shift+F)");
    midiSelectBtn.onClick = [this] { toggleFollowMidiSelection(); };
    updateMidiButtonAppearance (false);
}

ActionPanel::~ActionPanel() = default;

void ActionPanel::triggerAddSliceMode()
{
    const bool nextState = ! waveformView.isSliceDrawModeActive();
    waveformView.setSliceDrawMode (nextState);
    if (nextState)
        waveformView.showOverlayHint ("ADD mode: drag on waveform to create a slice.", 0, true);
    else
        waveformView.clearOverlayHint();
    repaint();
}

void ActionPanel::triggerLazyChop()
{
    IntersectProcessor::Command cmd;
    if (processor.lazyChop.isActive())
        cmd.type = IntersectProcessor::CmdLazyChopStop;
    else
        cmd.type = IntersectProcessor::CmdLazyChopStart;
    processor.pushCommand (cmd);
    repaint();
}

void ActionPanel::triggerDuplicateSlice()
{
    IntersectProcessor::Command cmd;
    cmd.type = IntersectProcessor::CmdDuplicateSlice;
    cmd.intParam1 = -1;
    processor.pushCommand (cmd);
    repaint();
}

void ActionPanel::triggerAutoChop()
{
    if (autoChopPanel != nullptr)
    {
        toggleAutoChop();
        return;
    }

    const auto& ui = processor.getUiSliceSnapshot();
    const int sel = ui.selectedSlice;
    if (sel < 0 || sel >= ui.numSlices)
    {
        waveformView.showOverlayHint ("Select a slice first, then press AUTO.", 2200);
        return;
    }

    toggleAutoChop();
}

void ActionPanel::triggerDeleteSelectedSlice()
{
    const auto& ui = processor.getUiSliceSnapshot();
    const int sel = ui.selectedSlice;
    if (sel < 0)
        return;

    IntersectProcessor::Command cmd;
    cmd.type = IntersectProcessor::CmdDeleteSlice;
    cmd.intParam1 = sel;
    processor.pushCommand (cmd);
}

void ActionPanel::toggleSnapToZeroCrossing()
{
    const bool newState = ! processor.snapToZeroCrossing.load();
    processor.snapToZeroCrossing.store (newState);
    updateSnapButtonAppearance (newState);
    repaint();
}

void ActionPanel::toggleFollowMidiSelection()
{
    const bool newState = ! processor.midiSelectsSlice.load();
    processor.midiSelectsSlice.store (newState);
    updateMidiButtonAppearance (newState);
    repaint();
}

void ActionPanel::toggleAutoChop()
{
    if (autoChopPanel != nullptr)
    {
        if (auto* parent = autoChopPanel->getParentComponent())
            parent->removeChildComponent (autoChopPanel.get());
        autoChopPanel.reset();
        return;
    }

    autoChopPanel = std::make_unique<AutoChopPanel> (processor, waveformView);

    if (auto* editor = waveformView.getParentComponent())
    {
        auto wfBounds = waveformView.getBoundsInParent();
        int panelH = 34;
        int panelX = wfBounds.getX();
        int panelW = wfBounds.getWidth();
        int panelY = wfBounds.getBottom() - panelH;
        autoChopPanel->setBounds (panelX, panelY, panelW, panelH);
        editor->addAndMakeVisible (*autoChopPanel);
    }
}

void ActionPanel::resized()
{
    const int btnH = getHeight();
    const int snapW = 42;
    const int midiW = 42;
    const int mainW = juce::jmax (0, getWidth() - snapW - midiW);
    const int btnW = mainW / 5;

    addSliceBtn.setBounds (0, 0, btnW, btnH);
    lazyChopBtn.setBounds (btnW, 0, btnW, btnH);
    splitBtn.setBounds (btnW * 2, 0, btnW, btnH);
    dupBtn.setBounds (btnW * 3, 0, btnW, btnH);
    deleteBtn.setBounds (btnW * 4, 0, mainW - btnW * 4, btnH);

    snapBtn.setBounds (mainW, 0, snapW, btnH);
    midiSelectBtn.setBounds (mainW + snapW, 0, midiW, btnH);
}

void ActionPanel::paint (juce::Graphics& g)
{
    auto inactiveText = juce::Colour (0xFF384858);
    auto activeText = getTheme().accent;

    for (auto* btn : { &addSliceBtn, &lazyChopBtn, &dupBtn, &splitBtn, &deleteBtn, &snapBtn, &midiSelectBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btn->setColour (juce::TextButton::textColourOnId, inactiveText);
        btn->setColour (juce::TextButton::textColourOffId, inactiveText);
    }

    updateMidiButtonAppearance (processor.midiSelectsSlice.load());
    updateSnapButtonAppearance (processor.snapToZeroCrossing.load());

    g.fillAll (getTheme().header);
    g.setColour (getTheme().moduleBorder.withAlpha (0.95f));
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    auto drawSegment = [&] (juce::TextButton& btn, juce::Colour fill, juce::Colour line)
    {
        auto bounds = btn.getBounds();
        if (! fill.isTransparent())
        {
            g.setColour (fill);
            g.fillRect (bounds);
        }

        if (bounds.getX() > 0)
        {
            g.setColour (line);
            g.drawVerticalLine (bounds.getX(), 3.0f, (float) getHeight() - 3.0f);
        }
    };

    drawSegment (addSliceBtn,
                 waveformView.isSliceDrawModeActive() ? juce::Colour (0xFF081818) : juce::Colours::transparentBlack,
                 getTheme().moduleBorder.withAlpha (0.55f));
    drawSegment (lazyChopBtn,
                 processor.lazyChop.isActive() ? juce::Colour (0xFF180808) : juce::Colours::transparentBlack,
                 getTheme().moduleBorder.withAlpha (0.55f));
    drawSegment (splitBtn,
                 autoChopPanel != nullptr ? juce::Colour (0xFF0E1218) : juce::Colours::transparentBlack,
                 getTheme().moduleBorder.withAlpha (0.55f));
    drawSegment (dupBtn, juce::Colours::transparentBlack, getTheme().moduleBorder.withAlpha (0.55f));
    drawSegment (deleteBtn, juce::Colours::transparentBlack, getTheme().moduleBorder.withAlpha (0.55f));
    drawSegment (snapBtn,
                 processor.snapToZeroCrossing.load() ? juce::Colour (0xFF081818) : juce::Colours::transparentBlack,
                 getTheme().moduleBorder.withAlpha (0.55f));
    drawSegment (midiSelectBtn,
                 processor.midiSelectsSlice.load() ? juce::Colour (0xFF081018) : juce::Colours::transparentBlack,
                 getTheme().moduleBorder.withAlpha (0.55f));

    if (processor.lazyChop.isActive())
    {
        lazyChopBtn.setButtonText ("STOP");
        lazyChopBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::red.brighter (0.2f));
        lazyChopBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::red.brighter (0.2f));
    }
    else
        lazyChopBtn.setButtonText ("LAZY");

    if (waveformView.isSliceDrawModeActive())
    {
        addSliceBtn.setColour (juce::TextButton::textColourOnId, activeText);
        addSliceBtn.setColour (juce::TextButton::textColourOffId, activeText);
    }

    if (autoChopPanel != nullptr)
    {
        splitBtn.setColour (juce::TextButton::textColourOnId, getTheme().tabGlobalActive);
        splitBtn.setColour (juce::TextButton::textColourOffId, getTheme().tabGlobalActive);
    }
}

void ActionPanel::updateMidiButtonAppearance (bool active)
{
    midiSelectBtn.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    if (active)
    {
        midiSelectBtn.setColour (juce::TextButton::textColourOnId,  getTheme().accent);
        midiSelectBtn.setColour (juce::TextButton::textColourOffId, getTheme().accent);
    }
    else
    {
        midiSelectBtn.setColour (juce::TextButton::textColourOnId,  juce::Colour (0xFF384858));
        midiSelectBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xFF384858));
    }
}

void ActionPanel::updateSnapButtonAppearance (bool active)
{
    snapBtn.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    if (active)
    {
        snapBtn.setColour (juce::TextButton::textColourOnId,  getTheme().accent);
        snapBtn.setColour (juce::TextButton::textColourOffId, getTheme().accent);
    }
    else
    {
        snapBtn.setColour (juce::TextButton::textColourOnId,  juce::Colour (0xFF384858));
        snapBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xFF384858));
    }
}
