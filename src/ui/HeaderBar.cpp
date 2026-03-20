#include "HeaderBar.h"
#include "IntersectLookAndFeel.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

namespace
{
float measureTextWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getWidth();
}
}

HeaderBar::HeaderBar (IntersectProcessor& p) : processor (p)
{
    for (auto* btn : { &undoBtn, &redoBtn, &panicBtn, &loadBtn, &themeBtn })
    {
        addAndMakeVisible (*btn);
        btn->setAlwaysOnTop (true);
        btn->setColour (juce::TextButton::buttonColourId, getTheme().button);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().foreground);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().foreground);
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
    loadBtn.setTooltip ("Load sample");

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

    loadBtn.onClick = [this] { openFileBrowser(); };
    themeBtn.onClick = [this] { showThemePopup(); };
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

    const int setW = buttonWidth (themeBtn.getButtonText(), 38);
    const int loadW = buttonWidth (loadBtn.getButtonText(), 42);
    const int panicW = buttonWidth (panicBtn.getButtonText(), 50);
    const int redoW = buttonWidth (redoBtn.getButtonText(), 44);
    const int undoW = buttonWidth (undoBtn.getButtonText(), 44);
    const int buttonStripW = undoW + redoW + panicW + loadW + setW + buttonGap * 4;

    juce::FlexBox row;
    row.flexDirection = juce::FlexBox::Direction::row;
    row.flexWrap = juce::FlexBox::Wrap::noWrap;
    row.alignItems = juce::FlexBox::AlignItems::center;

    // File | sep area (12+1+12) | SLICES | 12px gap | ROOT | flex spacer | buttons
    row.items.add (juce::FlexItem().withWidth (200.0f).withMinWidth (80.0f).withHeight ((float) contentHeight));
    row.items.add (juce::FlexItem().withWidth (25.0f).withHeight ((float) contentHeight));   // sep: 12+1+12
    row.items.add (juce::FlexItem().withWidth (58.0f).withHeight ((float) contentHeight));   // SLICES
    row.items.add (juce::FlexItem().withWidth (12.0f).withHeight ((float) contentHeight));   // gap
    row.items.add (juce::FlexItem().withWidth (42.0f).withHeight ((float) contentHeight));   // ROOT
    row.items.add (juce::FlexItem().withFlex (1.0f).withMinWidth (4.0f).withHeight ((float) contentHeight));
    row.items.add (juce::FlexItem().withWidth ((float) buttonStripW).withHeight ((float) contentHeight));
    row.performLayout (area.toFloat());

    sampleInfoBounds = row.items[0].currentBounds.getSmallestIntegerContainer();
    separatorBounds = row.items[1].currentBounds.getSmallestIntegerContainer();
    slicesBounds = row.items[2].currentBounds.getSmallestIntegerContainer();
    rootBounds = row.items[4].currentBounds.getSmallestIntegerContainer();

    const auto buttonArea = row.items[6].currentBounds.getSmallestIntegerContainer();
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
    buttons.items.add (juce::FlexItem (loadBtn).withWidth ((float) loadW).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem().withWidth ((float) buttonGap).withHeight ((float) buttonHeight));
    buttons.items.add (juce::FlexItem (themeBtn).withWidth ((float) setW).withHeight ((float) buttonHeight));
    buttons.performLayout (buttonArea.toFloat());
}

void HeaderBar::paint (juce::Graphics& g)
{
    for (auto* btn : { &undoBtn, &redoBtn, &panicBtn, &loadBtn, &themeBtn })
    {
        auto text = getTheme().foreground.withAlpha (btn->isMouseOverOrDragging() ? 1.0f : 0.88f);
        btn->setColour (juce::TextButton::buttonColourId,
                        (btn->isMouseOverOrDragging() ? getTheme().buttonHover : getTheme().button).withAlpha (0.94f));
        btn->setColour (juce::TextButton::textColourOnId, text);
        btn->setColour (juce::TextButton::textColourOffId, text);
    }

    g.fillAll (getTheme().header);
    g.setColour (getTheme().moduleBorder.withAlpha (0.8f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    const auto& ui = processor.getUiSliceSnapshot();
    const int rowY = (getHeight() - 26) / 2;
    const int rowH = 26;

    auto drawMetric = [&] (const juce::Rectangle<int>& bounds, const juce::String& label,
                           const juce::String& value, juce::Colour valueColour)
    {
        auto labelFont = IntersectLookAndFeel::makeFont (8.0f, true);
        auto valueFont = IntersectLookAndFeel::fitFontToWidth (value, 11.0f, 9.5f,
                                                               bounds.getWidth() - 14, false);
        const int labelW = juce::roundToInt (measureTextWidth (labelFont, label)) + 1;
        const int pairY = bounds.getY() + (bounds.getHeight() - 12) / 2;

        g.setFont (labelFont);
        g.setColour (juce::Colour (0xFF384050));
        g.drawText (label, bounds.getX(), pairY, labelW, 12,
                    juce::Justification::centredLeft);

        g.setFont (valueFont);
        g.setColour (valueColour);
        g.drawText (value, bounds.getX() + labelW + 3, pairY, bounds.getWidth() - labelW - 3, 12,
                    juce::Justification::centredLeft);
    };

    juce::String fileText;
    if (ui.sampleMissing)
    {
        fileText = "MISSING: " + ui.sampleFileName + "  CLICK TO RELINK";
        g.setColour (juce::Colours::orange.brighter (0.1f));
    }
    else if (ui.sampleLoaded)
    {
        double srate = processor.getSampleRate();
        if (srate <= 0.0)
            srate = 44100.0;
        const double lenSec = ui.sampleNumFrames / srate;
        fileText = ui.sampleFileName + " (" + juce::String (lenSec, 2) + "s)";
        g.setColour (juce::Colour (0xFF505868));
    }
    else
    {
        fileText = "load or drop a sample";
        g.setColour (getTheme().foreground.withAlpha (0.52f));
    }

    g.setFont (IntersectLookAndFeel::fitFontToWidth (fileText, 11.0f, 9.0f, sampleInfoBounds.getWidth(), false));
    g.drawText (fileText, sampleInfoBounds, juce::Justification::centredLeft);

    const bool rootEditable = (ui.numSlices == 0);
    drawMetric (slicesBounds, "SLICES", juce::String (ui.numSlices), juce::Colour (0xFF607080));
    drawMetric (rootBounds,
                "ROOT",
                juce::String (ui.rootNote),
                (rootEditable ? juce::Colour (0xFF607080) : getTheme().foreground.withAlpha (0.6f)));

    g.setColour (juce::Colour (0xFF1A1E28));
    const int sepX = separatorBounds.getX() + separatorBounds.getWidth() / 2;
    const int centerY = rowY + rowH / 2;
    g.fillRect (sepX, centerY - 7, 1, 14);
}

void HeaderBar::mouseDown (const juce::MouseEvent& e)
{
    draggingRoot = false;

    const auto& ui = processor.getUiSliceSnapshot();
    if (ui.numSlices == 0 && rootBounds.contains (e.getPosition()))
    {
        draggingRoot = true;
        dragStartY = e.y;
        dragStartValue = (float) ui.rootNote;
        return;
    }

    if (sampleInfoBounds.contains (e.getPosition()))
    {
        if (ui.sampleMissing)
            openRelinkBrowser();
        else
            openFileBrowser();
    }
}

void HeaderBar::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingRoot)
        return;

    const float deltaY = (float) (dragStartY - e.y);
    const int newVal = juce::jlimit (0, 127, (int) (dragStartValue + deltaY * (127.0f / 200.0f)));
    IntersectProcessor::Command cmd;
    cmd.type = IntersectProcessor::CmdSetRootNote;
    cmd.intParam1 = newVal;
    processor.pushCommand (cmd);
}

void HeaderBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto& ui = processor.getUiSliceSnapshot();
    if (ui.numSlices == 0 && rootBounds.contains (e.getPosition()))
        showRootEditor();
}

void HeaderBar::showRootEditor()
{
    const auto& ui = processor.getUiSliceSnapshot();
    auto editorBounds = rootBounds.reduced (0, 4);

    textEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*textEditor);
    textEditor->setBounds (editorBounds);
    textEditor->setFont (IntersectLookAndFeel::makeFont (11.0f));
    textEditor->setColour (juce::TextEditor::backgroundColourId, getTheme().darkBar.brighter (0.15f));
    textEditor->setColour (juce::TextEditor::textColourId, getTheme().foreground);
    textEditor->setColour (juce::TextEditor::outlineColourId, getTheme().accent);
    textEditor->setText (juce::String (ui.rootNote), false);
    textEditor->selectAll();
    textEditor->grabKeyboardFocus();

    juce::Component::SafePointer<HeaderBar> safeThis (this);
    textEditor->onReturnKey = [safeThis]
    {
        if (safeThis == nullptr || safeThis->textEditor == nullptr)
            return;
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdSetRootNote;
        cmd.intParam1 = juce::jlimit (0, 127, safeThis->textEditor->getText().getIntValue());
        safeThis->processor.pushCommand (cmd);
        safeThis->textEditor->onFocusLost = nullptr;
        safeThis->textEditor.reset();
        safeThis->repaint();
    };
    textEditor->onEscapeKey = [safeThis]
    {
        if (safeThis == nullptr || safeThis->textEditor == nullptr)
            return;
        safeThis->textEditor->onFocusLost = nullptr;
        safeThis->textEditor.reset();
        safeThis->repaint();
    };
    textEditor->onFocusLost = [safeThis]
    {
        if (safeThis == nullptr || safeThis->textEditor == nullptr)
            return;
        safeThis->textEditor->onFocusLost = nullptr;
        safeThis->textEditor.reset();
        safeThis->repaint();
    };
}

void HeaderBar::adjustScale (float delta)
{
    if (auto* p = processor.apvts.getParameter (ParamIds::uiScale))
    {
        float current = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
        float newScale = juce::jlimit (0.5f, 3.0f, current + delta);
        p->setValueNotifyingHost (p->convertTo0to1 (newScale));
    }
}

void HeaderBar::showThemePopup()
{
    auto* editor = dynamic_cast<IntersectEditor*> (getParentComponent());
    if (editor == nullptr)
        return;

    auto themes = editor->getAvailableThemes();
    auto currentName = getTheme().name;

    const bool nrpnEnabled = processor.midiEditState.enabled.load (std::memory_order_relaxed);
    const bool blockCc     = processor.midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed);
    const int  nrpnCh      = processor.midiEditState.channel.load (std::memory_order_relaxed);
    const juce::String chStr = (nrpnCh == 0) ? "OMNI" : juce::String (nrpnCh);

    juce::PopupMenu menu;
    menu.addSectionHeader ("INTERSECT  v" + juce::String (JucePlugin_VersionString));
    menu.addSeparator();

    float currentScale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    menu.addSectionHeader ("Scale  " + juce::String (currentScale, 2) + "x");
    menu.addItem (100, "- 0.25");
    menu.addItem (101, "+ 0.25");
    menu.addSeparator();

    menu.addSectionHeader ("NRPN");
    menu.addItem (200, "Enable", true, nrpnEnabled);
    menu.addItem (201, "Consume CCs", true, blockCc);
    menu.addSectionHeader ("MIDI Channel: " + chStr);
    menu.addItem (202, "Channel  -");
    menu.addItem (203, "Channel  +");
    menu.addSeparator();

    menu.addSectionHeader ("Theme");
    for (int i = 0; i < themes.size(); ++i)
        menu.addItem (i + 1, themes[i], true, themes[i] == currentName);

    auto* topLevel = getTopLevelComponent();
    float ms = IntersectLookAndFeel::getMenuScale();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&themeBtn)
                            .withParentComponent (topLevel)
                            .withStandardItemHeight ((int) (24 * ms)),
        [this, editor, themes] (int result)
        {
            float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();

            if (result == 100)
                adjustScale (-0.25f);
            else if (result == 101)
                adjustScale (0.25f);
            else if (result == 200)
            {
                const bool current = processor.midiEditState.enabled.load (std::memory_order_relaxed);
                processor.midiEditState.enabled.store (! current, std::memory_order_release);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == 201)
            {
                const bool current = processor.midiEditState.consumeMidiEditCc.load (std::memory_order_relaxed);
                processor.midiEditState.consumeMidiEditCc.store (! current, std::memory_order_relaxed);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == 202)
            {
                const int ch = processor.midiEditState.channel.load (std::memory_order_relaxed);
                processor.midiEditState.channel.store (juce::jlimit (0, 16, ch - 1), std::memory_order_relaxed);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result == 203)
            {
                const int ch = processor.midiEditState.channel.load (std::memory_order_relaxed);
                processor.midiEditState.channel.store (juce::jlimit (0, 16, ch + 1), std::memory_order_relaxed);
                editor->saveUserSettings (scale, getTheme().name);
            }
            else if (result > 0 && result <= themes.size())
            {
                editor->applyTheme (themes[result - 1]);
            }
        });
}

void HeaderBar::openFileBrowser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load Audio File",
        juce::File(),
        "*.wav;*.ogg;*.aiff;*.aif;*.flac;*.mp3");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile())
            {
                processor.loadFileAsync (result);
                processor.zoom.store (1.0f);
                processor.scroll.store (0.0f);
            }
        });
}

void HeaderBar::openRelinkBrowser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Relink Audio File",
        juce::File(),
        "*.wav;*.ogg;*.aiff;*.aif;*.flac;*.mp3");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile())
                processor.relinkFileAsync (result);
        });
}
