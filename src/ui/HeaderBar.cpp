#include "HeaderBar.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include "../audio/GrainEngine.h"
#include <cmath>

HeaderBar::HeaderBar (IntersectProcessor& p) : processor (p)
{
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (redoBtn);
    addAndMakeVisible (loadBtn);
    addAndMakeVisible (themeBtn);
    undoBtn.setAlwaysOnTop (true);
    redoBtn.setAlwaysOnTop (true);
    loadBtn.setAlwaysOnTop (true);
    themeBtn.setAlwaysOnTop (true);

    // Style buttons to match M button
    for (auto* btn : { &undoBtn, &redoBtn, &loadBtn, &themeBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, getTheme().button);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().foreground);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().foreground);
    }

    undoBtn.setTooltip ("Undo");
    redoBtn.setTooltip ("Redo");

    undoBtn.onClick = [this] {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdUndo;
        processor.pushCommand (cmd);
    };

    redoBtn.onClick = [this] {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdRedo;
        processor.pushCommand (cmd);
    };

    loadBtn.onClick = [this] { openFileBrowser(); };

    themeBtn.onClick = [this] { showThemePopup(); };
}

void HeaderBar::resized()
{
    int right = getWidth() - 8;  // 8px right margin matching content

    themeBtn.setBounds     (right - 26, 2, 26, 28);
    loadBtn.setBounds      (right - 26 - 4 - 40, 2, 40, 28);

    // UNDO/REDO stacked vertically
    int undoW = 48;
    int undoX = right - 26 - 4 - 40 - 4 - undoW;
    undoBtn.setBounds      (undoX, 2, undoW, 13);
    redoBtn.setBounds      (undoX, 17, undoW, 13);
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
    // Re-theme buttons so theme changes take effect
    for (auto* btn : { &undoBtn, &redoBtn, &loadBtn, &themeBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, getTheme().button);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().foreground);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().foreground);
    }

    g.fillAll (getTheme().header);
    headerCells.clear();

    if (processor.sampleData.isLoaded())
    {
        // --- Row 1: BPM | SET BPM | PITCH | ALGO | TONAL | FMNT | FMNT C | filename | scale btns ---
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));

        int x = 8;
        int row1y = 2;
        int row1h = 30;

        int cellW = 60;  // uniform cell width
        int cellGap = 4; // uniform gap between cells

        // BPM
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.drawText ("BPM", x, row1y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        float bpm = processor.apvts.getRawParameterValue (ParamIds::defaultBpm)->load();
        {
            juce::String bpmStr = juce::String (bpm, 2);
            if (bpmStr.contains ("."))
            {
                while (bpmStr.endsWith ("0")) bpmStr = bpmStr.dropLastCharacters (1);
                if (bpmStr.endsWith (".")) bpmStr = bpmStr.dropLastCharacters (1);
            }
            g.drawText (bpmStr, x, row1y + 15, cellW, 14, juce::Justification::centredLeft);
        }
        headerCells.push_back ({ x, row1y, cellW, row1h, ParamIds::defaultBpm, 20.0f, 999.0f, 0.01f, false, false, false, false });
        x += cellW + cellGap;

        // SET BPM (sample-level)
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().accent);
        g.drawText ("SET", x + 2, row1y + 2, 34, 13, juce::Justification::centredLeft);
        g.drawText ("BPM", x + 2, row1y + 15, 34, 13, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row1y, 38, row1h, juce::String(), 0.0f, 0.0f, 0.0f, false, false, false, true });
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        x += 38 + cellGap;

        // PITCH — may be read-only in Repitch+Stretch mode
        {
            bool stretchOn = processor.apvts.getRawParameterValue (ParamIds::defaultStretchEnabled)->load() > 0.5f;
            int algo = (int) processor.apvts.getRawParameterValue (ParamIds::defaultAlgorithm)->load();
            bool pitchReadOnly = (algo == 0 && stretchOn);

            g.setFont (IntersectLookAndFeel::makeFont (12.0f));
            g.setColour (pitchReadOnly ? getTheme().foreground.withAlpha (0.5f) : getTheme().foreground.withAlpha (0.9f));
            g.drawText ("PITCH", x, row1y + 2, cellW, 13, juce::Justification::centredLeft);
            g.setFont (IntersectLookAndFeel::makeFont (14.0f));

            if (pitchReadOnly)
            {
                float dawBpm = processor.dawBpm.load();
                float calcPitch = (dawBpm > 0.0f && bpm > 0.0f)
                    ? 12.0f * std::log2 (dawBpm / bpm) : 0.0f;
                juce::String pitchStr = (calcPitch >= 0 ? "+" : "") + juce::String (calcPitch, 1) + "st";
                g.setColour (getTheme().foreground.withAlpha (0.5f));
                g.drawText (pitchStr, x, row1y + 15, cellW, 14, juce::Justification::centredLeft);
                headerCells.push_back ({ x, row1y, cellW, row1h, ParamIds::defaultPitch, -24.0f, 24.0f, 0.1f, false, false, true, false });
            }
            else
            {
                float pitch = processor.apvts.getRawParameterValue (ParamIds::defaultPitch)->load();
                g.setColour (getTheme().foreground.withAlpha (0.9f));
                g.drawText (juce::String (pitch, 1), x, row1y + 15, cellW, 14, juce::Justification::centredLeft);
                headerCells.push_back ({ x, row1y, cellW, row1h, ParamIds::defaultPitch, -24.0f, 24.0f, 0.1f, false, false, false, false });
            }
            x += cellW + cellGap;
        }

        // ALGORITHM
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("ALGO", x, row1y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        int algo = (int) processor.apvts.getRawParameterValue (ParamIds::defaultAlgorithm)->load();
        juce::String algoNames[] = { "Repitch", "Stretch", "Bungee" };
        g.drawText (algoNames[juce::jlimit (0, 2, algo)], x, row1y + 15, cellW, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row1y, cellW, row1h, ParamIds::defaultAlgorithm, 0.0f, 2.0f, 1.0f, true, false, false, false });
        x += cellW + cellGap;

        if (algo == 1)
        {
            // TONAL — only for Stretch (Signalsmith)
            g.setFont (IntersectLookAndFeel::makeFont (12.0f));
            g.setColour (getTheme().foreground.withAlpha (0.9f));
            g.drawText ("TONAL", x, row1y + 2, cellW, 13, juce::Justification::centredLeft);
            g.setFont (IntersectLookAndFeel::makeFont (14.0f));
            float tonal = processor.apvts.getRawParameterValue (ParamIds::defaultTonality)->load();
            g.drawText (juce::String ((int) tonal) + "Hz", x, row1y + 15, cellW, 14, juce::Justification::centredLeft);
            headerCells.push_back ({ x, row1y, cellW, row1h, ParamIds::defaultTonality, 0.0f, 8000.0f, 100.0f, false, false, false, false });
            x += cellW + cellGap;

            // FMNT
            g.setFont (IntersectLookAndFeel::makeFont (12.0f));
            g.setColour (getTheme().foreground.withAlpha (0.9f));
            g.drawText ("FMNT", x, row1y + 2, cellW, 13, juce::Justification::centredLeft);
            g.setFont (IntersectLookAndFeel::makeFont (14.0f));
            float fmnt = processor.apvts.getRawParameterValue (ParamIds::defaultFormant)->load();
            g.drawText ((fmnt >= 0 ? "+" : "") + juce::String (fmnt, 1), x, row1y + 15, cellW, 14, juce::Justification::centredLeft);
            headerCells.push_back ({ x, row1y, cellW, row1h, ParamIds::defaultFormant, -24.0f, 24.0f, 0.1f, false, false, false, false });
            x += cellW + cellGap;

            // FMNT C
            g.setFont (IntersectLookAndFeel::makeFont (12.0f));
            g.setColour (getTheme().foreground.withAlpha (0.9f));
            g.drawText ("FMNT C", x, row1y + 2, cellW, 13, juce::Justification::centredLeft);
            g.setFont (IntersectLookAndFeel::makeFont (14.0f));
            bool fmntC = processor.apvts.getRawParameterValue (ParamIds::defaultFormantComp)->load() > 0.5f;
            g.setColour (fmntC ? getTheme().lockActive : getTheme().foreground.withAlpha (0.5f));
            g.drawText (fmntC ? "ON" : "OFF", x, row1y + 15, cellW, 14, juce::Justification::centredLeft);
            headerCells.push_back ({ x, row1y, cellW, row1h, ParamIds::defaultFormantComp, 0.0f, 1.0f, 1.0f, false, true, false, false });
            x += cellW + cellGap;
        }
        else if (algo == 2)
        {
            // GRAIN — only for Bungee
            g.setFont (IntersectLookAndFeel::makeFont (12.0f));
            g.setColour (getTheme().foreground.withAlpha (0.9f));
            g.drawText ("GRAIN", x, row1y + 2, cellW, 13, juce::Justification::centredLeft);
            g.setFont (IntersectLookAndFeel::makeFont (14.0f));
            int gm = (int) processor.apvts.getRawParameterValue (ParamIds::defaultGrainMode)->load();
            juce::String gmNames[] = { "Fast", "Normal", "Smooth" };
            g.drawText (gmNames[juce::jlimit (0, 2, gm)], x, row1y + 15, cellW, 14, juce::Justification::centredLeft);
            headerCells.push_back ({ x, row1y, cellW, row1h, ParamIds::defaultGrainMode, 0.0f, 2.0f, 1.0f, true, false, false, false });
            x += cellW + cellGap;
        }

        // Filename and sample info (right-aligned, left of UNDO button)
        {
            int rightEdge = undoBtn.getX() - 6;
            bool isMissing = processor.sampleMissing.load();

            g.setFont (IntersectLookAndFeel::makeFont (12.0f));
            if (isMissing)
                g.setColour (juce::Colours::orange);
            else
                g.setColour (getTheme().foreground.withAlpha (0.35f));
            g.drawText (isMissing ? "MISSING" : "SAMPLE", x, row1y + 2, rightEdge - x, 13, juce::Justification::right);

            g.setFont (IntersectLookAndFeel::makeFont (14.0f));
            if (isMissing)
                g.setColour (juce::Colours::orange.withAlpha (0.9f));
            else
                g.setColour (getTheme().foreground.withAlpha (0.7f));

            juce::String fname = processor.sampleData.getFileName();
            if (isMissing)
            {
                g.drawText (fname + " (click to relink)", x, row1y + 15, rightEdge - x, 14, juce::Justification::right);
            }
            else
            {
                double srate = processor.getSampleRate();
                if (srate <= 0) srate = 44100.0;
                double lenSec = processor.sampleData.getNumFrames() / srate;
                g.drawText (fname + " (" + juce::String (lenSec, 2) + "s)",
                            x, row1y + 15, rightEdge - x, 14, juce::Justification::right);
            }

            sampleInfoBounds = { x, row1y, rightEdge - x, row1h };
        }

        // --- Separator line between rows ---
        g.setColour (getTheme().separator);
        g.drawHorizontalLine (32, 8.0f, (float) getWidth() - 8.0f);

        // --- Row 2: ATK / DEC / SUS / REL / PP / MUTE / STRETCH / VOL ---
        int row2y = 34;
        int row2h = 30;
        x = 8;

        // ATK
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("ATK", x, row2y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        float atk = processor.apvts.getRawParameterValue (ParamIds::defaultAttack)->load();
        g.drawText (juce::String ((int) atk) + "ms", x, row2y + 15, cellW, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, cellW, row2h, ParamIds::defaultAttack, 0.0f, 1000.0f, 1.0f, false, false, false, false });
        x += cellW + cellGap;

        // DEC
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("DEC", x, row2y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        float dec = processor.apvts.getRawParameterValue (ParamIds::defaultDecay)->load();
        g.drawText (juce::String ((int) dec) + "ms", x, row2y + 15, cellW, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, cellW, row2h, ParamIds::defaultDecay, 0.0f, 5000.0f, 1.0f, false, false, false, false });
        x += cellW + cellGap;

        // SUS
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("SUS", x, row2y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        float sus = processor.apvts.getRawParameterValue (ParamIds::defaultSustain)->load();
        g.drawText (juce::String ((int) sus) + "%", x, row2y + 15, cellW, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, cellW, row2h, ParamIds::defaultSustain, 0.0f, 100.0f, 1.0f, false, false, false, false });
        x += cellW + cellGap;

        // REL
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("REL", x, row2y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        float rel = processor.apvts.getRawParameterValue (ParamIds::defaultRelease)->load();
        g.drawText (juce::String ((int) rel) + "ms", x, row2y + 15, cellW, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, cellW, row2h, ParamIds::defaultRelease, 0.0f, 5000.0f, 1.0f, false, false, false, false });
        x += cellW + cellGap;

        // TAIL (release tail toggle)
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("TAIL", x, row2y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        bool tail = processor.apvts.getRawParameterValue (ParamIds::defaultReleaseTail)->load() > 0.5f;
        g.setColour (tail ? getTheme().lockActive : getTheme().foreground.withAlpha (0.5f));
        g.drawText (tail ? "ON" : "OFF", x, row2y + 15, cellW, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, cellW, row2h, ParamIds::defaultReleaseTail, 0.0f, 1.0f, 1.0f, false, true, false, false });
        x += cellW + cellGap;

        // REV (reverse toggle)
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("REV", x, row2y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        bool rev = processor.apvts.getRawParameterValue (ParamIds::defaultReverse)->load() > 0.5f;
        g.setColour (rev ? getTheme().lockActive : getTheme().foreground.withAlpha (0.5f));
        g.drawText (rev ? "ON" : "OFF", x, row2y + 15, cellW, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, cellW, row2h, ParamIds::defaultReverse, 0.0f, 1.0f, 1.0f, false, true, false, false });
        x += cellW + cellGap;

        // PP
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("PP", x, row2y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        bool pp = processor.apvts.getRawParameterValue (ParamIds::defaultPingPong)->load() > 0.5f;
        g.setColour (pp ? getTheme().lockActive : getTheme().foreground.withAlpha (0.5f));
        g.drawText (pp ? "ON" : "OFF", x, row2y + 15, cellW, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, cellW, row2h, ParamIds::defaultPingPong, 0.0f, 1.0f, 1.0f, false, true, false, false });
        x += cellW + cellGap;

        // MUTE GROUP
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("MUTE", x, row2y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        float muteGrp = processor.apvts.getRawParameterValue (ParamIds::defaultMuteGroup)->load();
        g.drawText (juce::String ((int) muteGrp), x, row2y + 15, cellW, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, cellW, row2h, ParamIds::defaultMuteGroup, 0.0f, 32.0f, 1.0f, false, false, false, false });
        x += cellW + cellGap;

        // STRETCH toggle
        int stretchW = 65;  // slightly wider for "STRETCH" label
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("STRETCH", x, row2y + 2, stretchW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        bool strOn = processor.apvts.getRawParameterValue (ParamIds::defaultStretchEnabled)->load() > 0.5f;
        g.setColour (strOn ? getTheme().accent : getTheme().foreground.withAlpha (0.5f));
        g.drawText (strOn ? "ON" : "OFF", x, row2y + 15, stretchW, 14, juce::Justification::centredLeft);
        headerCells.push_back ({ x, row2y, stretchW, row2h, ParamIds::defaultStretchEnabled, 0.0f, 1.0f, 1.0f, false, true, false, false });
        x += stretchW + cellGap;

        // GAIN (dB)
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (getTheme().foreground.withAlpha (0.9f));
        g.drawText ("GAIN", x, row2y + 2, cellW, 13, juce::Justification::centredLeft);
        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        float gainDb = processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load();
        {
            juce::String gainStr = (gainDb >= 0.0f ? "+" : "") + juce::String (gainDb, 1) + "dB";
            g.drawText (gainStr, x, row2y + 15, cellW, 14, juce::Justification::centredLeft);
        }
        headerCells.push_back ({ x, row2y, cellW, row2h, ParamIds::masterVolume, -100.0f, 24.0f, 0.1f, false, false, false, false });
        x += cellW + cellGap;

        // VOICES (right-aligned, like SLICES/ROOT)
        {
            int voicesW = 55;
            int voicesX = getWidth() - 8 - voicesW;
            g.setFont (IntersectLookAndFeel::makeFont (12.0f));
            g.setColour (getTheme().foreground.withAlpha (0.9f));
            g.drawText ("VOICES", voicesX, row2y + 2, voicesW, 13, juce::Justification::right);
            g.setFont (IntersectLookAndFeel::makeFont (14.0f));
            int maxV = (int) processor.apvts.getRawParameterValue (ParamIds::maxVoices)->load();
            g.setColour (getTheme().foreground.withAlpha (0.9f));
            g.drawText (juce::String (maxV), voicesX, row2y + 15, voicesW, 14, juce::Justification::right);
            headerCells.push_back ({ voicesX, row2y, voicesW, row2h, ParamIds::maxVoices, 1.0f, 32.0f, 1.0f, false, false, false, false });
        }
    }
    else if (processor.sampleMissing.load())
    {
        // Sample is missing — show MISSING indicator with filename
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (IntersectLookAndFeel::makeFont (18.0f, true));
        g.drawText ("INTERSECT", 8, 8, 160, 20, juce::Justification::centredLeft);

        int rightEdge = undoBtn.getX() - 6;
        g.setFont (IntersectLookAndFeel::makeFont (12.0f));
        g.setColour (juce::Colours::orange);
        g.drawText ("MISSING", 180, 2, rightEdge - 180, 13, juce::Justification::right);

        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        g.setColour (juce::Colours::orange.withAlpha (0.9f));
        juce::String fname = processor.sampleData.getFileName();
        g.drawText (fname + " (click to relink)", 180, 16, rightEdge - 180, 14, juce::Justification::right);

        sampleInfoBounds = { 180, 2, rightEdge - 180, 28 };
    }
    else
    {
        sampleInfoBounds = {};

        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (IntersectLookAndFeel::makeFont (18.0f, true));
        g.drawText ("INTERSECT", 8, 20, 160, 20, juce::Justification::centredLeft);

        g.setFont (IntersectLookAndFeel::makeFont (14.0f));
        g.setColour (getTheme().foreground.withAlpha (0.6f));
        g.drawText ("DROP AUDIO FILE", 180, 22, 300, 16,
                     juce::Justification::centredLeft);
    }
}

void HeaderBar::mouseDown (const juce::MouseEvent& e)
{
    if (textEditor != nullptr)
        textEditor.reset();

    activeDragCell = -1;
    auto pos = e.getPosition();

    // Click on sample info area opens file browser (relink if missing)
    if (sampleInfoBounds.contains (pos))
    {
        if (processor.sampleMissing.load())
            openRelinkBrowser();
        else
            openFileBrowser();
        return;
    }

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

            // Choice popup (Algorithm or Grain Mode)
            if (cell.isChoice)
            {
                juce::PopupMenu menu;
                if (cell.paramId == ParamIds::defaultGrainMode)
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
                auto* topLvl = getTopLevelComponent();
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                        .withParentComponent (topLvl),
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
            {
                IntersectProcessor::Command gestureCmd;
                gestureCmd.type = IntersectProcessor::CmdBeginGesture;
                processor.pushCommand (gestureCmd);
            }
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
    textEditor->setBounds (cell.x, cell.y + 12, cell.w, 18);
    textEditor->setFont (IntersectLookAndFeel::makeFont (14.0f));
    textEditor->setColour (juce::TextEditor::backgroundColourId, getTheme().header.brighter (0.2f));
    textEditor->setColour (juce::TextEditor::textColourId, juce::Colours::white);
    textEditor->setColour (juce::TextEditor::outlineColourId, getTheme().accent);

    float currentVal = processor.apvts.getRawParameterValue (cell.paramId)->load();
    juce::String displayVal;
    if (cell.paramId == ParamIds::masterVolume)
        displayVal = juce::String (currentVal, 1);
    else if (cell.paramId == ParamIds::defaultBpm)
        displayVal = juce::String (currentVal, 2);
    else if (cell.step >= 1.0f)
        displayVal = juce::String ((int) currentVal);
    else
        displayVal = juce::String (currentVal, 1);

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
    menu.addItem (1, "16 bars");
    menu.addItem (2, "8 bars");
    menu.addItem (3, "4 bars");
    menu.addItem (4, "2 bars");
    menu.addItem (5, "1 bar");
    menu.addItem (6, "1/2 note");
    menu.addItem (7, "1/4 note");
    menu.addItem (8, "1/8 note");
    menu.addItem (9, "1/16 note");

    auto* editor = getTopLevelComponent();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withParentComponent (editor),
        [this, forSampleDefault] (int result) {
            if (result <= 0 || result > 9) return;
            const float bars[] = { 0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };

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
                float newBpm = GrainEngine::calcStretchBpm (startS, endS, bars[result], processor.getSampleRate());
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

void HeaderBar::showThemePopup()
{
    auto* editor = dynamic_cast<IntersectEditor*> (getParentComponent());
    if (editor == nullptr)
        return;

    auto themes = editor->getAvailableThemes();
    auto currentName = getTheme().name;

    juce::PopupMenu menu;

    // Scale section
    float currentScale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
    menu.addSectionHeader ("Scale  " + juce::String (currentScale, 2) + "x");
    menu.addItem (100, "- 0.25");
    menu.addItem (101, "+ 0.25");
    menu.addSeparator();

    // Theme section
    menu.addSectionHeader ("Theme");
    for (int i = 0; i < themes.size(); ++i)
        menu.addItem (i + 1, themes[i], true, themes[i] == currentName);

    auto* topLevel = getTopLevelComponent();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&themeBtn)
                            .withParentComponent (topLevel),
        [this, editor, themes] (int result) {
            if (result == 100)
                adjustScale (-0.25f);
            else if (result == 101)
                adjustScale (0.25f);
            else if (result > 0 && result <= themes.size())
                editor->applyTheme (themes[result - 1]);
        });
}

void HeaderBar::openFileBrowser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load Audio File",
        juce::File(),
        "*.wav;*.ogg;*.aiff;*.flac;*.mp3");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile())
            {
                IntersectProcessor::Command cmd;
                cmd.type = IntersectProcessor::CmdLoadFile;
                cmd.fileParam = result;
                processor.pushCommand (cmd);
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
        "*.wav;*.ogg;*.aiff;*.flac;*.mp3");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile())
            {
                IntersectProcessor::Command cmd;
                cmd.type = IntersectProcessor::CmdRelinkFile;
                cmd.fileParam = result;
                processor.pushCommand (cmd);
            }
        });
}
