#include "SliceControlBar.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../audio/GrainEngine.h"

SliceControlBar::SliceControlBar (IntersectProcessor& p) : processor (p)
{
}

void SliceControlBar::resized()
{
}

void SliceControlBar::drawLockIcon (juce::Graphics& g, int x, int y, bool locked)
{
    if (locked)
    {
        g.setColour (getTheme().lockActive);
        g.fillRect (x, y, 10, 10);
    }
    else
    {
        g.setColour (getTheme().lockInactive.withAlpha (0.6f));
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
    g.setFont (IntersectLookAndFeel::makeFont (12.0f));
    g.setColour (locked ? getTheme().lockActive.withAlpha (0.8f)
                        : getTheme().foreground.withAlpha (0.45f));
    g.drawText (label, x + 14, y + 2, 70, 13, juce::Justification::centredLeft);

    // Value
    g.setFont (IntersectLookAndFeel::makeFont (14.0f));
    g.setColour (locked ? getTheme().foreground
                        : getTheme().foreground.withAlpha (0.4f));
    g.drawText (value, x + 14, y + 15, 70, 14, juce::Justification::centredLeft);

    // Lock icon
    drawLockIcon (g, x + 1, y + 4, locked);

    int labelW = (int) IntersectLookAndFeel::makeFont (12.0f).getStringWidthFloat (label);
    int valueW = (int) IntersectLookAndFeel::makeFont (14.0f).getStringWidthFloat (value);
    outWidth = std::max (std::max (labelW, valueW) + 20, 55);

    cells.push_back ({ x, y, outWidth, cellH, lockBit, fieldId, minVal, maxVal, step, isBoolean, isChoice, false });
}

void SliceControlBar::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().darkBar);
    cells.clear();

    int idx = processor.sliceManager.selectedSlice;
    int numSlices = processor.sliceManager.getNumSlices();
    int rightEdge = getWidth() - 8;  // 8px right margin matching content

    int row1y = 2;
    int row2y = 36;

    // ====== Row 2 right side: always draw SLICES and ROOT ======
    {
        // ROOT note display (Row 2, right-aligned flush with rightEdge)
        int rn = processor.sliceManager.rootNote.load();
        bool editable = (numSlices == 0);
        int rnW = 55;
        int rnX = rightEdge - rnW;
        rootNoteArea = { rnX, row2y, rnW, 30 };

        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (editable ? getTheme().accent.withAlpha (0.7f)
                              : getTheme().foreground.withAlpha (0.35f));
        g.drawText ("ROOT", rnX, row2y + 2, rnW, 13, juce::Justification::right);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        g.setColour (editable ? getTheme().foreground.withAlpha (0.6f)
                              : getTheme().foreground.withAlpha (0.4f));
        g.drawText (juce::String (rn), rnX, row2y + 15, rnW, 14, juce::Justification::right);

        // SLICES count (Row 2, left of ROOT, right-aligned)
        int slcW = 55;
        int slcX = rnX - slcW - 4;
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.35f));
        g.drawText ("SLICES", slcX, row2y + 2, slcW, 13, juce::Justification::right);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        g.setColour (getTheme().foreground.withAlpha (0.4f));
        g.drawText (juce::String (numSlices), slcX, row2y + 15, slcW, 14, juce::Justification::right);
    }

    if (idx < 0 || idx >= numSlices)
    {
        g.setFont (IntersectLookAndFeel::makeFont (15.0f));
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
    int   gLoopMode = (int) processor.apvts.getRawParameterValue (ParamIds::defaultLoop)->load();
    bool  gStretch = processor.apvts.getRawParameterValue (ParamIds::defaultStretchEnabled)->load() > 0.5f;

    int cw;
    using F = IntersectProcessor;

    // ====== Row 1 (y=2): BPM | SET BPM | PITCH | ALGO | ... ======
    int x = 8;

    // Row 1 right side: SLICE N (12pt label) over S-E info (14pt value)
    {
        int infoW = rightEdge - 8;

        juce::String sliceLabel = "SLICE " + juce::String (idx + 1);
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().accent.withAlpha (0.7f));
        g.drawText (sliceLabel, 8, row1y + 2, infoW, 13, juce::Justification::right);

        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        g.setColour (getTheme().foreground.withAlpha (0.5f));
        double srate = processor.getSampleRate();
        if (srate <= 0) srate = 44100.0;
        double lenSec = (s.endSample - s.startSample) / srate;
        juce::String seText = juce::String (s.startSample) + "-" + juce::String (s.endSample) +
                              " (" + juce::String (lenSec, 2) + "s)";
        g.drawText (seText, 8, row1y + 15, infoW, 14, juce::Justification::right);
    }

    // BPM
    bool locked = s.lockMask & kLockBpm;
    float bpmVal = locked ? s.bpm : gBpm;
    juce::String val;
    {
        juce::String bpmStr = juce::String (bpmVal, 2);
        if (bpmStr.contains ("."))
        {
            while (bpmStr.endsWith ("0")) bpmStr = bpmStr.dropLastCharacters (1);
            if (bpmStr.endsWith (".")) bpmStr = bpmStr.dropLastCharacters (1);
        }
        val = bpmStr;
    }
    drawParamCell (g, x, row1y, "BPM", val, locked, kLockBpm, F::FieldBpm, 20.0f, 999.0f, 0.01f, false, false, cw);
    x += cw + 4;

    // SET BPM (slice-level)
    {
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
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

    // STRETCH (moved from row 2)
    {
        locked = s.lockMask & kLockStretch;
        bool strOnR1 = locked ? s.stretchEnabled : gStretch;
        drawParamCell (g, x, row1y, "STRETCH", strOnR1 ? "ON" : "OFF",
                       locked, kLockStretch, F::FieldStretchEnabled,
                       0.0f, 1.0f, 1.0f, true, false, cw);
        x += cw + 4;
    }

    // 1SHOT (moved from row 2)
    {
        bool gOneShot = processor.apvts.getRawParameterValue (ParamIds::defaultOneShot)->load() > 0.5f;
        locked = s.lockMask & kLockOneShot;
        bool oneShotVal = locked ? s.oneShot : gOneShot;
        drawParamCell (g, x, row1y, "1SHOT", oneShotVal ? "ON" : "OFF",
                       locked, kLockOneShot, F::FieldOneShot,
                       0.0f, 1.0f, 1.0f, true, false, cw);
        x += cw + 4;
    }

    // ====== Separator line ======
    g.setColour (getTheme().separator);
    g.drawHorizontalLine (34, 8.0f, (float) getWidth() - 8.0f);

    // ====== Row 2 (y=36): ATK | DEC | SUS | REL | TAIL | REV | PP | MUTE | STRETCH | GAIN | OUT | MIDI ======
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

    // TAIL (release tail)
    bool gTail = processor.apvts.getRawParameterValue (ParamIds::defaultReleaseTail)->load() > 0.5f;
    locked = s.lockMask & kLockReleaseTail;
    bool tailVal = locked ? s.releaseTail : gTail;
    drawParamCell (g, x, row2y, "TAIL", tailVal ? "ON" : "OFF", locked, kLockReleaseTail, F::FieldReleaseTail, 0.0f, 1.0f, 1.0f, true, false, cw);
    x += cw + 4;

    // REV (reverse)
    bool gRev = processor.apvts.getRawParameterValue (ParamIds::defaultReverse)->load() > 0.5f;
    locked = s.lockMask & kLockReverse;
    bool revVal = locked ? s.reverse : gRev;
    drawParamCell (g, x, row2y, "REV", revVal ? "ON" : "OFF", locked, kLockReverse, F::FieldReverse, 0.0f, 1.0f, 1.0f, true, false, cw);
    x += cw + 4;

    // LOOP (choice: Off/Loop/PP)
    locked = s.lockMask & kLockLoop;
    int loopVal = locked ? s.loopMode : gLoopMode;
    juce::String loopNames[] = { "OFF", "LOOP", "PP" };
    drawParamCell (g, x, row2y, "LOOP", loopNames[juce::jlimit (0, 2, loopVal)], locked, kLockLoop, F::FieldLoop, 0.0f, 2.0f, 1.0f, false, true, cw);
    x += cw + 4;

    // MUTE GROUP
    locked = s.lockMask & kLockMuteGroup;
    int mg = locked ? s.muteGroup : gMG;
    drawParamCell (g, x, row2y, "MUTE", juce::String (mg), locked, kLockMuteGroup, F::FieldMuteGroup, 0.0f, 32.0f, 1.0f, false, false, cw);
    x += cw + 4;

    // GAIN (dB)
    float gGainDb = processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load();
    locked = s.lockMask & kLockVolume;
    float gainVal = locked ? s.volume : gGainDb;
    {
        juce::String gainStr = (gainVal >= 0.0f ? "+" : "") + juce::String (gainVal, 1) + "dB";
        drawParamCell (g, x, row2y, "GAIN", gainStr, locked, kLockVolume, F::FieldVolume, -100.0f, 24.0f, 0.1f, false, false, cw);
    }
    x += cw + 4;

    // OUT (output bus)
    locked = s.lockMask & kLockOutputBus;
    int outBus = locked ? s.outputBus : 0;
    drawParamCell (g, x, row2y, "OUT", juce::String (outBus + 1), locked, kLockOutputBus, F::FieldOutputBus, 0.0f, 15.0f, 1.0f, false, false, cw);
    x += cw + 4;

    // MIDI note (not lockable)
    g.setFont (IntersectLookAndFeel::makeFont (12.0f));
    g.setColour (getTheme().foreground.withAlpha (0.5f));
    g.drawText ("MIDI", x + 2, row2y + 2, 45, 13, juce::Justification::centredLeft);
    g.setFont (IntersectLookAndFeel::makeFont (14.0f));
    g.setColour (getTheme().foreground.withAlpha (0.8f));
    g.drawText (juce::String (s.midiNote), x + 2, row2y + 15, 45, 14, juce::Justification::centredLeft);
    cells.push_back ({ x, row2y, 45, 32, 0, F::FieldMidiNote, 0.0f, 127.0f, 1.0f, false, false, false });
    x += 49;
}

void SliceControlBar::mouseDown (const juce::MouseEvent& e)
{
    if (textEditor != nullptr)
        textEditor.reset();

    activeDragCell = -1;
    draggingRootNote = false;
    auto pos = e.getPosition();

    // Root note drag (only editable when no slices exist)
    if (processor.sliceManager.getNumSlices() == 0 && rootNoteArea.contains (pos))
    {
        IntersectProcessor::Command gestureCmd;
        gestureCmd.type = IntersectProcessor::CmdBeginGesture;
        processor.pushCommand (gestureCmd);

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
                    if (cell.fieldId == IntersectProcessor::FieldStretchEnabled)
                    {
                        bool gStr = processor.apvts.getRawParameterValue (ParamIds::defaultStretchEnabled)->load() > 0.5f;
                        currentVal = sliceLocked ? s.stretchEnabled : gStr;
                    }
                    else if (cell.fieldId == IntersectProcessor::FieldFormantComp)
                    {
                        bool gFC = processor.apvts.getRawParameterValue (ParamIds::defaultFormantComp)->load() > 0.5f;
                        currentVal = sliceLocked ? s.formantComp : gFC;
                    }
                    else if (cell.fieldId == IntersectProcessor::FieldReleaseTail)
                    {
                        bool gTail = processor.apvts.getRawParameterValue (ParamIds::defaultReleaseTail)->load() > 0.5f;
                        currentVal = sliceLocked ? s.releaseTail : gTail;
                    }
                    else if (cell.fieldId == IntersectProcessor::FieldReverse)
                    {
                        bool gRev = processor.apvts.getRawParameterValue (ParamIds::defaultReverse)->load() > 0.5f;
                        currentVal = sliceLocked ? s.reverse : gRev;
                    }
                    else if (cell.fieldId == IntersectProcessor::FieldOneShot)
                    {
                        bool gOS = processor.apvts.getRawParameterValue (ParamIds::defaultOneShot)->load() > 0.5f;
                        currentVal = sliceLocked ? s.oneShot : gOS;
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

            // Choice: click to cycle
            if (cell.isChoice)
            {
                int idx = processor.sliceManager.selectedSlice;
                if (idx >= 0 && idx < processor.sliceManager.getNumSlices())
                {
                    const auto& s = processor.sliceManager.getSlice (idx);
                    int current = 0;
                    int maxVal = (int) cell.maxVal;

                    if (cell.fieldId == IntersectProcessor::FieldAlgorithm)
                    {
                        int gAlgo = (int) processor.apvts.getRawParameterValue (ParamIds::defaultAlgorithm)->load();
                        current = (s.lockMask & kLockAlgorithm) ? s.algorithm : gAlgo;
                    }
                    else if (cell.fieldId == IntersectProcessor::FieldGrainMode)
                    {
                        int gGM = (int) processor.apvts.getRawParameterValue (ParamIds::defaultGrainMode)->load();
                        current = (s.lockMask & kLockGrainMode) ? s.grainMode : gGM;
                    }
                    else if (cell.fieldId == IntersectProcessor::FieldLoop)
                    {
                        int gLM = (int) processor.apvts.getRawParameterValue (ParamIds::defaultLoop)->load();
                        current = (s.lockMask & kLockLoop) ? s.loopMode : gLM;
                    }

                    int next = (current + 1) > maxVal ? 0 : current + 1;
                    IntersectProcessor::Command cmd;
                    cmd.type = IntersectProcessor::CmdSetSliceParam;
                    cmd.intParam1 = cell.fieldId;
                    cmd.floatParam1 = (float) next;
                    processor.pushCommand (cmd);
                    repaint();
                }
                return;
            }

            // Set up drag for numeric params
            {
                IntersectProcessor::Command gestureCmd;
                gestureCmd.type = IntersectProcessor::CmdBeginGesture;
                processor.pushCommand (gestureCmd);
            }
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
                    case IntersectProcessor::FieldReleaseTail:
                    case IntersectProcessor::FieldReverse:
                        // Boolean — handled by toggle, shouldn't reach here
                        break;
                    case IntersectProcessor::FieldOutputBus:
                        dragStartValue = (float) ((s.lockMask & kLockOutputBus) ? s.outputBus : 0);
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
        textEditor->setBounds (rootNoteArea.getX(), rootNoteArea.getY() + 15,
                               rootNoteArea.getWidth(), 16);
        textEditor->setFont (IntersectLookAndFeel::makeFont (14.0f));
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
                        currentVal = (s.lockMask & kLockVolume) ? s.volume :
                            processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load();
                        break;
                    case IntersectProcessor::FieldOutputBus:
                        currentVal = (float) ((s.lockMask & kLockOutputBus) ? s.outputBus : 0);
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
    textEditor->setFont (IntersectLookAndFeel::makeFont (14.0f));
    textEditor->setColour (juce::TextEditor::backgroundColourId, getTheme().darkBar.brighter (0.15f));
    textEditor->setColour (juce::TextEditor::textColourId, getTheme().foreground);
    textEditor->setColour (juce::TextEditor::outlineColourId, getTheme().accent);

    // Display value
    juce::String displayVal;
    if (cell.fieldId == IntersectProcessor::FieldAttack ||
             cell.fieldId == IntersectProcessor::FieldDecay ||
             cell.fieldId == IntersectProcessor::FieldRelease)
        displayVal = juce::String ((int) currentValue);
    else if (cell.fieldId == IntersectProcessor::FieldSustain)
        displayVal = juce::String ((int) currentValue);
    else if (cell.fieldId == IntersectProcessor::FieldVolume)
        displayVal = juce::String (currentValue, 1);
    else if (cell.fieldId == IntersectProcessor::FieldBpm)
        displayVal = juce::String (currentValue, 2);
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
        // Volume is now in dB — no conversion needed

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

    auto* topLvl = getTopLevelComponent();
    float ms = IntersectLookAndFeel::getMenuScale();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withParentComponent (topLvl)
                            .withStandardItemHeight ((int) (24 * ms)),
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
