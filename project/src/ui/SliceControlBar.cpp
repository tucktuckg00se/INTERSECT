#include "SliceControlBar.h"
#include "TuckersLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../audio/WsolaEngine.h"

SliceControlBar::SliceControlBar (IntersectProcessor& p) : processor (p) {}

void SliceControlBar::drawLockIcon (juce::Graphics& g, int x, int y, bool locked)
{
    if (locked)
    {
        g.setColour (Theme::lockGold);
        g.fillRect (x, y, 8, 8);
    }
    else
    {
        g.setColour (Theme::lockDim.withAlpha (0.6f));
        g.drawRect (x, y, 8, 8, 1);
    }
}

void SliceControlBar::drawParamCell (juce::Graphics& g, int x, int y,
                                     const juce::String& label, const juce::String& value,
                                     bool locked, uint32_t lockBit,
                                     int fieldId, float minVal, float maxVal, float step,
                                     bool isBoolean, bool isChoice, int& outWidth)
{
    // Label
    g.setFont (juce::Font (9.0f));
    g.setColour (locked ? Theme::lockGold.withAlpha (0.8f)
                        : Theme::foreground.withAlpha (0.45f));
    g.drawText (label, x + 12, y + 2, 60, 12, juce::Justification::centredLeft);

    // Value
    g.setFont (juce::Font (11.0f));
    g.setColour (locked ? Theme::foreground
                        : Theme::foreground.withAlpha (0.4f));
    g.drawText (value, x + 12, y + 14, 60, 14, juce::Justification::centredLeft);

    // Lock icon
    drawLockIcon (g, x + 1, y + 4, locked);

    int labelW = (int) juce::Font (9.0f).getStringWidthFloat (label);
    int valueW = (int) juce::Font (11.0f).getStringWidthFloat (value);
    outWidth = std::max (std::max (labelW, valueW) + 18, 50);

    cells.push_back ({ x, y, outWidth, 28, lockBit, fieldId, minVal, maxVal, step, isBoolean, isChoice, false });
}

void SliceControlBar::paint (juce::Graphics& g)
{
    g.fillAll (Theme::darkBar);
    cells.clear();

    int idx = processor.sliceManager.selectedSlice;
    int numSlices = processor.sliceManager.getNumSlices();

    if (idx < 0 || idx >= numSlices)
    {
        g.setFont (juce::Font (12.0f));
        g.setColour (Theme::foreground.withAlpha (0.35f));
        g.drawText ("No slice selected", 8, 14, 200, 16, juce::Justification::centredLeft);
        return;
    }

    const auto& s = processor.sliceManager.getSlice (idx);

    // Slice label
    g.setFont (juce::Font (13.0f).boldened());
    g.setColour (Theme::accent);
    g.drawText ("Slice " + juce::String (idx + 1), 8, 14, 55, 16, juce::Justification::centredLeft);
    int x = 70;

    // Global defaults for inheritance display
    float gBpm     = processor.apvts.getRawParameterValue (ParamIds::defaultBpm)->load();
    float gPitch   = processor.apvts.getRawParameterValue (ParamIds::defaultPitch)->load();
    int   gAlgo    = (int) processor.apvts.getRawParameterValue (ParamIds::defaultAlgorithm)->load();
    float gAttack  = processor.apvts.getRawParameterValue (ParamIds::defaultAttack)->load();
    float gDecay   = processor.apvts.getRawParameterValue (ParamIds::defaultDecay)->load();
    float gSustain = processor.apvts.getRawParameterValue (ParamIds::defaultSustain)->load();
    float gRelease = processor.apvts.getRawParameterValue (ParamIds::defaultRelease)->load();
    int   gMG      = (int) processor.apvts.getRawParameterValue (ParamIds::defaultMuteGroup)->load();
    bool  gPP      = processor.apvts.getRawParameterValue (ParamIds::defaultPingPong)->load() > 0.5f;
    bool  gStretch = processor.apvts.getRawParameterValue (ParamIds::defaultStretchEnabled)->load() > 0.5f;

    int cw;
    using F = IntersectProcessor;

    // BPM
    bool locked = s.lockMask & kLockBpm;
    juce::String val = juce::String ((int) (locked ? s.bpm : gBpm));
    drawParamCell (g, x, 2, "BPM", val, locked, kLockBpm, F::FieldBpm, 20.0f, 999.0f, 1.0f, false, false, cw);
    x += cw + 4;

    // SET BPM (slice-level)
    {
        g.setFont (juce::Font (9.0f));
        g.setColour (Theme::accent);
        g.drawText ("SET", x + 2, 4, 30, 12, juce::Justification::centredLeft);
        g.drawText ("BPM", x + 2, 16, 30, 12, juce::Justification::centredLeft);
        cells.push_back ({ x, 2, 34, 28, 0, 0, 0.0f, 0.0f, 0.0f, false, false, true });
        x += 38;
    }

    // PITCH
    locked = s.lockMask & kLockPitch;
    float pv = locked ? s.pitchSemitones : gPitch;
    val = (pv >= 0 ? "+" : "") + juce::String (pv, 1);
    drawParamCell (g, x, 2, "PITCH", val, locked, kLockPitch, F::FieldPitch, -24.0f, 24.0f, 0.1f, false, false, cw);
    x += cw + 4;

    // ALGORITHM
    locked = s.lockMask & kLockAlgorithm;
    int av = locked ? s.algorithm : gAlgo;
    drawParamCell (g, x, 2, "ALGO", av == 0 ? "Repitch" : "Stretch", locked, kLockAlgorithm, F::FieldAlgorithm, 0.0f, 1.0f, 1.0f, false, true, cw);
    x += cw + 4;

    // ATTACK
    locked = s.lockMask & kLockAttack;
    float atk = locked ? s.attackSec * 1000.0f : gAttack;
    drawParamCell (g, x, 2, "ATK", juce::String ((int) atk) + "ms", locked, kLockAttack, F::FieldAttack, 0.0f, 1.0f, 0.001f, false, false, cw);
    x += cw + 4;

    // DECAY
    locked = s.lockMask & kLockDecay;
    float dec = locked ? s.decaySec * 1000.0f : gDecay;
    drawParamCell (g, x, 2, "DEC", juce::String ((int) dec) + "ms", locked, kLockDecay, F::FieldDecay, 0.0f, 5.0f, 0.001f, false, false, cw);
    x += cw + 4;

    // SUSTAIN
    locked = s.lockMask & kLockSustain;
    float susVal = locked ? s.sustainLevel * 100.0f : gSustain;
    drawParamCell (g, x, 2, "SUS", juce::String ((int) susVal) + "%", locked, kLockSustain, F::FieldSustain, 0.0f, 1.0f, 0.01f, false, false, cw);
    x += cw + 4;

    // RELEASE
    locked = s.lockMask & kLockRelease;
    float relVal = locked ? s.releaseSec * 1000.0f : gRelease;
    drawParamCell (g, x, 2, "REL", juce::String ((int) relVal) + "ms", locked, kLockRelease, F::FieldRelease, 0.0f, 5.0f, 0.001f, false, false, cw);
    x += cw + 4;

    // MUTE GROUP
    locked = s.lockMask & kLockMuteGroup;
    int mg = locked ? s.muteGroup : gMG;
    drawParamCell (g, x, 2, "MUTE", juce::String (mg), locked, kLockMuteGroup, F::FieldMuteGroup, 0.0f, 32.0f, 1.0f, false, false, cw);
    x += cw + 4;

    // PING-PONG
    locked = s.lockMask & kLockPingPong;
    bool pp = locked ? s.pingPong : gPP;
    drawParamCell (g, x, 2, "PP", pp ? "ON" : "OFF", locked, kLockPingPong, F::FieldPingPong, 0.0f, 1.0f, 1.0f, true, false, cw);
    x += cw + 4;

    // STRETCH
    locked = s.lockMask & kLockStretch;
    bool strOn = locked ? s.stretchEnabled : gStretch;
    drawParamCell (g, x, 2, "STRETCH", strOn ? "ON" : "OFF", locked, kLockStretch, F::FieldStretchEnabled, 0.0f, 1.0f, 1.0f, true, false, cw);
    x += cw + 4;

    // TONALITY
    float gTonal = processor.apvts.getRawParameterValue (ParamIds::defaultTonality)->load();
    locked = s.lockMask & kLockTonality;
    float tonalVal = locked ? s.tonalityHz : gTonal;
    drawParamCell (g, x, 2, "TONAL", juce::String ((int) tonalVal) + "Hz", locked, kLockTonality, F::FieldTonality, 0.0f, 8000.0f, 100.0f, false, false, cw);
    x += cw + 4;

    // FORMANT
    float gFmnt = processor.apvts.getRawParameterValue (ParamIds::defaultFormant)->load();
    locked = s.lockMask & kLockFormant;
    float fmntVal = locked ? s.formantSemitones : gFmnt;
    drawParamCell (g, x, 2, "FMNT", (fmntVal >= 0 ? "+" : "") + juce::String (fmntVal, 1), locked, kLockFormant, F::FieldFormant, -24.0f, 24.0f, 0.1f, false, false, cw);
    x += cw + 4;

    // FORMANT COMP
    bool gFmntC = processor.apvts.getRawParameterValue (ParamIds::defaultFormantComp)->load() > 0.5f;
    locked = s.lockMask & kLockFormantComp;
    bool fmntCVal = locked ? s.formantComp : gFmntC;
    drawParamCell (g, x, 2, "FMNT C", fmntCVal ? "ON" : "OFF", locked, kLockFormantComp, F::FieldFormantComp, 0.0f, 1.0f, 1.0f, true, false, cw);
    x += cw + 4;

    // MIDI note (not lockable — no lock bit)
    g.setFont (juce::Font (9.0f));
    g.setColour (Theme::foreground.withAlpha (0.5f));
    g.drawText ("MIDI", x + 2, 2, 40, 12, juce::Justification::centredLeft);
    g.setFont (juce::Font (11.0f));
    g.setColour (Theme::foreground.withAlpha (0.8f));
    g.drawText (juce::String (s.midiNote), x + 2, 14, 40, 14, juce::Justification::centredLeft);
    cells.push_back ({ x, 2, 40, 28, 0, F::FieldMidiNote, 0.0f, 127.0f, 1.0f, false, false, false });
    x += 40;

    // Start/End/Length info
    g.setFont (juce::Font (9.0f));
    g.setColour (Theme::foreground.withAlpha (0.35f));
    double srate = processor.getSampleRate();
    if (srate <= 0) srate = 44100.0;
    double lenSec = (s.endSample - s.startSample) / srate;
    g.drawText ("Start:" + juce::String (s.startSample) +
                "  End:" + juce::String (s.endSample) +
                "  Len:" + juce::String (s.endSample - s.startSample) +
                " (" + juce::String (lenSec, 2) + "s)",
                70, 32, 500, 12, juce::Justification::centredLeft);
}

void SliceControlBar::mouseDown (const juce::MouseEvent& e)
{
    if (textEditor != nullptr)
        textEditor.reset();

    auto pos = e.getPosition();

    for (int i = 0; i < (int) cells.size(); ++i)
    {
        const auto& cell = cells[(size_t) i];

        // Lock icon click (first 12px of the cell)
        if (cell.lockBit != 0 && pos.x >= cell.x && pos.x < cell.x + 12 &&
            pos.y >= cell.y && pos.y < cell.y + cell.h)
        {
            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdToggleLock;
            cmd.intParam1 = (int) cell.lockBit;
            processor.pushCommand (cmd);
            repaint();
            return;
        }

        // SET BPM button
        if (cell.isSetBpm && pos.x >= cell.x && pos.x < cell.x + cell.w &&
            pos.y >= cell.y && pos.y < cell.y + cell.h)
        {
            showSetBpmPopup();
            return;
        }

        // Value area click
        if (pos.x >= cell.x + 12 && pos.x < cell.x + cell.w &&
            pos.y >= cell.y && pos.y < cell.y + cell.h)
        {
            // Boolean toggle (ping-pong, stretch)
            if (cell.isBoolean)
            {
                int idx = processor.sliceManager.selectedSlice;
                if (idx >= 0 && idx < processor.sliceManager.getNumSlices())
                {
                    const auto& s = processor.sliceManager.getSlice (idx);
                    bool sliceLocked = s.lockMask & cell.lockBit;

                    bool currentVal = false;
                    if (cell.fieldId == IntersectProcessor::FieldPingPong)
                    {
                        bool gPP = processor.apvts.getRawParameterValue (ParamIds::defaultPingPong)->load() > 0.5f;
                        currentVal = sliceLocked ? s.pingPong : gPP;
                    }
                    else if (cell.fieldId == IntersectProcessor::FieldStretchEnabled)
                    {
                        bool gStr = processor.apvts.getRawParameterValue (ParamIds::defaultStretchEnabled)->load() > 0.5f;
                        currentVal = sliceLocked ? s.stretchEnabled : gStr;
                    }
                    else if (cell.fieldId == IntersectProcessor::FieldFormantComp)
                    {
                        bool gFC = processor.apvts.getRawParameterValue (ParamIds::defaultFormantComp)->load() > 0.5f;
                        currentVal = sliceLocked ? s.formantComp : gFC;
                    }

                    IntersectProcessor::Command cmd;
                    cmd.type = IntersectProcessor::CmdSetSliceParam;
                    cmd.intParam1 = cell.fieldId;
                    cmd.floatParam1 = currentVal ? 0.0f : 1.0f;
                    processor.pushCommand (cmd);
                    repaint();
                }
                return;
            }

            // Algorithm choice popup
            if (cell.isChoice)
            {
                juce::PopupMenu menu;
                menu.addItem (1, "Repitch");
                menu.addItem (2, "Stretch");
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                    [this, fieldId = cell.fieldId] (int result) {
                        if (result > 0)
                        {
                            IntersectProcessor::Command cmd;
                            cmd.type = IntersectProcessor::CmdSetSliceParam;
                            cmd.intParam1 = fieldId;
                            cmd.floatParam1 = (float) (result - 1);
                            processor.pushCommand (cmd);
                            repaint();
                        }
                    });
                return;
            }

            // Set up drag for numeric params
            activeDragCell = i;
            dragStartY = pos.y;

            // Get current value
            int idx = processor.sliceManager.selectedSlice;
            if (idx >= 0 && idx < processor.sliceManager.getNumSlices())
            {
                const auto& s = processor.sliceManager.getSlice (idx);
                switch (cell.fieldId)
                {
                    case IntersectProcessor::FieldBpm:
                        dragStartValue = (s.lockMask & kLockBpm) ? s.bpm :
                            processor.apvts.getRawParameterValue (ParamIds::defaultBpm)->load();
                        break;
                    case IntersectProcessor::FieldPitch:
                        dragStartValue = (s.lockMask & kLockPitch) ? s.pitchSemitones :
                            processor.apvts.getRawParameterValue (ParamIds::defaultPitch)->load();
                        break;
                    case IntersectProcessor::FieldAttack:
                        dragStartValue = (s.lockMask & kLockAttack) ? s.attackSec :
                            processor.apvts.getRawParameterValue (ParamIds::defaultAttack)->load() / 1000.0f;
                        break;
                    case IntersectProcessor::FieldDecay:
                        dragStartValue = (s.lockMask & kLockDecay) ? s.decaySec :
                            processor.apvts.getRawParameterValue (ParamIds::defaultDecay)->load() / 1000.0f;
                        break;
                    case IntersectProcessor::FieldSustain:
                        dragStartValue = (s.lockMask & kLockSustain) ? s.sustainLevel :
                            processor.apvts.getRawParameterValue (ParamIds::defaultSustain)->load() / 100.0f;
                        break;
                    case IntersectProcessor::FieldRelease:
                        dragStartValue = (s.lockMask & kLockRelease) ? s.releaseSec :
                            processor.apvts.getRawParameterValue (ParamIds::defaultRelease)->load() / 1000.0f;
                        break;
                    case IntersectProcessor::FieldMuteGroup:
                        dragStartValue = (float) ((s.lockMask & kLockMuteGroup) ? s.muteGroup :
                            (int) processor.apvts.getRawParameterValue (ParamIds::defaultMuteGroup)->load());
                        break;
                    case IntersectProcessor::FieldMidiNote:
                        dragStartValue = (float) s.midiNote;
                        break;
                    case IntersectProcessor::FieldTonality:
                        dragStartValue = (s.lockMask & kLockTonality) ? s.tonalityHz :
                            processor.apvts.getRawParameterValue (ParamIds::defaultTonality)->load();
                        break;
                    case IntersectProcessor::FieldFormant:
                        dragStartValue = (s.lockMask & kLockFormant) ? s.formantSemitones :
                            processor.apvts.getRawParameterValue (ParamIds::defaultFormant)->load();
                        break;
                    default:
                        dragStartValue = 0.0f;
                        break;
                }
            }
            return;
        }
    }
}

void SliceControlBar::mouseDrag (const juce::MouseEvent& e)
{
    if (activeDragCell < 0 || activeDragCell >= (int) cells.size())
        return;

    const auto& cell = cells[(size_t) activeDragCell];
    float deltaY = (float) (dragStartY - e.y);
    float range = cell.maxVal - cell.minVal;
    float sensitivity = range / 200.0f;
    float newVal = dragStartValue + deltaY * sensitivity;
    newVal = juce::jlimit (cell.minVal, cell.maxVal, newVal);

    if (cell.step >= 1.0f)
        newVal = std::round (newVal / cell.step) * cell.step;

    IntersectProcessor::Command cmd;
    cmd.type = IntersectProcessor::CmdSetSliceParam;
    cmd.intParam1 = cell.fieldId;
    cmd.floatParam1 = newVal;
    processor.pushCommand (cmd);
    repaint();
}

void SliceControlBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto pos = e.getPosition();

    for (int i = 0; i < (int) cells.size(); ++i)
    {
        const auto& cell = cells[(size_t) i];

        if (pos.x >= cell.x + 12 && pos.x < cell.x + cell.w &&
            pos.y >= cell.y && pos.y < cell.y + cell.h)
        {
            if (cell.isBoolean || cell.isChoice)
                return;

            // Get current value
            float currentVal = 0.0f;
            int idx = processor.sliceManager.selectedSlice;
            if (idx >= 0 && idx < processor.sliceManager.getNumSlices())
            {
                const auto& s = processor.sliceManager.getSlice (idx);
                switch (cell.fieldId)
                {
                    case IntersectProcessor::FieldBpm:
                        currentVal = (s.lockMask & kLockBpm) ? s.bpm :
                            processor.apvts.getRawParameterValue (ParamIds::defaultBpm)->load();
                        break;
                    case IntersectProcessor::FieldPitch:
                        currentVal = (s.lockMask & kLockPitch) ? s.pitchSemitones :
                            processor.apvts.getRawParameterValue (ParamIds::defaultPitch)->load();
                        break;
                    case IntersectProcessor::FieldAttack:
                        currentVal = (s.lockMask & kLockAttack) ? s.attackSec * 1000.0f :
                            processor.apvts.getRawParameterValue (ParamIds::defaultAttack)->load();
                        break;
                    case IntersectProcessor::FieldDecay:
                        currentVal = (s.lockMask & kLockDecay) ? s.decaySec * 1000.0f :
                            processor.apvts.getRawParameterValue (ParamIds::defaultDecay)->load();
                        break;
                    case IntersectProcessor::FieldSustain:
                        currentVal = (s.lockMask & kLockSustain) ? s.sustainLevel * 100.0f :
                            processor.apvts.getRawParameterValue (ParamIds::defaultSustain)->load();
                        break;
                    case IntersectProcessor::FieldRelease:
                        currentVal = (s.lockMask & kLockRelease) ? s.releaseSec * 1000.0f :
                            processor.apvts.getRawParameterValue (ParamIds::defaultRelease)->load();
                        break;
                    case IntersectProcessor::FieldMuteGroup:
                        currentVal = (float) ((s.lockMask & kLockMuteGroup) ? s.muteGroup :
                            (int) processor.apvts.getRawParameterValue (ParamIds::defaultMuteGroup)->load());
                        break;
                    case IntersectProcessor::FieldMidiNote:
                        currentVal = (float) s.midiNote;
                        break;
                    case IntersectProcessor::FieldTonality:
                        currentVal = (s.lockMask & kLockTonality) ? s.tonalityHz :
                            processor.apvts.getRawParameterValue (ParamIds::defaultTonality)->load();
                        break;
                    case IntersectProcessor::FieldFormant:
                        currentVal = (s.lockMask & kLockFormant) ? s.formantSemitones :
                            processor.apvts.getRawParameterValue (ParamIds::defaultFormant)->load();
                        break;
                    default: break;
                }
            }

            showTextEditor (cell, currentVal);
            return;
        }
    }
}

void SliceControlBar::showTextEditor (const ParamCell& cell, float currentValue)
{
    textEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*textEditor);
    textEditor->setBounds (cell.x + 12, cell.y + 12, cell.w - 14, 16);
    textEditor->setFont (juce::Font (11.0f));
    textEditor->setColour (juce::TextEditor::backgroundColourId, Theme::darkBar.brighter (0.15f));
    textEditor->setColour (juce::TextEditor::textColourId, Theme::foreground);
    textEditor->setColour (juce::TextEditor::outlineColourId, Theme::accent);

    // Display value
    juce::String displayVal;
    if (cell.fieldId == IntersectProcessor::FieldAttack ||
        cell.fieldId == IntersectProcessor::FieldDecay ||
        cell.fieldId == IntersectProcessor::FieldRelease)
        displayVal = juce::String ((int) currentValue);
    else if (cell.fieldId == IntersectProcessor::FieldSustain)
        displayVal = juce::String ((int) currentValue);
    else if (cell.step >= 1.0f)
        displayVal = juce::String ((int) currentValue);
    else
        displayVal = juce::String (currentValue, 1);

    textEditor->setText (displayVal, false);
    textEditor->selectAll();
    textEditor->grabKeyboardFocus();

    int fieldId = cell.fieldId;
    float minV = cell.minVal;
    float maxV = cell.maxVal;

    textEditor->onReturnKey = [this, fieldId, minV, maxV] {
        if (textEditor == nullptr) return;
        float val = textEditor->getText().getFloatValue();

        // Convert ms to seconds for ATK/DEC/REL, percent to fraction for SUS
        if (fieldId == IntersectProcessor::FieldAttack ||
            fieldId == IntersectProcessor::FieldDecay ||
            fieldId == IntersectProcessor::FieldRelease)
            val /= 1000.0f;
        else if (fieldId == IntersectProcessor::FieldSustain)
            val /= 100.0f;

        val = juce::jlimit (minV, maxV, val);

        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdSetSliceParam;
        cmd.intParam1 = fieldId;
        cmd.floatParam1 = val;
        processor.pushCommand (cmd);
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

void SliceControlBar::showSetBpmPopup()
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
        [this] (int result) {
            if (result <= 0) return;
            float bars[] = { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f };

            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdStretch;
            cmd.floatParam1 = bars[result];
            processor.pushCommand (cmd);
            repaint();
        });
}
