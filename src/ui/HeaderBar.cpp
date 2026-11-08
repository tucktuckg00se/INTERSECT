#include "HeaderBar.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"

namespace
{
juce::String firstMissingSampleName (const IntersectProcessor::UiSliceSnapshot& ui)
{
    for (int i = 0; i < ui.numSessionSamples; ++i)
        if (ui.sessionSamples[(size_t) i].isPlaceholder())
            return ui.sessionSamples[(size_t) i].fileName.toString();
    return {};
}

float measureTextWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getWidth();
}
}

HeaderBar::HeaderBar (IntersectProcessor& p) : processor (p)
{
    for (auto* btn : { &browserBtn, &saveBtn, &undoBtn, &redoBtn, &panicBtn, &settingsBtn })
    {
        addAndMakeVisible (*btn);
        btn->setAlwaysOnTop (true);
        btn->getProperties().set (IntersectLookAndFeel::outlineOnlyButtonProperty, true);
        btn->setColour (juce::TextButton::buttonColourId, getTheme().surface4);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().text2);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().text2);
    }

    panicBtn.setTooltip ("Panic: kill all sound");
    panicBtn.onClick = [this]
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdPanic;
        processor.pushCommand (cmd);
    };

    undoBtn.setTooltip ("Undo (Ctrl+Z)");
    redoBtn.setTooltip ("Redo (Ctrl+Shift+Z)");
    browserBtn.setTooltip ("Open the file browser");
    settingsBtn.setTooltip ("Settings");

    browserBtn.onClick = [this]
    {
        if (onBrowserToggle != nullptr)
            onBrowserToggle();
    };

    saveBtn.setTooltip ("Save the kit to your preset library");
    saveBtn.onClick = [this]
    {
        if (onSaveRequested != nullptr)
            onSaveRequested();
    };

    undoBtn.onClick = [this]
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdUndo;
        processor.pushCommand (cmd);
    };

    redoBtn.onClick = [this]
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdRedo;
        processor.pushCommand (cmd);
    };

    settingsBtn.onClick = [this]
    {
        if (onSettingsToggle != nullptr)
            onSettingsToggle();
    };
}

juce::String HeaderBar::getTooltip()
{
    if (! sampleInfoBounds.contains (getMouseXYRelative()))
        return {};

    const auto& ui = processor.getUiSliceSnapshot();
    if (ui.hasStatusMessage && ui.statusIsWarning)
        return "Copy message";
    if (ui.numMissingSessionSamples > 0)
        return "Relink missing sample";

    return {};
}

void HeaderBar::resized()
{
    auto area = getLocalBounds().reduced (16, 0);
    const int contentHeight = 20;
    const int buttonHeight = 16;
    const int buttonGap = 2;
    const auto buttonFont = IntersectLookAndFeel::makeFont (9.0f);

    auto buttonWidth = [&] (const juce::String& text, int minWidth)
    {
        return juce::jmax (minWidth, juce::roundToInt (measureTextWidth (buttonFont, text)) + 18);
    };

    const int setW = buttonWidth (settingsBtn.getButtonText(), 38);
    const int panicW = buttonWidth (panicBtn.getButtonText(), 50);
    const int redoW = buttonWidth (redoBtn.getButtonText(), 44);
    const int undoW = buttonWidth (undoBtn.getButtonText(), 44);
    const int browserW = buttonWidth (browserBtn.getButtonText(), 46);
    const int saveW = buttonWidth (saveBtn.getButtonText(), 44);
    const int rightStripW = undoW + redoW + panicW + setW + buttonGap * 3;
    const auto centredY = area.getY() + (area.getHeight() - buttonHeight) / 2;

    browserBtn.setBounds (area.getX(), centredY, browserW, buttonHeight);
    saveBtn.setBounds (browserBtn.getRight() + buttonGap, centredY, saveW, buttonHeight);

    auto buttonArea = area.removeFromRight (rightStripW);
    juce::FlexBox buttons;
    buttons.flexDirection = juce::FlexBox::Direction::row;
    buttons.flexWrap = juce::FlexBox::Wrap::noWrap;
    buttons.alignItems = juce::FlexBox::AlignItems::center;
    buttons.items.add (juce::FlexItem (undoBtn).withWidth ((float) undoW).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem().withWidth ((float) buttonGap).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem (redoBtn).withWidth ((float) redoW).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem().withWidth ((float) buttonGap).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem (panicBtn).withWidth ((float) panicW).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem().withWidth ((float) buttonGap).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem (settingsBtn).withWidth ((float) setW).withHeight ((float) buttonHeight));
    buttons.performLayout (buttonArea.toFloat());

    const int leftGuard = browserW + buttonGap + saveW + 12;
    const int rightGuard = rightStripW + 12;
    sampleInfoBounds = getLocalBounds().reduced (juce::jmax (leftGuard, rightGuard), 0)
                                     .withHeight (contentHeight)
                                     .withCentre ({ getWidth() / 2, getHeight() / 2 });
}

void HeaderBar::paint (juce::Graphics& g)
{
    for (auto* btn : { &browserBtn, &saveBtn, &undoBtn, &redoBtn, &panicBtn, &settingsBtn })
    {
        auto text = getTheme().text2.withAlpha (btn->isMouseOverOrDragging() ? 1.0f : 0.88f);
        btn->setColour (juce::TextButton::buttonColourId,
                        (btn->isMouseOverOrDragging() ? getTheme().surface5 : getTheme().surface4).withAlpha (0.94f));
        btn->setColour (juce::TextButton::textColourOnId, btn->getToggleState() ? getTheme().accent : text);
        btn->setColour (juce::TextButton::textColourOffId, text);
    }

    g.fillAll (getTheme().surface2);
    g.setColour (getTheme().surface3.withAlpha (0.8f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    const auto& ui = processor.getUiSliceSnapshot();

    juce::String fileText;
    if (ui.hasStatusMessage)
    {
        fileText = ui.statusMessage.toString();
        g.setColour (ui.statusIsWarning ? getTheme().color5.brighter (0.1f)
                                        : getTheme().text1);
    }
    else if (ui.numMissingSessionSamples > 0)
    {
        fileText = ui.numMissingSessionSamples > 1
            ? juce::String (ui.numMissingSessionSamples) + " SAMPLES MISSING - CLICK TO RELINK"
            : "MISSING: " + firstMissingSampleName (ui) + " - CLICK TO RELINK";
        g.setColour (getTheme().color5.brighter (0.1f));
    }
    else
    {
        fileText = "INTERSECT";
        g.setColour (getTheme().text2.withAlpha (0.52f));
    }

    g.setFont (IntersectLookAndFeel::fitFontToWidth (fileText, 11.0f, 9.0f, sampleInfoBounds.getWidth(), false));
    g.drawText (fileText, sampleInfoBounds, juce::Justification::centred);
}

void HeaderBar::mouseDown (const juce::MouseEvent& e)
{
    if (sampleInfoBounds.contains (e.getPosition()))
    {
        const auto& ui = processor.getUiSliceSnapshot();
        if (ui.hasStatusMessage && ui.statusIsWarning)
        {
            // A failed stem job adds versions, GPU and model details, so one paste says what ran where.
            auto text = ui.statusMessage.toString();
            if (const auto diagnostics = processor.getStemFailureDiagnostics (text); diagnostics.isNotEmpty())
                text << "\n\n" << diagnostics;
            juce::SystemClipboard::copyTextToClipboard (text);
        }
        else if (onRelinkRequested != nullptr)
        {
            for (int i = 0; i < ui.numSessionSamples; ++i)
            {
                if (ui.sessionSamples[(size_t) i].isPlaceholder())
                {
                    onRelinkRequested (ui.sessionSamples[(size_t) i].sampleId);
                    break;
                }
            }
        }
    }
}

void HeaderBar::setBrowserActive (bool isActive)
{
    browserBtn.setToggleState (isActive, juce::dontSendNotification);
    browserBtn.setTooltip (isActive ? "Close the file browser (Esc)" : "Open the file browser");
    repaint();
}

void HeaderBar::setSettingsActive (bool isActive)
{
    settingsBtn.setToggleState (isActive, juce::dontSendNotification);
    settingsBtn.setTooltip (isActive ? "Close settings (Esc)" : "Settings");
    repaint();
}
