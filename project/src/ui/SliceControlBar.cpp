#include "SliceControlBar.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../audio/GrainEngine.h"

SliceControlBar::SliceControlBar (IntersectProcessor& p) : processor (p)
{
    addAndMakeVisible (midiSelectBtn);
    midiSelectBtn.setAlwaysOnTop (true);
    midiSelectBtn.setTooltip ("MIDI selects slice");
    midiSelectBtn.onClick = [this] {
        bool current = processor.midiSelectsSlice.load();
        bool newState = ! current;
        processor.midiSelectsSlice.store (newState);
        updateMidiButtonAppearance (newState);
        repaint();
    };
    updateMidiButtonAppearance (false);
}

void SliceControlBar::resized()
{
    int btnW = 24;
    midiSelectBtn.setBounds (getWidth() - btnW - 4, 2, btnW, 28);
}

void SliceControlBar::updateMidiButtonAppearance (bool active)
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

void SliceControlBar::drawLockIcon (juce::Graphics& g, int x, int y, bool locked)
{
    if (locked)
    {
        g.setColour (getTheme().lockGold);
        g.fillRect (x, y, 10, 10);
    }
    else
    {
        g.setColour (getTheme().lockDim.withAlpha (0.6f));
        g.drawRect (x, y, 10, 10, 1);
    }
}

void SliceControlBar::drawParamCell (juce::Graphics& g, int x, int y,
                                     const juce::String& label, const juce::String& value,
                                     bool locked, uint32_t lockBit,
                                     int fieldId, float minVal, float maxVal, float step,
                                     bool isBoolean, bool isChoice, int& outWidth)
{
    int cellH = 32;

    // Label
    g.setFont (juce::Font (12.0f));
    g.setColour (locked ? getTheme().lockGold.withAlpha (0.8f)
                        : getTheme().foreground.withAlpha (0.45f));
    g.drawText (label, x + 14, y + 2, 70, 13, juce::Justification::centredLeft);

    // Value
    g.setFont (juce::Font (14.0f));
    g.setColour (locked ? getTheme().foreground
                        : getTheme().foreground.withAlpha (0.4f));
    g.drawText (value, x + 14, y + 15, 70, 14, juce::Justification::centredLeft);

    // Lock icon
    drawLockIcon (g, x + 1, y + 4, locked);

    int labelW = (int) juce::Font (12.0f).getStringWidthFloat (label);
    int valueW = (int) juce::Font (14.0f).getStringWidthFloat (value);
    outWidth = std::max (std::max (labelW, valueW) + 20, 55);

    cells.push_back ({ x, y, outWidth, cellH, lockBit, fieldId, minVal, maxVal, step, isBoolean, isChoice, false });
}

void SliceControlBar::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().darkBar);
    cells.clear();

    // Sync M button appearance
    updateMidiButtonAppearance (processor.midiSelectsSlice.load());

    int idx = processor.sliceManager.selectedSlice;
    int numSlices = processor.sliceManager.getNumSlices();
    int rightEdge = midiSelectBtn.getX() - 4;  // right boundary for painted content

    // ====== Row 1 right side: always draw ROOT, SLICES, SLICE N ======
    int row1y = 2;

    // Root note display (always visible, Row 1)
    {
        int rn = processor.sliceManager.rootNote.load();
        bool editable = (numSlices == 0);
        int rnX = rightEdge - 55;
        rootNoteArea = { rnX, row1y, 55, 30 };

        g.setFont (juce::Font (12.0f));
        g.setColour (editable ? getTheme().accent.withAlpha (0.7f)
                              : getTheme().foreground.withAlpha (0.45f));
        g.drawText ("ROOT", rnX, row1y + 2, 55, 13, juce::Justification::centredLeft);
        g.setFont (juce::Font (14.0f));
        g.setColour (editable ? getTheme().foreground
                              : getTheme().foreground.withAlpha (0.8f));
        g.drawText (juce::String (rn), rnX, row1y + 15, 55, 14, juce::Justification::centredLeft);
    }

    // SLICES count (Row 1, left of ROOT)
    {
        int slcX = rightEdge - 115;
        g.setFont (juce::Font (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.45f));
        g.drawText ("SLICES", slcX, row1y + 2, 55, 13, juce::Justification::centredLeft);
        g.setFont (juce::Font (14.0f));
        g.setColour (getTheme().foreground.withAlpha (0.8f));
        g.drawText (juce::String (numSlices), slcX, row1y + 15, 55, 14, juce::Justification::centredLeft);
    }

    if (idx < 0 || idx >= numSlices)
    {
        g.setFont (juce::Font (15.0f));
        g.setColour (getTheme().foreground.withAlpha (0.35f));
        g.drawText ("No slice selected", 8, 24, 220, 18, juce::Justification::centredLeft);
        return;
    }

    const auto& s = processor.sliceManager.getSlice (idx);

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

    // ====== Row 1 (y=2): BPM | SET BPM | PITCH | ALGO | ... | S-E | SLICES | ROOT | SLICE N | M ======
    int x = 8;

    // Slice label (right side of row 1, left of ROOT)
    g.setFont (juce::Font (17.0f).boldened());
    g.setColour (getTheme().accent);
    g.drawText ("SLICE " + juce::String (idx + 1), rightEdge - 195, row1y + 4, 75, 20,
                juce::Justification::centredRight);

    // Start/End/Length info (Row 1, left of SLICE N)
    {
        g.setFont (juce::Font (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.35f));
        double srate = processor.getSampleRate();
        if (srate <= 0) srate = 44100.0;
        double lenSec = (s.endSample - s.startSample) / srate;
        juce::String seText = juce::String (s.startSample) + "-" + juce::String (s.endSample) +
                              " (" + juce::String (lenSec, 2) + "s)";
        int seW = (int) juce::Font (12.0f).getStringWidthFloat (seText) + 8;
        g.drawText (seText, rightEdge - 200 - seW, row1y + 8, seW, 14, juce::Justification::centredRight);
    }

    // BPM
    bool locked = s.lockMask & kLockBpm;
    juce::String val = juce::String ((int) (locked ? s.bpm : gBpm));
    drawParamCell (g, x, row1y, "BPM", val, locked, kLockBpm, F::FieldBpm, 20.0f, 999.0f, 1.0f, false, false, cw);
    x += cw + 4;

    // SET BPM (slice-level)
    {
        g.setFont (juce::Font (12.0f));
        g.setColour (getTheme().accent);
        g.drawText ("SET", x + 2, row1y + 2, 34, 13, juce::Justification::centredLeft);
        g.drawText ("BPM", x + 2, row1y + 15, 34, 13, juce::Justification::centredLeft);
        cells.push_back ({ x, row1y, 38, 32, 0, 0, 0.0f, 0.0f, 0.0f, false, false, true });
        x += 42;
    }

    // PITCH
    locked = s.lockMask & kLockPitch;
    float pv = locked ? s.pitchSemitones : gPitch;
    val = (pv >= 0 ? "+" : "") + juce::String (pv, 1);
    drawParamCell (g, x, row1y, "PITCH", val, locked, kLockPitch, F::FieldPitch, -24.0f, 24.0f, 0.1f, false, false, cw);
    x += cw + 4;

    // ALGORITHM
    locked = s.lockMask & kLockAlgorithm;
    int av = locked ? s.algorithm : gAlgo;
    juce::String algoNames[] = { "Repitch", "Stretch", "Bungee" };
    drawParamCell (g, x, row1y, "ALGO", algoNames[juce::jlimit (0, 2, av)], locked, kLockAlgorithm, F::FieldAlgorithm, 0.0f, 2.0f, 1.0f, false, true, cw);
    x += cw + 4;

    if (av == 1)
    {
        // TONALITY — only for Stretch (Signalsmith)
        float gTonal = processor.apvts.getRawParameterValue (ParamIds::defaultTonality)->load();
        locked = s.lockMask & kLockTonality;
        float tonalVal = locked ? s.tonalityHz : gTonal;
        drawParamCell (g, x, row1y, "TONAL", juce::String ((int) tonalVal) + "Hz", locked, kLockTonality, F::FieldTonality, 0.0f, 8000.0f, 100.0f, false, false, cw);
        x += cw + 4;

        // FORMANT
        float gFmnt = processor.apvts.getRawParameterValue (ParamIds::defaultFormant)->load();
        locked = s.lockMask & kLockFormant;
        float fmntVal = locked ? s.formantSemitones : gFmnt;
        drawParamCell (g, x, row1y, "FMNT", (fmntVal >= 0 ? "+" : "") + juce::String (fmntVal, 1), locked, kLockFormant, F::FieldFormant, -24.0f, 24.0f, 0.1f, false, false, cw);
        x += cw + 4;

        // FORMANT COMP
        bool gFmntC = processor.apvts.getRawParameterValue (ParamIds::defaultFormantComp)->load() > 0.5f;
        locked = s.lockMask & kLockFormantComp;
        bool fmntCVal = locked ? s.formantComp : gFmntC;
        drawParamCell (g, x, row1y, "FMNT C", fmntCVal ? "ON" : "OFF", locked, kLockFormantComp, F::FieldFormantComp, 0.0f, 1.0f, 1.0f, true, false, cw);
        x += cw + 4;
    }
    else if (av == 2)
    {
        // GRAIN — only for Bungee
        int gGM = (int) processor.apvts.getRawParameterValue (ParamIds::defaultGrainMode)->load();
        locked = s.lockMask & kLockGrainMode;
        int gmVal = locked ? s.grainMode : gGM;
        juce::String gmNames[] = { "Fast", "Normal", "Smooth" };
        drawParamCell (g, x, row1y, "GRAIN", gmNames[juce::jlimit (0, 2, gmVal)], locked, kLockGrainMode, F::FieldGrainMode, 0.0f, 2.0f, 1.0f, false, true, cw);
        x += cw + 4;
    }

    // ====== Separator line ======
    g.setColour (getTheme().separator);
    g.drawHorizontalLine (34, 8.0f, (float) getWidth() - 8.0f);

    // ====== Row 2 (y=36): ATK | DEC | SUS | REL | PP | MUTE | STRETCH | VOL | MIDI ======
    int row2y = 36;
    x = 8;

    // ATTACK
    locked = s.lockMask & kLockAttack;
    float atk = locked ? s.attackSec * 1000.0f : gAttack;
    drawParamCell (g, x, row2y, "ATK", juce::String ((int) atk) + "ms", locked, kLockAttack, F::FieldAttack, 0.0f, 1.0f, 0.001f, false, false, cw);
    x += cw + 4;

    // DECAY
    locked = s.lockMask & kLockDecay;
    float dec = locked ? s.decaySec * 1000.0f : gDecay;
    drawParamCell (g, x, row2y, "DEC", juce::String ((int) dec) + "ms", locked, kLockDecay, F::FieldDecay, 0.0f, 5.0f, 0.001f, false, false, cw);
    x += cw + 4;

    // SUSTAIN
    locked = s.lockMask & kLockSustain;
    float susVal = locked ? s.sustainLevel * 100.0f : gSustain;
    drawParamCell (g, x, row2y, "SUS", juce::String ((int) susVal) + "%", locked, kLockSustain, F::FieldSustain, 0.0f, 1.0f, 0.01f, false, false, cw);
    x += cw + 4;

    // RELEASE
    locked = s.lockMask & kLockRelease;
    float relVal = locked ? s.releaseSec * 1000.0f : gRelease;
    drawParamCell (g, x, row2y, "REL", juce::String ((int) relVal) + "ms", locked, kLockRelease, F::FieldRelease, 0.0f, 5.0f, 0.001f, false, false, cw);
    x += cw + 4;

    // PING-PONG
    locked = s.lockMask & kLockPingPong;
    bool pp = locked ? s.pingPong : gPP;
    drawParamCell (g, x, row2y, "PP", pp ? "ON" : "OFF", locked, kLockPingPong, F::FieldPingPong, 0.0f, 1.0f, 1.0f, true, false, cw);
    x += cw + 4;

    // MUTE GROUP
    locked = s.lockMask & kLockMuteGroup;
    int mg = locked ? s.muteGroup : gMG;
    drawParamCell (g, x, row2y, "MUTE", juce::String (mg), locked, kLockMuteGroup, F::FieldMuteGroup, 0.0f, 32.0f, 1.0f, false, false, cw);
    x += cw + 4;

    // STRETCH
    locked = s.lockMask & kLockStretch;
    bool strOn = locked ? s.stretchEnabled : gStretch;
    drawParamCell (g, x, row2y, "STRETCH", strOn ? "ON" : "OFF", locked, kLockStretch, F::FieldStretchEnabled, 0.0f, 1.0f, 1.0f, true, false, cw);
    x += cw + 4;

    // VOL
    float gVol = processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load();
    locked = s.lockMask & kLockVolume;
    float volVal = locked ? s.volume : gVol;
    drawParamCell (g, x, row2y, "VOL", juce::String ((int) (volVal * 100.0f)) + "%", locked, kLockVolume, F::FieldVolume, 0.0f, 1.0f, 0.01f, false, false, cw);
    x += cw + 4;

    // MIDI note (not lockable)
    g.setFont (juce::Font (12.0f));
    g.setColour (getTheme().foreground.withAlpha (0.5f));
    g.drawText ("MIDI", x + 2, row2y + 2, 45, 13, juce::Justification::centredLeft);
    g.setFont (juce::Font (14.0f));
    g.setColour (getTheme().foreground.withAlpha (0.8f));
    g.drawText (juce::String (s.midiNote), x + 2, row2y + 15, 45, 14, juce::Justification::centredLeft);
    cells.push_back ({ x, row2y, 45, 32, 0, F::FieldMidiNote, 0.0f, 127.0f, 1.0f, false, false, false });
    x += 49;
}

void SliceControlBar::mouseDown (const juce::MouseEvent& e)
{
    if (textEditor != nullptr)
        textEditor.reset();

    draggingRootNote = false;
    auto pos = e.getPosition();

    // Root note drag (only editable when no slices exist)
    if (processor.sliceManager.getNumSlices() == 0 && rootNoteArea.contains (pos))
    {
        draggingRootNote = true;
        dragStartY = pos.y;
        dragStartValue = (float) processor.sliceManager.rootNote.load();
        return;
    }

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

            // Choice popup (Algorithm or Grain Mode)
            if (cell.isChoice)
            {
                juce::PopupMenu menu;
                if (cell.fieldId == IntersectProcessor::FieldGrainMode)
                {
                    menu.addItem (1, "Fast");
                    menu.addItem (2, "Normal");
                    menu.addItem (3, "Smooth");
                }
                else
                {
                    menu.addItem (1, "Repitch");
                    menu.addItem (2, "Stretch");
                    menu.addItem (3, "Bungee");
                }
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
                    case IntersectProcessor::FieldVolume:
                        dragStartValue = (s.lockMask & kLockVolume) ? s.volume :
                            processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load();
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
    if (draggingRootNote)
    {
        float deltaY = (float) (dragStartY - e.y);
        int newVal = (int) (dragStartValue + deltaY * (127.0f / 200.0f));
        newVal = juce::jlimit (0, 127, newVal);
        processor.sliceManager.rootNote.store (newVal);
        repaint();
        return;
    }

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

    // Root note text entry (only when no slices)
    if (processor.sliceManager.getNumSlices() == 0 && rootNoteArea.contains (pos))
    {
        textEditor = std::make_unique<juce::TextEditor>();
        addAndMakeVisible (*textEditor);
        textEditor->setBounds (rootNoteArea.getX(), rootNoteArea.getY() + 14,
                               rootNoteArea.getWidth(), 16);
        textEditor->setFont (juce::Font (14.0f));
        textEditor->setColour (juce::TextEditor::backgroundColourId, getTheme().darkBar.brighter (0.15f));
        textEditor->setColour (juce::TextEditor::textColourId, getTheme().foreground);
        textEditor->setColour (juce::TextEditor::outlineColourId, getTheme().accent);
        textEditor->setText (juce::String (processor.sliceManager.rootNote.load()), false);
        textEditor->selectAll();
        textEditor->grabKeyboardFocus();

        textEditor->onReturnKey = [this] {
            if (textEditor == nullptr) return;
            int val = juce::jlimit (0, 127, textEditor->getText().getIntValue());
            processor.sliceManager.rootNote.store (val);
            textEditor.reset();
            repaint();
        };
        textEditor->onEscapeKey = [this] { textEditor.reset(); repaint(); };
        textEditor->onFocusLost = [this] { textEditor.reset(); repaint(); };
        return;
    }

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
                    case IntersectProcessor::FieldVolume:
                        currentVal = ((s.lockMask & kLockVolume) ? s.volume :
                            processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load()) * 100.0f;
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
    textEditor->setBounds (cell.x + 14, cell.y + 14, cell.w - 16, 16);
    textEditor->setFont (juce::Font (14.0f));
    textEditor->setColour (juce::TextEditor::backgroundColourId, getTheme().darkBar.brighter (0.15f));
    textEditor->setColour (juce::TextEditor::textColourId, getTheme().foreground);
    textEditor->setColour (juce::TextEditor::outlineColourId, getTheme().accent);

    // Display value
    juce::String displayVal;
    if (cell.fieldId == IntersectProcessor::FieldAttack ||
        cell.fieldId == IntersectProcessor::FieldDecay ||
        cell.fieldId == IntersectProcessor::FieldRelease)
        displayVal = juce::String ((int) currentValue);
    else if (cell.fieldId == IntersectProcessor::FieldSustain ||
             cell.fieldId == IntersectProcessor::FieldVolume)
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

        // Convert ms to seconds for ATK/DEC/REL, percent to fraction for SUS/VOL
        if (fieldId == IntersectProcessor::FieldAttack ||
            fieldId == IntersectProcessor::FieldDecay ||
            fieldId == IntersectProcessor::FieldRelease)
            val /= 1000.0f;
        else if (fieldId == IntersectProcessor::FieldSustain ||
                 fieldId == IntersectProcessor::FieldVolume)
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
    menu.addItem (1, "16 bars");
    menu.addItem (2, "8 bars");
    menu.addItem (3, "4 bars");
    menu.addItem (4, "2 bars");
    menu.addItem (5, "1 bar");
    menu.addItem (6, "1/2 note");
    menu.addItem (7, "1/4 note");
    menu.addItem (8, "1/8 note");
    menu.addItem (9, "1/16 note");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this] (int result) {
            if (result <= 0 || result > 9) return;
            const float bars[] = { 0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };

            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdStretch;
            cmd.floatParam1 = bars[result];
            processor.pushCommand (cmd);
            repaint();
        });
}
