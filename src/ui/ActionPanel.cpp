#include "ActionPanel.h"
#include "AutoChopPanel.h"
#include "IntersectLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"

ActionPanel::ActionPanel (IntersectProcessor& p, WaveformView& wv)
    : processor (p), waveformView (wv)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
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
    cmd.sliceIdx = processor.sliceManager.selectedSlice.load();
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
    repaint();
}

void ActionPanel::toggleFollowMidiSelection()
{
    const bool newState = ! processor.midiSelectsSlice.load();
    processor.midiSelectsSlice.store (newState);
    repaint();
}

void ActionPanel::triggerReseqMidi()
{
    if (reseqPanel != nullptr)
    {
        dismissReseqPanel();
        repaint();
        return;
    }

    if (processor.sliceManager.getNumSlices() < 2)
    {
        waveformView.showOverlayHint ("Need at least 2 slices to resequence.", 2200);
        return;
    }

    showReseqPanel();
    repaint();
}

void ActionPanel::dismissReseqPanel()
{
    if (reseqPanel != nullptr)
    {
        if (auto* parent = reseqPanel->getParentComponent())
            parent->removeChildComponent (reseqPanel.get());
        reseqPanel.reset();
    }
}

void ActionPanel::showReseqPanel()
{
    class ReseqPanel : public juce::Component
    {
    public:
        juce::Label label;
        juce::TextButton byPosBtn { "BY POSITION" };
        juce::TextButton byOrderBtn { "AS CREATED" };
        juce::TextButton cancelBtn { "CANCEL" };

        ReseqPanel()
        {
            const auto& theme = getTheme();

            addAndMakeVisible (label);
            label.setFont (IntersectLookAndFeel::makeFont (11.0f));
            label.setColour (juce::Label::textColourId, theme.text2.withAlpha (0.8f));
            label.setJustificationType (juce::Justification::centredLeft);
            label.setText ("Resequence MIDI notes from root note:", juce::dontSendNotification);

            for (auto* btn : { &byPosBtn, &byOrderBtn, &cancelBtn })
            {
                btn->setColour (juce::TextButton::buttonColourId, theme.surface4);
                btn->setColour (juce::TextButton::textColourOnId, theme.text2);
                btn->setColour (juce::TextButton::textColourOffId, theme.text2);
                addAndMakeVisible (btn);
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (getTheme().surface2.withAlpha (0.95f));
            g.fillRect (getLocalBounds());
            g.setColour (getTheme().surface5);
            g.drawRect (getLocalBounds(), 1);
        }

        void resized() override
        {
            int h = getHeight();
            int pad = 4;
            int btnH = h - pad * 2;
            int gap = 6;
            int cancelW = 60;

            int x = pad + 4;
            int labelW = 240;
            label.setBounds (x, pad, labelW, btnH);
            x += labelW + gap;

            byPosBtn.setBounds (x, pad, 120, btnH);
            x += 120 + gap;

            byOrderBtn.setBounds (x, pad, 110, btnH);

            cancelBtn.setBounds (getWidth() - cancelW - pad, pad, cancelW, btnH);
        }
    };

    auto reseq = std::make_unique<ReseqPanel>();

    reseq->byPosBtn.onClick = [this] {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdRepackMidi;
        cmd.intParam1 = 1;
        processor.pushCommand (cmd);
        dismissReseqPanel();
        waveformView.showOverlayHint ("MIDI notes resequenced by position.", 2200);
        repaint();
    };

    reseq->byOrderBtn.onClick = [this] {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdRepackMidi;
        cmd.intParam1 = 0;
        processor.pushCommand (cmd);
        dismissReseqPanel();
        waveformView.showOverlayHint ("MIDI notes resequenced as created.", 2200);
        repaint();
    };

    reseq->cancelBtn.onClick = [this] {
        dismissReseqPanel();
        repaint();
    };

    reseqPanel = std::move (reseq);

    if (auto* editor = waveformView.getParentComponent())
    {
        auto wfBounds = waveformView.getBoundsInParent();
        int panelH = 34;
        int panelX = wfBounds.getX();
        int panelW = wfBounds.getWidth();
        int panelY = wfBounds.getBottom() - panelH;
        reseqPanel->setBounds (panelX, panelY, panelW, panelH);
        editor->addAndMakeVisible (*reseqPanel);
    }
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
    items.clear();

    constexpr float kNarrowWidth = 42.0f;

    auto bounds = getLocalBounds();

    juce::FlexBox row;
    row.flexDirection = juce::FlexBox::Direction::row;
    row.flexWrap = juce::FlexBox::Wrap::noWrap;
    row.alignItems = juce::FlexBox::AlignItems::stretch;

    // ADD(0), LAZY(1), AUTO(2), COPY(3), DEL(4) get flex:1; ZX(5), FM(6), RESEQ(7) get fixed width
    row.items.add (juce::FlexItem().withFlex (1.0f));  // ADD
    row.items.add (juce::FlexItem().withFlex (1.0f));  // LAZY
    row.items.add (juce::FlexItem().withFlex (1.0f));  // AUTO
    row.items.add (juce::FlexItem().withFlex (1.0f));  // COPY
    row.items.add (juce::FlexItem().withFlex (1.0f));  // DEL
    row.items.add (juce::FlexItem().withWidth (kNarrowWidth));  // ZX
    row.items.add (juce::FlexItem().withWidth (kNarrowWidth));  // FM
    row.items.add (juce::FlexItem().withWidth (52.0f));         // RESEQ

    row.performLayout (bounds.toFloat());

    struct ItemDef { juce::String text; int id; bool narrow; };
    const ItemDef defs[] = {
        { "ADD",   0, false },
        { "LAZY",  1, false },
        { "AUTO",  2, false },
        { "COPY",  3, false },
        { "DEL",   4, false },
        { "ZX",    5, true  },
        { "FM",    6, true  },
        { "RESEQ", 7, true  },
    };

    for (int i = 0; i < 8; ++i)
    {
        ActionItem item;
        item.text = defs[i].text;
        item.bounds = row.items[i].currentBounds.getSmallestIntegerContainer();
        item.id = defs[i].id;
        item.isNarrow = defs[i].narrow;
        items.push_back (item);
    }
}

void ActionPanel::paint (juce::Graphics& g)
{
    const auto& theme = getTheme();
    g.fillAll (theme.surface2);

    const bool lazyActive = processor.lazyChop.isActive();
    const bool addActive = waveformView.isSliceDrawModeActive();

    // Panels remove themselves from parent on cancel/apply;
    // detect orphaned panels and clean up the unique_ptrs.
    if (autoChopPanel != nullptr && autoChopPanel->getParentComponent() == nullptr)
        autoChopPanel.reset();
    if (reseqPanel != nullptr && reseqPanel->getParentComponent() == nullptr)
        reseqPanel.reset();

    const bool autoActive = autoChopPanel != nullptr;
    const bool reseqActive = reseqPanel != nullptr;
    const bool snapActive = processor.snapToZeroCrossing.load();
    const bool fmActive = processor.midiSelectsSlice.load();

    const auto inactiveText = theme.text0;
    const auto activeText = theme.accent;
    const auto activeBg = theme.surface4;
    const auto hoverText = theme.text1;
    const auto hoverBg = theme.surface5;

    auto font = IntersectLookAndFeel::makeFont (11.0f);
    g.setFont (font);

    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& item = items[(size_t) i];

        bool isActive = false;
        juce::String displayText = item.text;

        switch (item.id)
        {
            case 0: isActive = addActive; break;
            case 1:
                isActive = lazyActive;
                if (lazyActive) displayText = "STOP";
                break;
            case 2: isActive = autoActive; break;
            case 5: isActive = snapActive; break;
            case 6: isActive = fmActive; break;
            case 7: isActive = reseqActive; break;
            default: break;
        }

        const bool isHovered = (i == hoveredIndex);

        // Background
        if (isActive)
        {
            g.setColour (activeBg);
            g.fillRect (item.bounds);
        }
        else if (isHovered)
        {
            g.setColour (hoverBg);
            g.fillRect (item.bounds);
        }

        // Text
        if (isActive)
            g.setColour (activeText);
        else if (isHovered)
            g.setColour (hoverText);
        else
            g.setColour (inactiveText);

        g.drawFittedText (displayText, item.bounds, juce::Justification::centred, 1);
    }

    // Pass 2: Draw borders on top so backgrounds never overwrite them
    g.setColour (theme.surface3);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    for (int i = 0; i < (int) items.size() - 1; ++i)
    {
        const auto& item = items[(size_t) i];
        g.fillRect (item.bounds.getRight() - 1, item.bounds.getY(),
                    1, item.bounds.getHeight());
    }
}

int ActionPanel::hitTestItem (juce::Point<int> pos) const
{
    for (int i = 0; i < (int) items.size(); ++i)
    {
        if (items[(size_t) i].bounds.contains (pos))
            return i;
    }
    return -1;
}

void ActionPanel::mouseDown (const juce::MouseEvent& e)
{
    int idx = hitTestItem (e.getPosition());
    if (idx < 0)
        return;

    switch (items[(size_t) idx].id)
    {
        case 0: triggerAddSliceMode(); break;
        case 1: triggerLazyChop(); break;
        case 2: triggerAutoChop(); break;
        case 3: triggerDuplicateSlice(); break;
        case 4: triggerDeleteSelectedSlice(); break;
        case 5: toggleSnapToZeroCrossing(); break;
        case 6: toggleFollowMidiSelection(); break;
        case 7: triggerReseqMidi(); break;
        default: break;
    }
}

void ActionPanel::mouseMove (const juce::MouseEvent& e)
{
    int idx = hitTestItem (e.getPosition());
    if (idx != hoveredIndex)
    {
        hoveredIndex = idx;
        repaint();
    }
}

void ActionPanel::mouseExit (const juce::MouseEvent&)
{
    if (hoveredIndex != -1)
    {
        hoveredIndex = -1;
        repaint();
    }
}
