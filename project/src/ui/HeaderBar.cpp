#include "HeaderBar.h"
#include "TuckersLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../audio/WsolaEngine.h"
#include <cmath>

HeaderBar::HeaderBar (IntersectProcessor& p) : processor (p)
{
    addAndMakeVisible (scaleDownBtn);
    addAndMakeVisible (scaleUpBtn);
    scaleDownBtn.onClick = [this] { adjustScale (-0.25f); };
    scaleUpBtn.onClick   = [this] { adjustScale (0.25f); };
}

void HeaderBar::resized()
{
    int btnW = 20;
    int btnH = 16;
    scaleDownBtn.setBounds (getWidth() - btnW * 2 - 6, 28, btnW, btnH);
    scaleUpBtn.setBounds   (getWidth() - btnW - 4, 28, btnW, btnH);
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

void HeaderBar::paint (juce::Graphics& g)
{
    g.fillAll (Theme::tealHeader);
    headerCells.clear();

    if (processor.sampleData.isLoaded())
    {
        // --- Row 1: Title + BPM / PITCH / ALGO / SET BPM ---
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (14.0f).boldened());
        g.drawText ("SAMPLE A", 8, 4, 70, 16, juce::Justification::centredLeft);

        g.setFont (juce::Font (11.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));

        int x = 90;
        int row1y = 2;
        int row1h = 22;

        // BPM
        g.setFont (juce::Font (9.0f));
        g.drawText ("BPM", x, row1y, 50, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (12.0f));
        float bpm = processor.apvts.getRawParameterValue (ParamIds::defaultBpm)->load();
        g.drawText (juce::String ((int) bpm), x, row1y + 10, 50, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row1y, 55, row1h, ParamIds::defaultBpm, 20.0f, 999.0f, 1.0f, false, false, false, false });
        x += 55;

        // PITCH — may be read-only in Repitch+Stretch mode
        {
            bool stretchOn = processor.apvts.getRawParameterValue (ParamIds::defaultStretchEnabled)->load() > 0.5f;
            int algo = (int) processor.apvts.getRawParameterValue (ParamIds::defaultAlgorithm)->load();
            bool pitchReadOnly = (algo == 0 && stretchOn);

            g.setFont (juce::Font (9.0f));
            g.setColour (pitchReadOnly ? Theme::foreground.withAlpha (0.5f) : Theme::foreground.withAlpha (0.9f));
            g.drawText ("PITCH", x, row1y, 60, 10, juce::Justification::centredLeft);
            g.setFont (juce::Font (12.0f));

            if (pitchReadOnly)
            {
                // Show calculated pitch from BPM ratio
                float dawBpm = processor.dawBpm.load();
                float calcPitch = (dawBpm > 0.0f && bpm > 0.0f)
                    ? 12.0f * std::log2 (dawBpm / bpm) : 0.0f;
                juce::String pitchStr = (calcPitch >= 0 ? "+" : "") + juce::String (calcPitch, 1) + "st";
                g.setColour (Theme::foreground.withAlpha (0.5f));
                g.drawText (pitchStr, x, row1y + 10, 60, 12, juce::Justification::centredLeft);
                headerCells.push_back ({ x, row1y, 60, row1h, ParamIds::defaultPitch, -24.0f, 24.0f, 0.1f, false, false, true, false });
            }
            else
            {
                float pitch = processor.apvts.getRawParameterValue (ParamIds::defaultPitch)->load();
                g.setColour (Theme::foreground.withAlpha (0.9f));
                g.drawText (juce::String (pitch, 1), x, row1y + 10, 60, 12, juce::Justification::centredLeft);
                headerCells.push_back ({ x, row1y, 60, row1h, ParamIds::defaultPitch, -24.0f, 24.0f, 0.1f, false, false, false, false });
            }
            x += 60;
        }

        // ALGORITHM
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("ALGO", x, row1y, 60, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (12.0f));
        int algo = (int) processor.apvts.getRawParameterValue (ParamIds::defaultAlgorithm)->load();
        g.drawText (algo == 0 ? "Repitch" : "Stretch", x, row1y + 10, 60, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row1y, 60, row1h, ParamIds::defaultAlgorithm, 0.0f, 1.0f, 1.0f, true, false, false, false });
        x += 65;

        // SET BPM (sample-level)
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::accent);
        g.drawText ("SET BPM", x, row1y + 4, 50, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row1y, 50, row1h, juce::String(), 0.0f, 0.0f, 0.0f, false, false, false, true });
        x += 55;

        // Slice count (right side of row 1)
        g.setFont (juce::Font (10.0f));
        g.setColour (Theme::foreground.withAlpha (0.5f));
        g.drawText ("Slices: " + juce::String (processor.sliceManager.getNumSlices()),
                     getWidth() - 90, row1y + 6, 80, 14, juce::Justification::centredRight);

        // --- Separator line between rows ---
        g.setColour (Theme::separator);
        g.drawHorizontalLine (24, 8.0f, (float) getWidth() - 8.0f);

        // --- Row 2: ATK / DEC / SUS / REL / PP / STRETCH ---
        int row2y = 26;
        int row2h = 22;
        x = 8;

        // ATK
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("ATK", x, row2y, 50, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (11.0f));
        float atk = processor.apvts.getRawParameterValue (ParamIds::defaultAttack)->load();
        g.drawText (juce::String ((int) atk) + "ms", x, row2y + 10, 50, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, 50, row2h, ParamIds::defaultAttack, 0.0f, 1000.0f, 1.0f, false, false, false, false });
        x += 55;

        // DEC
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("DEC", x, row2y, 55, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (11.0f));
        float dec = processor.apvts.getRawParameterValue (ParamIds::defaultDecay)->load();
        g.drawText (juce::String ((int) dec) + "ms", x, row2y + 10, 55, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, 55, row2h, ParamIds::defaultDecay, 0.0f, 5000.0f, 1.0f, false, false, false, false });
        x += 60;

        // SUS
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("SUS", x, row2y, 50, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (11.0f));
        float sus = processor.apvts.getRawParameterValue (ParamIds::defaultSustain)->load();
        g.drawText (juce::String ((int) sus) + "%", x, row2y + 10, 50, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, 50, row2h, ParamIds::defaultSustain, 0.0f, 100.0f, 1.0f, false, false, false, false });
        x += 55;

        // REL
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("REL", x, row2y, 55, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (11.0f));
        float rel = processor.apvts.getRawParameterValue (ParamIds::defaultRelease)->load();
        g.drawText (juce::String ((int) rel) + "ms", x, row2y + 10, 55, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, 55, row2h, ParamIds::defaultRelease, 0.0f, 5000.0f, 1.0f, false, false, false, false });
        x += 60;

        // PP
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("PP", x, row2y, 40, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (11.0f));
        bool pp = processor.apvts.getRawParameterValue (ParamIds::defaultPingPong)->load() > 0.5f;
        g.setColour (pp ? Theme::lockGold : Theme::foreground.withAlpha (0.5f));
        g.drawText (pp ? "ON" : "OFF", x, row2y + 10, 40, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, 40, row2h, ParamIds::defaultPingPong, 0.0f, 1.0f, 1.0f, false, true, false, false });
        x += 45;

        // MUTE GROUP
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("MUTE", x, row2y, 45, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (11.0f));
        float muteGrp = processor.apvts.getRawParameterValue (ParamIds::defaultMuteGroup)->load();
        g.drawText (juce::String ((int) muteGrp), x, row2y + 10, 45, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, 45, row2h, ParamIds::defaultMuteGroup, 0.0f, 32.0f, 1.0f, false, false, false, false });
        x += 50;

        // STRETCH toggle
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("STRETCH", x, row2y, 55, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (11.0f));
        bool strOn = processor.apvts.getRawParameterValue (ParamIds::defaultStretchEnabled)->load() > 0.5f;
        g.setColour (strOn ? Theme::accent : Theme::foreground.withAlpha (0.5f));
        g.drawText (strOn ? "ON" : "OFF", x, row2y + 10, 55, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, 55, row2h, ParamIds::defaultStretchEnabled, 0.0f, 1.0f, 1.0f, false, true, false, false });
        x += 60;

        // TONAL
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("TONAL", x, row2y, 55, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (11.0f));
        float tonal = processor.apvts.getRawParameterValue (ParamIds::defaultTonality)->load();
        g.drawText (juce::String ((int) tonal) + "Hz", x, row2y + 10, 55, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, 55, row2h, ParamIds::defaultTonality, 0.0f, 8000.0f, 100.0f, false, false, false, false });
        x += 60;

        // FMNT
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("FMNT", x, row2y, 55, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (11.0f));
        float fmnt = processor.apvts.getRawParameterValue (ParamIds::defaultFormant)->load();
        g.drawText ((fmnt >= 0 ? "+" : "") + juce::String (fmnt, 1), x, row2y + 10, 55, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, 55, row2h, ParamIds::defaultFormant, -24.0f, 24.0f, 0.1f, false, false, false, false });
        x += 60;

        // FMNT C
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::foreground.withAlpha (0.9f));
        g.drawText ("FMNT C", x, row2y, 50, 10, juce::Justification::centredLeft);
        g.setFont (juce::Font (11.0f));
        bool fmntC = processor.apvts.getRawParameterValue (ParamIds::defaultFormantComp)->load() > 0.5f;
        g.setColour (fmntC ? Theme::lockGold : Theme::foreground.withAlpha (0.5f));
        g.drawText (fmntC ? "ON" : "OFF", x, row2y + 10, 50, 12, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, 50, row2h, ParamIds::defaultFormantComp, 0.0f, 1.0f, 1.0f, false, true, false, false });
        x += 55;
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::Font (14.0f).boldened());
        g.drawText ("INTERSECT", 8, 16, 140, 16, juce::Justification::centredLeft);

        g.setFont (juce::Font (11.0f));
        g.setColour (Theme::foreground.withAlpha (0.6f));
        g.drawText ("DROP AUDIO FILE", 160, 18, 300, 14,
                     juce::Justification::centredLeft);
    }
}

void HeaderBar::mouseDown (const juce::MouseEvent& e)
{
    if (textEditor != nullptr)
        textEditor.reset();

    auto pos = e.getPosition();

    for (int i = 0; i < (int) headerCells.size(); ++i)
    {
        const auto& cell = headerCells[(size_t) i];
        if (pos.x >= cell.x && pos.x < cell.x + cell.w &&
            pos.y >= cell.y && pos.y < cell.y + cell.h)
        {
            // SET BPM button
            if (cell.isSetBpm)
            {
                showSetBpmPopup (true);
                return;
            }

            // Read-only cells (e.g. repitch pitch display)
            if (cell.isReadOnly)
                return;

            // Boolean toggle (ping-pong, stretch)
            if (cell.isBoolean)
            {
                if (auto* p = processor.apvts.getParameter (cell.paramId))
                {
                    float current = p->getValue();
                    p->setValueNotifyingHost (current > 0.5f ? 0.0f : 1.0f);
                }
                repaint();
                return;
            }

            // Algorithm choice popup
            if (cell.isChoice)
            {
                juce::PopupMenu menu;
                menu.addItem (1, "Repitch");
                menu.addItem (2, "Stretch");
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                    [this, paramId = cell.paramId] (int result) {
                        if (result > 0)
                        {
                            if (auto* p = processor.apvts.getParameter (paramId))
                                p->setValueNotifyingHost (p->convertTo0to1 ((float) (result - 1)));
                            repaint();
                        }
                    });
                return;
            }

            // Set up drag for numeric params
            activeDragCell = i;
            dragStartY = pos.y;
            dragStartValue = processor.apvts.getRawParameterValue (cell.paramId)->load();
            return;
        }
    }
}

void HeaderBar::mouseDrag (const juce::MouseEvent& e)
{
    if (activeDragCell < 0 || activeDragCell >= (int) headerCells.size())
        return;

    const auto& cell = headerCells[(size_t) activeDragCell];
    if (cell.isReadOnly || cell.isSetBpm)
        return;

    float deltaY = (float) (dragStartY - e.y);  // up = increase
    float range = cell.maxVal - cell.minVal;
    float sensitivity = range / 200.0f;
    float newVal = dragStartValue + deltaY * sensitivity;
    newVal = juce::jlimit (cell.minVal, cell.maxVal, newVal);

    if (cell.step >= 1.0f)
        newVal = std::round (newVal);

    if (auto* p = processor.apvts.getParameter (cell.paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (newVal));

    repaint();
}

void HeaderBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto pos = e.getPosition();

    for (int i = 0; i < (int) headerCells.size(); ++i)
    {
        const auto& cell = headerCells[(size_t) i];
        if (pos.x >= cell.x && pos.x < cell.x + cell.w &&
            pos.y >= cell.y && pos.y < cell.y + cell.h)
        {
            if (cell.isChoice || cell.isBoolean || cell.isReadOnly || cell.isSetBpm)
                return;

            showTextEditor (cell);
            return;
        }
    }
}

void HeaderBar::showTextEditor (const HeaderCell& cell)
{
    textEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*textEditor);
    textEditor->setBounds (cell.x, cell.y + 8, cell.w, 16);
    textEditor->setFont (juce::Font (12.0f));
    textEditor->setColour (juce::TextEditor::backgroundColourId, Theme::tealHeader.brighter (0.2f));
    textEditor->setColour (juce::TextEditor::textColourId, juce::Colours::white);
    textEditor->setColour (juce::TextEditor::outlineColourId, Theme::accent);

    float currentVal = processor.apvts.getRawParameterValue (cell.paramId)->load();
    juce::String displayVal = cell.step >= 1.0f ? juce::String ((int) currentVal)
                                                 : juce::String (currentVal, 1);

    textEditor->setText (displayVal, false);
    textEditor->selectAll();
    textEditor->grabKeyboardFocus();

    juce::String paramId = cell.paramId;
    float minV = cell.minVal;
    float maxV = cell.maxVal;

    textEditor->onReturnKey = [this, paramId, minV, maxV] {
        if (textEditor == nullptr) return;
        float val = textEditor->getText().getFloatValue();
        val = juce::jlimit (minV, maxV, val);

        if (auto* p = processor.apvts.getParameter (paramId))
            p->setValueNotifyingHost (p->convertTo0to1 (val));

        textEditor.reset();
        repaint();
    };

    textEditor->onEscapeKey = [this] {
        textEditor.reset();
        repaint();
    };

    textEditor->onFocusLost = [this] {
        textEditor.reset();
        repaint();
    };
}

void HeaderBar::showSetBpmPopup (bool forSampleDefault)
{
    juce::PopupMenu menu;
    menu.addItem (1, "4 bars");
    menu.addItem (2, "2 bars");
    menu.addItem (3, "1 bar");
    menu.addItem (4, "1/2 bar");
    menu.addItem (5, "1/4 bar");
    menu.addItem (6, "1/8 bar");
    menu.addItem (7, "1/16 bar");
    menu.addItem (8, "1/32 bar");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, forSampleDefault] (int result) {
            if (result <= 0) return;
            float bars[] = { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f };

            // Determine start/end from selected slice or full sample
            int startS = 0;
            int endS = processor.sampleData.getNumFrames();
            int sel = processor.sliceManager.selectedSlice;
            if (sel >= 0 && sel < processor.sliceManager.getNumSlices())
            {
                const auto& s = processor.sliceManager.getSlice (sel);
                startS = s.startSample;
                endS = s.endSample;
            }

            if (forSampleDefault)
            {
                // Set sample-level BPM
                float newBpm = WsolaEngine::calcStretchBpm (startS, endS, bars[result], processor.getSampleRate());
                if (auto* p = processor.apvts.getParameter (ParamIds::defaultBpm))
                    p->setValueNotifyingHost (p->convertTo0to1 (newBpm));
            }
            else
            {
                // Set slice BPM via command
                IntersectProcessor::Command cmd;
                cmd.type = IntersectProcessor::CmdStretch;
                cmd.floatParam1 = bars[result];
                processor.pushCommand (cmd);
            }
            repaint();
        });
}
