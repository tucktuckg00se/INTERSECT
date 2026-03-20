#include "SignalChainBar.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../audio/GrainEngine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
constexpr int kOuterPad = 0;
constexpr int kTopPad = 0;
constexpr int kContextHeight = 26;
constexpr int kSectionGap = 0;
constexpr int kModuleGap = 0;
constexpr int kModuleHeaderHeight = 16;
constexpr int kRowGap = 0;
constexpr int kContextTabGap = 0;
constexpr int kContextTextGap = 12;
constexpr int kCellInsetX = 3;
constexpr int kLabelYOffset = 0;
constexpr int kLabelHeight = 9;
constexpr int kValueYOffset = 9;
constexpr int kMinValueHeight = 13;

const juce::NormalisableRange<float>& getFilterCutoffDragRange()
{
    static const juce::NormalisableRange<float> range (20.0f, 20000.0f, 1.0f, 0.25f);
    return range;
}

struct RowCellSpec
{
    float weight = 1.0f;
    int gapAfter = 0;
};

juce::String formatTrimmed (float value, int decimals)
{
    juce::String text (value, decimals);
    if (text.containsChar ('.'))
    {
        while (text.endsWithChar ('0'))
            text = text.dropLastCharacters (1);
        if (text.endsWithChar ('.'))
            text = text.dropLastCharacters (1);
    }
    return text;
}

juce::String formatSigned (float value, int decimals, const juce::String& suffix)
{
    return (value >= 0.0f ? "+" : "") + formatTrimmed (value, decimals) + suffix;
}

juce::String formatBool (bool on)
{
    return on ? "ON" : "OFF";
}

juce::String formatMs (float valueMs)
{
    return formatTrimmed (valueMs, valueMs < 100.0f ? 1 : 0) + "ms";
}

juce::String formatPercent (float valuePercent, int decimals = 0)
{
    return formatTrimmed (valuePercent, decimals) + "%";
}

juce::String formatGain (float valueDb)
{
    return (valueDb >= 0.0f ? "+" : "") + formatTrimmed (valueDb, 1) + "dB";
}

juce::String formatHz (float valueHz)
{
    if (valueHz >= 1000.0f)
        return formatTrimmed (valueHz / 1000.0f, 2) + "k";
    return formatTrimmed (valueHz, 0);
}

juce::String getChoiceName (int value, const juce::StringArray& names)
{
    return names[juce::jlimit (0, names.size() - 1, value)];
}

juce::String midiNoteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 4);
}

juce::Rectangle<int> toIntBounds (juce::Rectangle<float> bounds)
{
    return bounds.getSmallestIntegerContainer();
}

constexpr float kBaseCellWidth = 46.0f;

std::vector<juce::Rectangle<int>> makeRowCells (juce::Rectangle<int> rowBounds,
                                                std::initializer_list<RowCellSpec> specs)
{
    std::vector<juce::Rectangle<int>> rects;
    if (specs.size() == 0)
        return rects;

    juce::FlexBox row;
    row.flexDirection = juce::FlexBox::Direction::row;
    row.flexWrap = juce::FlexBox::Wrap::noWrap;
    row.alignItems = juce::FlexBox::AlignItems::stretch;
    row.justifyContent = juce::FlexBox::JustifyContent::flexStart;

    std::vector<int> cellIndices;
    cellIndices.reserve (specs.size());
    size_t index = 0;
    for (const auto& spec : specs)
    {
        cellIndices.push_back (row.items.size());
        auto item = juce::FlexItem().withWidth (spec.weight * kBaseCellWidth).withMinWidth (14.0f);
        item.flexShrink = 1.0f;
        row.items.add (item);
        ++index;
        if (index != specs.size() && spec.gapAfter > 0)
            row.items.add (juce::FlexItem().withWidth ((float) spec.gapAfter));
    }

    row.performLayout (rowBounds.toFloat());

    for (int itemIndex : cellIndices)
        rects.push_back (toIntBounds (row.items[(int) itemIndex].currentBounds));

    return rects;
}

float measureTextWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getWidth();
}

void drawMiniButton (juce::Graphics& g,
                     juce::Rectangle<int> bounds,
                     const juce::String& text,
                     const juce::Colour& fill,
                     const juce::Colour& outline,
                     const juce::Colour& textColour,
                     float maxFontSize,
                     float minFontSize,
                     bool enabled)
{
    auto buttonFill = enabled ? fill : fill.withAlpha (fill.getFloatAlpha() * 0.35f);
    auto buttonOutline = enabled ? outline : outline.withAlpha (outline.getFloatAlpha() * 0.35f);
    auto buttonText = enabled ? textColour : textColour.withAlpha (textColour.getFloatAlpha() * 0.45f);

    IntersectLookAndFeel::drawShellButton (g, bounds.toFloat(), buttonFill, buttonOutline, 4.0f);
    g.setFont (IntersectLookAndFeel::fitFontToWidth (text, maxFontSize, minFontSize,
                                                     bounds.getWidth() - 8, true));
    g.setColour (buttonText);
    g.drawFittedText (text, bounds, juce::Justification::centred, 1);
}

std::array<uint32_t, 11> getModuleBits (SignalChainBar::Module module)
{
    switch (module)
    {
        case SignalChainBar::Module::Playback:
            return { kLockBpm, kLockPitch, kLockCentsDetune, kLockAlgorithm, kLockStretch,
                     kLockTonality, kLockFormant, kLockFormantComp, kLockGrainMode,
                     kLockOneShot, 0u };
        case SignalChainBar::Module::Filter:
            return { kLockFilterEnabled, kLockFilterType, kLockFilterSlope, kLockFilterCutoff,
                     kLockFilterReso, kLockFilterDrive, kLockFilterKeyTrack,
                     kLockFilterEnvAttack, kLockFilterEnvDecay, kLockFilterEnvSustain,
                     kLockFilterEnvRelease };
        case SignalChainBar::Module::Amp:
            return { kLockAttack, kLockDecay, kLockSustain, kLockRelease, kLockReleaseTail,
                     0u, 0u, 0u, 0u, 0u, 0u };
        case SignalChainBar::Module::Output:
            return { kLockReverse, kLockLoop, kLockMuteGroup, kLockVolume, kLockOutputBus,
                     0u, 0u, 0u, 0u, 0u, 0u };
    }

    return {};
}
} // namespace

SignalChainBar::SignalChainBar (IntersectProcessor& p) : processor (p)
{
}

void SignalChainBar::syncScopeFromSelection()
{
    const auto& ui = processor.getUiSliceSnapshot();
    const bool hasValidSlice = ui.selectedSlice >= 0 && ui.selectedSlice < ui.numSlices;

    if (! hasValidSlice)
        scope = Scope::Global;
    else if (! lastHadValidSlice)
        scope = Scope::Slice;

    lastHadValidSlice = hasValidSlice;
}

bool SignalChainBar::isSliceScopeActive() const
{
    const auto& ui = processor.getUiSliceSnapshot();
    return scope == Scope::Slice && ui.selectedSlice >= 0 && ui.selectedSlice < ui.numSlices;
}

float SignalChainBar::storedToDisplay (const Cell& cell, float storedValue) const
{
    return storedValue * cell.displayScale + cell.displayOffset;
}

float SignalChainBar::displayToStored (const Cell& cell, float displayValue) const
{
    if (std::abs (cell.displayScale) < 1.0e-6f)
        return displayValue;
    return (displayValue - cell.displayOffset) / cell.displayScale;
}

float SignalChainBar::clampStoredValue (const Cell& cell, float storedValue) const
{
    return juce::jlimit (cell.minVal, cell.maxVal, storedValue);
}

float SignalChainBar::clampDisplayValue (const Cell& cell, float displayValue) const
{
    return storedToDisplay (cell, clampStoredValue (cell, displayToStored (cell, displayValue)));
}

float SignalChainBar::storedToInteraction (const Cell& cell, float storedValue) const
{
    if (cell.dragMapping == DragMapping::FilterCutoff)
        return getFilterCutoffDragRange().convertTo0to1 (clampStoredValue (cell, storedValue));

    return storedToDisplay (cell, storedValue);
}

float SignalChainBar::interactionToStored (const Cell& cell, float interactionValue) const
{
    if (cell.dragMapping == DragMapping::FilterCutoff)
        return getFilterCutoffDragRange().convertFrom0to1 (juce::jlimit (0.0f, 1.0f, interactionValue));

    return displayToStored (cell, interactionValue);
}

float SignalChainBar::clampInteractionValue (const Cell& cell, float interactionValue) const
{
    if (cell.dragMapping == DragMapping::FilterCutoff)
        return juce::jlimit (0.0f, 1.0f, interactionValue);

    return clampDisplayValue (cell, interactionValue);
}

void SignalChainBar::addTabCell (const juce::Rectangle<int>& bounds,
                                 const juce::String& text,
                                 TabTarget target,
                                 bool isActive,
                                 bool isEnabled)
{
    Cell cell;
    cell.kind = CellKind::Tab;
    cell.tabTarget = target;
    cell.bounds = bounds;
    cell.valueText = text;
    cell.isActive = isActive;
    cell.isEnabled = isEnabled;
    cells.push_back (cell);
}

void SignalChainBar::addParamCell (const Cell& cell)
{
    cells.push_back (cell);
}

int SignalChainBar::countModuleOverrides (const ModuleLayout& module, uint32_t lockMask) const
{
    const auto bits = getModuleBits (module.module);
    int count = (int) std::count_if (bits.begin(), bits.end(),
                                     [lockMask] (uint32_t bit)
                                     {
                                         return bit != 0 && (lockMask & bit) != 0;
                                     });
    if (module.module == Module::Filter && (lockMask & kLockFilterEnvAmount) != 0)
        ++count;
    return count;
}

int SignalChainBar::countEffectiveModuleOverrides (Module module, const Slice& slice) const
{
    if ((slice.lockMask) == 0)
        return 0;

    auto checkBool = [&] (uint32_t bit, bool sliceVal, const juce::String& paramId) -> int
    {
        if ((slice.lockMask & bit) == 0) return 0;
        bool globalVal = processor.apvts.getRawParameterValue (paramId)->load() > 0.5f;
        return (sliceVal != globalVal) ? 1 : 0;
    };

    auto checkInt = [&] (uint32_t bit, int sliceVal, const juce::String& paramId) -> int
    {
        if ((slice.lockMask & bit) == 0) return 0;
        int globalVal = (int) processor.apvts.getRawParameterValue (paramId)->load();
        return (sliceVal != globalVal) ? 1 : 0;
    };

    auto checkFloat = [&] (uint32_t bit, float sliceVal, const juce::String& paramId, float scale = 1.0f) -> int
    {
        if ((slice.lockMask & bit) == 0) return 0;
        float globalVal = processor.apvts.getRawParameterValue (paramId)->load() * scale;
        return (std::abs (sliceVal - globalVal) > 1e-4f) ? 1 : 0;
    };

    int count = 0;

    switch (module)
    {
        case Module::Playback:
            count += checkFloat (kLockBpm, slice.bpm, ParamIds::defaultBpm);
            count += checkFloat (kLockPitch, slice.pitchSemitones, ParamIds::defaultPitch);
            count += checkFloat (kLockCentsDetune, slice.centsDetune, ParamIds::defaultCentsDetune);
            count += checkInt (kLockAlgorithm, slice.algorithm, ParamIds::defaultAlgorithm);
            count += checkBool (kLockStretch, slice.stretchEnabled, ParamIds::defaultStretchEnabled);
            count += checkFloat (kLockTonality, slice.tonalityHz, ParamIds::defaultTonality);
            count += checkFloat (kLockFormant, slice.formantSemitones, ParamIds::defaultFormant);
            count += checkBool (kLockFormantComp, slice.formantComp, ParamIds::defaultFormantComp);
            count += checkInt (kLockGrainMode, slice.grainMode, ParamIds::defaultGrainMode);
            count += checkBool (kLockOneShot, slice.oneShot, ParamIds::defaultOneShot);
            break;

        case Module::Filter:
            count += checkBool (kLockFilterEnabled, slice.filterEnabled, ParamIds::defaultFilterEnabled);
            count += checkInt (kLockFilterType, slice.filterType, ParamIds::defaultFilterType);
            count += checkInt (kLockFilterSlope, slice.filterSlope, ParamIds::defaultFilterSlope);
            count += checkFloat (kLockFilterCutoff, slice.filterCutoff, ParamIds::defaultFilterCutoff);
            count += checkFloat (kLockFilterReso, slice.filterReso, ParamIds::defaultFilterReso);
            count += checkFloat (kLockFilterDrive, slice.filterDrive, ParamIds::defaultFilterDrive);
            count += checkFloat (kLockFilterKeyTrack, slice.filterKeyTrack, ParamIds::defaultFilterKeyTrack);
            count += checkFloat (kLockFilterEnvAttack, slice.filterEnvAttackSec, ParamIds::defaultFilterEnvAttack, 1.0f / 1000.0f);
            count += checkFloat (kLockFilterEnvDecay, slice.filterEnvDecaySec, ParamIds::defaultFilterEnvDecay, 1.0f / 1000.0f);
            count += checkFloat (kLockFilterEnvSustain, slice.filterEnvSustain, ParamIds::defaultFilterEnvSustain, 1.0f / 100.0f);
            count += checkFloat (kLockFilterEnvRelease, slice.filterEnvReleaseSec, ParamIds::defaultFilterEnvRelease, 1.0f / 1000.0f);
            count += checkFloat (kLockFilterEnvAmount, slice.filterEnvAmount, ParamIds::defaultFilterEnvAmount);
            break;

        case Module::Amp:
            count += checkFloat (kLockAttack, slice.attackSec, ParamIds::defaultAttack, 1.0f / 1000.0f);
            count += checkFloat (kLockDecay, slice.decaySec, ParamIds::defaultDecay, 1.0f / 1000.0f);
            count += checkFloat (kLockSustain, slice.sustainLevel, ParamIds::defaultSustain, 1.0f / 100.0f);
            count += checkFloat (kLockRelease, slice.releaseSec, ParamIds::defaultRelease, 1.0f / 1000.0f);
            count += checkBool (kLockReleaseTail, slice.releaseTail, ParamIds::defaultReleaseTail);
            break;

        case Module::Output:
            count += checkBool (kLockReverse, slice.reverse, ParamIds::defaultReverse);
            count += checkInt (kLockLoop, slice.loopMode, ParamIds::defaultLoop);
            count += checkInt (kLockMuteGroup, slice.muteGroup, ParamIds::defaultMuteGroup);
            count += checkFloat (kLockVolume, slice.volume, ParamIds::masterVolume);
            if ((slice.lockMask & kLockOutputBus) != 0)
                ++count; // no global equivalent
            break;
    }

    return count;
}

int SignalChainBar::countAllOverrides (uint32_t lockMask) const
{
    int count = 0;
    for (auto moduleId : { Module::Playback, Module::Filter, Module::Amp, Module::Output })
    {
        ModuleLayout module;
        module.module = moduleId;
        count += countModuleOverrides (module, lockMask);
    }
    return count;
}

int SignalChainBar::countAllEffectiveOverrides (const Slice& slice) const
{
    int count = 0;
    for (auto moduleId : { Module::Playback, Module::Filter, Module::Amp, Module::Output })
        count += countEffectiveModuleOverrides (moduleId, slice);
    return count;
}

void SignalChainBar::rebuildLayout()
{
    cells.clear();
    syncScopeFromSelection();

    const auto& ui = processor.getUiSliceSnapshot();
    const bool hasValidSlice = ui.selectedSlice >= 0 && ui.selectedSlice < ui.numSlices;
    const bool sliceScope = isSliceScopeActive();
    const Slice* selectedSlice = hasValidSlice ? &ui.slices[(size_t) ui.selectedSlice] : nullptr;

    const float sampleRate = processor.getSampleRate() > 0.0 ? (float) processor.getSampleRate() : 44100.0f;

    contextTitle.clear();
    contextSubtitle.clear();
    contextStatus.clear();
    contextStatusBounds = {};
    contextDot1Bounds = {};
    contextDot2Bounds = {};

    auto area = getLocalBounds().reduced (kOuterPad, kTopPad);
    juce::FlexBox shell;
    shell.flexDirection = juce::FlexBox::Direction::column;
    shell.flexWrap = juce::FlexBox::Wrap::noWrap;
    shell.items.add (juce::FlexItem().withFlex (1.0f));
    if (kSectionGap > 0)
        shell.items.add (juce::FlexItem().withHeight ((float) kSectionGap));
    shell.items.add (juce::FlexItem().withHeight ((float) kContextHeight));
    shell.performLayout (area.toFloat());

    moduleStripBounds = toIntBounds (shell.items[0].currentBounds);
    contextBounds = toIntBounds (shell.items[shell.items.size() - 1].currentBounds);

    const int globalTabWidth = 56;
    const juce::String sliceTabText = hasValidSlice ? "SLICE " + juce::String (ui.selectedSlice + 1) : "SLICE";
    const int sliceTabWidth = hasValidSlice ? 76 : 56;
    juce::FlexBox contextRow;
    contextRow.flexDirection = juce::FlexBox::Direction::row;
    contextRow.flexWrap = juce::FlexBox::Wrap::noWrap;
    contextRow.alignItems = juce::FlexBox::AlignItems::stretch;

    contextRow.items.add (juce::FlexItem().withWidth ((float) globalTabWidth));
    contextRow.items.add (juce::FlexItem().withWidth ((float) kContextTabGap));
    contextRow.items.add (juce::FlexItem().withWidth ((float) sliceTabWidth));
    contextRow.items.add (juce::FlexItem().withWidth ((float) kContextTextGap));

    if (sliceScope && selectedSlice != nullptr)
    {
        // Time · Note · MIDI layout with fixed-width items
        const int timeItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (42.0f));  // time
        const int dot1ItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (10.0f));  // dot 1
        const int noteItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (24.0f));  // note name
        const int dot2ItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (10.0f));  // dot 2
        const int midiItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (50.0f));  // MIDI number
        contextRow.items.add (juce::FlexItem().withFlex (1.0f));    // spacer
        const int statusItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (90.0f));  // status (with right margin)

        contextRow.performLayout (contextBounds.toFloat());

        addTabCell (toIntBounds (contextRow.items[0].currentBounds), "GLOBAL", TabTarget::Global, ! sliceScope, true);
        addTabCell (toIntBounds (contextRow.items[2].currentBounds), sliceTabText, TabTarget::Slice, sliceScope, hasValidSlice);

        contextInfoBounds = toIntBounds (contextRow.items[timeItemIndex].currentBounds);
        contextDot1Bounds = toIntBounds (contextRow.items[dot1ItemIndex].currentBounds);
        contextDot2Bounds = toIntBounds (contextRow.items[dot2ItemIndex].currentBounds);
        contextStatusBounds = toIntBounds (contextRow.items[statusItemIndex].currentBounds);

        const int overrideCount = countAllEffectiveOverrides (*selectedSlice);
        const float lenSec = (float) (selectedSlice->endSample - selectedSlice->startSample) / sampleRate;
        contextTitle = formatTrimmed (lenSec, 2) + "s";
        contextSubtitle.clear();
        contextStatus = overrideCount > 0 ? juce::String (overrideCount) + " overrides" : juce::String();

        // Note name cell (e.g. "D2") — interactive
        Cell noteCell;
        noteCell.module = Module::Playback;
        noteCell.bounds = toIntBounds (contextRow.items[noteItemIndex].currentBounds);
        noteCell.label = "";
        noteCell.valueText = midiNoteName (selectedSlice->midiNote);
        noteCell.isContextInline = true;
        noteCell.fieldId = IntersectProcessor::FieldMidiNote;
        noteCell.currentValue = (float) selectedSlice->midiNote;
        noteCell.minVal = 0.0f;
        noteCell.maxVal = 127.0f;
        noteCell.step = 1.0f;
        noteCell.dragPerPixel = 0.1f;
        addParamCell (noteCell);

        // MIDI number cell (e.g. "MIDI 38") — interactive
        Cell midiCell;
        midiCell.module = Module::Playback;
        midiCell.bounds = toIntBounds (contextRow.items[midiItemIndex].currentBounds);
        midiCell.label = "MIDI";
        midiCell.valueText = juce::String (selectedSlice->midiNote);
        midiCell.isContextInline = true;
        midiCell.fieldId = IntersectProcessor::FieldMidiNote;
        midiCell.currentValue = (float) selectedSlice->midiNote;
        midiCell.minVal = 0.0f;
        midiCell.maxVal = 127.0f;
        midiCell.step = 1.0f;
        midiCell.dragPerPixel = 0.1f;
        addParamCell (midiCell);
    }
    else
    {
        const int infoItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withFlex (1.0f).withMinWidth (90.0f));

        contextRow.performLayout (contextBounds.toFloat());

        addTabCell (toIntBounds (contextRow.items[0].currentBounds), "GLOBAL", TabTarget::Global, ! sliceScope, true);
        addTabCell (toIntBounds (contextRow.items[2].currentBounds), sliceTabText, TabTarget::Slice, sliceScope, hasValidSlice);

        contextInfoBounds = toIntBounds (contextRow.items[infoItemIndex].currentBounds);

        contextTitle = "GLOBAL DEFAULTS";
        if (ui.sampleLoaded)
            contextSubtitle = formatTrimmed ((float) ui.sampleNumFrames / sampleRate, 2) + "s SAMPLE";
        else if (ui.sampleMissing)
            contextSubtitle = "MISSING SAMPLE, RELINK REQUIRED";
        else if (hasValidSlice)
            contextSubtitle = "SLICE SELECTED, EDITS STAY GLOBAL";
        else
            contextSubtitle = "NO SLICE SELECTED";
    }

    auto moduleArea = moduleStripBounds;
    const std::array<float, 4> moduleWeights { 2.2f, 2.0f, 1.0f, 1.1f };
    std::array<juce::String, 4> names { "PLAYBACK", "FILTER", "AMP", "OUTPUT" };
    std::array<juce::Colour, 4> colours {
        getTheme().moduleNamePlayback,
        getTheme().moduleNameFilter,
        getTheme().moduleNameAmp,
        getTheme().moduleNameOutput
    };

    juce::FlexBox moduleRow;
    moduleRow.flexDirection = juce::FlexBox::Direction::row;
    moduleRow.flexWrap = juce::FlexBox::Wrap::noWrap;
    moduleRow.alignItems = juce::FlexBox::AlignItems::stretch;
    std::array<int, 4> moduleItemIndices {};
    for (int i = 0; i < 4; ++i)
    {
        moduleItemIndices[(size_t) i] = moduleRow.items.size();
        moduleRow.items.add (juce::FlexItem().withFlex (moduleWeights[(size_t) i]).withMinWidth (68.0f));
        if (i < 3)
            moduleRow.items.add (juce::FlexItem().withWidth ((float) kModuleGap));
    }
    moduleRow.performLayout (moduleArea.toFloat());

    for (int i = 0; i < 4; ++i)
    {
        auto& module = modules[(size_t) i];
        module.module = (Module) i;
        module.name = names[(size_t) i];
        module.titleColour = colours[(size_t) i];
        module.bounds = toIntBounds (moduleRow.items[moduleItemIndices[(size_t) i]].currentBounds);

        auto moduleInner = module.bounds;
        moduleInner.removeFromTop (4);
        moduleInner.removeFromBottom (6);

        juce::FlexBox moduleColumn;
        moduleColumn.flexDirection = juce::FlexBox::Direction::column;
        moduleColumn.flexWrap = juce::FlexBox::Wrap::noWrap;
        moduleColumn.items.add (juce::FlexItem().withHeight ((float) kModuleHeaderHeight));
        moduleColumn.items.add (juce::FlexItem().withFlex (1.0f));
        moduleColumn.performLayout (moduleInner.toFloat());

        module.headerBounds = toIntBounds (moduleColumn.items[0].currentBounds);
        module.headerBounds.removeFromLeft (8);
        module.headerBounds.removeFromRight (8);
        module.bodyBounds = toIntBounds (moduleColumn.items[1].currentBounds);
        module.bodyBounds.removeFromLeft (8);
        module.bodyBounds.removeFromRight (8);
        module.overrideCount = sliceScope && selectedSlice != nullptr
            ? countEffectiveModuleOverrides (module.module, *selectedSlice)
            : 0;
    }

    const float gBpm = processor.apvts.getRawParameterValue (ParamIds::defaultBpm)->load();
    const float gPitch = processor.apvts.getRawParameterValue (ParamIds::defaultPitch)->load();
    const float gCents = processor.apvts.getRawParameterValue (ParamIds::defaultCentsDetune)->load();
    const int gAlgo = (int) processor.apvts.getRawParameterValue (ParamIds::defaultAlgorithm)->load();
    const float gAttackMs = processor.apvts.getRawParameterValue (ParamIds::defaultAttack)->load();
    const float gDecayMs = processor.apvts.getRawParameterValue (ParamIds::defaultDecay)->load();
    const float gSustainPct = processor.apvts.getRawParameterValue (ParamIds::defaultSustain)->load();
    const float gReleaseMs = processor.apvts.getRawParameterValue (ParamIds::defaultRelease)->load();
    const int gMuteGroup = (int) processor.apvts.getRawParameterValue (ParamIds::defaultMuteGroup)->load();
    const bool gStretch = processor.apvts.getRawParameterValue (ParamIds::defaultStretchEnabled)->load() > 0.5f;
    const bool gReverse = processor.apvts.getRawParameterValue (ParamIds::defaultReverse)->load() > 0.5f;
    const int gLoop = (int) processor.apvts.getRawParameterValue (ParamIds::defaultLoop)->load();
    const bool gOneShot = processor.apvts.getRawParameterValue (ParamIds::defaultOneShot)->load() > 0.5f;
    const bool gTail = processor.apvts.getRawParameterValue (ParamIds::defaultReleaseTail)->load() > 0.5f;
    const float gTonality = processor.apvts.getRawParameterValue (ParamIds::defaultTonality)->load();
    const float gFormant = processor.apvts.getRawParameterValue (ParamIds::defaultFormant)->load();
    const bool gFormantComp = processor.apvts.getRawParameterValue (ParamIds::defaultFormantComp)->load() > 0.5f;
    const int gGrain = (int) processor.apvts.getRawParameterValue (ParamIds::defaultGrainMode)->load();
    const float gGain = processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load();
    const int gVoices = (int) processor.apvts.getRawParameterValue (ParamIds::maxVoices)->load();
    const bool gFilterEnabled = processor.apvts.getRawParameterValue (ParamIds::defaultFilterEnabled)->load() > 0.5f;
    const int gFilterType = (int) processor.apvts.getRawParameterValue (ParamIds::defaultFilterType)->load();
    const int gFilterSlope = (int) processor.apvts.getRawParameterValue (ParamIds::defaultFilterSlope)->load();
    const float gFilterCutoff = processor.apvts.getRawParameterValue (ParamIds::defaultFilterCutoff)->load();
    const float gFilterReso = processor.apvts.getRawParameterValue (ParamIds::defaultFilterReso)->load();
    const float gFilterDrive = processor.apvts.getRawParameterValue (ParamIds::defaultFilterDrive)->load();
    const float gFilterKey = processor.apvts.getRawParameterValue (ParamIds::defaultFilterKeyTrack)->load();
    const float gFilterAtkSec = processor.apvts.getRawParameterValue (ParamIds::defaultFilterEnvAttack)->load() / 1000.0f;
    const float gFilterDecSec = processor.apvts.getRawParameterValue (ParamIds::defaultFilterEnvDecay)->load() / 1000.0f;
    const float gFilterSus = processor.apvts.getRawParameterValue (ParamIds::defaultFilterEnvSustain)->load() / 100.0f;
    const float gFilterRelSec = processor.apvts.getRawParameterValue (ParamIds::defaultFilterEnvRelease)->load() / 1000.0f;
    const float gFilterAmt = processor.apvts.getRawParameterValue (ParamIds::defaultFilterEnvAmount)->load();

    auto resolveFloat = [selectedSlice] (uint32_t bit, float sliceValue, float globalValue)
    {
        const bool locked = selectedSlice != nullptr && (selectedSlice->lockMask & bit) != 0;
        return std::pair<float, bool> { locked ? sliceValue : globalValue, locked };
    };

    auto resolveInt = [selectedSlice] (uint32_t bit, int sliceValue, int globalValue)
    {
        const bool locked = selectedSlice != nullptr && (selectedSlice->lockMask & bit) != 0;
        return std::pair<int, bool> { locked ? sliceValue : globalValue, locked };
    };

    auto resolveBool = [selectedSlice] (uint32_t bit, bool sliceValue, bool globalValue)
    {
        const bool locked = selectedSlice != nullptr && (selectedSlice->lockMask & bit) != 0;
        return std::pair<bool, bool> { locked ? sliceValue : globalValue, locked };
    };

    const auto algoNames = juce::StringArray { "Repitch", "Stretch", "Bungee" };
    const auto grainNames = juce::StringArray { "Fast", "Normal", "Smooth" };
    const auto loopNames = juce::StringArray { "Off", "Loop", "PP" };
    const auto filterTypeNames = juce::StringArray { "LP", "HP", "BP", "NT" };
    const auto filterSlopeNames = juce::StringArray { "12dB", "24dB" };

    auto moduleRows = [] (const ModuleLayout& module)
    {
        juce::FlexBox rows;
        rows.flexDirection = juce::FlexBox::Direction::column;
        rows.flexWrap = juce::FlexBox::Wrap::noWrap;
        rows.items.add (juce::FlexItem().withFlex (1.0f).withMinHeight (18.0f));
        if (kRowGap > 0)
            rows.items.add (juce::FlexItem().withHeight ((float) kRowGap));
        rows.items.add (juce::FlexItem().withFlex (1.0f).withMinHeight (18.0f));
        rows.performLayout (module.bodyBounds.toFloat());
        return std::pair<juce::Rectangle<int>, juce::Rectangle<int>> {
            toIntBounds (rows.items[0].currentBounds),
            toIntBounds (rows.items[rows.items.size() - 1].currentBounds)
        };
    };

    const auto playbackRows = moduleRows (modules[0]);
    const auto filterRows = moduleRows (modules[1]);
    const auto ampRows = moduleRows (modules[2]);
    const auto outputRows = moduleRows (modules[3]);

    const auto playbackRow1 = makeRowCells (playbackRows.first, {
        { 1.0f, 8 }, { 0.92f, 8 }, { 0.9f, 0 }
    });
    const auto filterRow1 = makeRowCells (filterRows.first, {
        { 0.82f, 6 }, { 0.9f, 10 }, { 1.18f, 6 }, { 0.96f, 10 }, { 0.94f, 6 }, { 0.84f, 0 }
    });
    const auto filterRow2 = makeRowCells (filterRows.second, {
        { 0.92f, 6 }, { 0.92f, 6 }, { 0.82f, 6 }, { 0.95f, 14 }, { 1.06f, 0 }
    });
    const auto ampRow1 = makeRowCells (ampRows.first, {
        { 1.0f, 10 }, { 1.0f, 10 }, { 0.92f, 0 }
    });
    const auto ampRow2 = makeRowCells (ampRows.second, {
        { 1.02f, 12 }, { 0.82f, 0 }
    });
    const auto outputRow1 = makeRowCells (outputRows.first, {
        { 0.78f, 10 }, { 0.94f, 10 }, { 0.84f, 0 }
    });
    const auto outputRow2 = makeRowCells (outputRows.second, {
        { 1.0f, 12 }, { 0.96f, 0 }
    });

    {
        Cell setBpmCell;
        setBpmCell.kind = CellKind::SetBpm;
        setBpmCell.module = Module::Playback;
        auto nameFont = IntersectLookAndFeel::fitFontToWidth (
            modules[0].name, 8.5f, 7.2f, modules[0].headerBounds.getWidth() - 6, true);
        int nameW = (int) std::ceil (measureTextWidth (nameFont, modules[0].name));
        setBpmCell.bounds = juce::Rectangle<int> (
            modules[0].headerBounds.getX() + nameW + 5,
            modules[0].headerBounds.getY() + 2,
            38, modules[0].headerBounds.getHeight() - 4);
        setBpmCell.valueText = "SET BPM";
        setBpmCell.isEnabled = sliceScope ? hasValidSlice : ui.sampleLoaded;
        addParamCell (setBpmCell);
    }

    const auto [resolvedBpm, bpmLocked] = sliceScope ? resolveFloat (kLockBpm, selectedSlice->bpm, gBpm)
                                                     : std::pair<float, bool> { gBpm, false };
    const auto [resolvedPitch, pitchLocked] = sliceScope ? resolveFloat (kLockPitch, selectedSlice->pitchSemitones, gPitch)
                                                         : std::pair<float, bool> { gPitch, false };
    const auto [resolvedCents, centsLocked] = sliceScope ? resolveFloat (kLockCentsDetune, selectedSlice->centsDetune, gCents)
                                                         : std::pair<float, bool> { gCents, false };
    const auto [resolvedAlgo, algoLocked] = sliceScope ? resolveInt (kLockAlgorithm, selectedSlice->algorithm, gAlgo)
                                                       : std::pair<int, bool> { gAlgo, false };
    const auto [resolvedStretch, stretchLocked] = sliceScope ? resolveBool (kLockStretch, selectedSlice->stretchEnabled, gStretch)
                                                             : std::pair<bool, bool> { gStretch, false };
    const auto playbackRow2 = resolvedAlgo == 1
        ? makeRowCells (playbackRows.second, {
            { 1.45f, 10 }, { 0.96f, 5 }, { 0.92f, 5 }, { 0.8f, 10 }, { 1.04f, 8 }, { 0.74f, 0 }
        })
        : (resolvedAlgo == 2
            ? makeRowCells (playbackRows.second, {
                { 1.46f, 10 }, { 1.06f, 10 }, { 1.06f, 8 }, { 0.78f, 0 }
            })
            : makeRowCells (playbackRows.second, {
                { 1.46f, 10 }, { 1.18f, 8 }, { 0.82f, 0 }
            }));
    const bool repitchStretch = resolvedAlgo == 0 && resolvedStretch;
    const float dawBpm = processor.dawBpm.load (std::memory_order_relaxed);
    const float derivedSemis = (repitchStretch && dawBpm > 0.0f && resolvedBpm > 0.0f)
        ? 12.0f * std::log2 (dawBpm / resolvedBpm)
        : 0.0f;
    const float displayPitch = repitchStretch ? derivedSemis : resolvedPitch;
    const float displayCents = repitchStretch
        ? (derivedSemis - std::round (derivedSemis)) * 100.0f
        : resolvedCents;

    Cell cell;
    cell.module = Module::Playback;
    cell.globalParamId = ParamIds::defaultBpm;
    cell.fieldId = IntersectProcessor::FieldBpm;
    cell.lockBit = kLockBpm;
    cell.currentValue = resolvedBpm;
    cell.minVal = 20.0f;
    cell.maxVal = 999.0f;
    cell.step = 0.01f;
    cell.dragPerPixel = 0.25f;
    cell.textDecimals = 2;
    cell.isLocked = bpmLocked;
    cell.bounds = playbackRow1[0];
    cell.label = "BPM";
    cell.valueText = formatTrimmed (resolvedBpm, 2);
    cell.drawTrailingDivider = true;
    addParamCell (cell);

    cell = {};
    cell.module = Module::Playback;
    cell.globalParamId = ParamIds::defaultPitch;
    cell.fieldId = IntersectProcessor::FieldPitch;
    cell.lockBit = kLockPitch;
    cell.currentValue = displayPitch;
    cell.minVal = -48.0f;
    cell.maxVal = 48.0f;
    cell.step = 0.01f;
    cell.dragPerPixel = 0.1f;
    cell.textDecimals = 2;
    cell.isLocked = pitchLocked;
    cell.isReadOnly = repitchStretch;
    cell.bounds = playbackRow1[1];
    cell.label = "PITCH";
    cell.valueText = formatSigned (displayPitch, 2, "st");
    cell.drawTrailingDivider = true;
    addParamCell (cell);

    cell = {};
    cell.module = Module::Playback;
    cell.globalParamId = ParamIds::defaultCentsDetune;
    cell.fieldId = IntersectProcessor::FieldCentsDetune;
    cell.lockBit = kLockCentsDetune;
    cell.currentValue = displayCents;
    cell.minVal = -100.0f;
    cell.maxVal = 100.0f;
    cell.step = 0.1f;
    cell.dragPerPixel = 1.0f;
    cell.textDecimals = 1;
    cell.isLocked = centsLocked;
    cell.isReadOnly = repitchStretch;
    cell.bounds = playbackRow1[2];
    cell.label = "TUNE";
    cell.valueText = formatSigned (displayCents, 1, "ct");
    cell.drawTrailingDivider = false;
    addParamCell (cell);

    cell = {};
    cell.module = Module::Playback;
    cell.globalParamId = ParamIds::defaultAlgorithm;
    cell.fieldId = IntersectProcessor::FieldAlgorithm;
    cell.lockBit = kLockAlgorithm;
    cell.currentValue = (float) resolvedAlgo;
    cell.minVal = 0.0f;
    cell.maxVal = 2.0f;
    cell.step = 1.0f;
    cell.choiceCount = 3;
    cell.textDecimals = 0;
    cell.isChoice = true;
    cell.isLocked = algoLocked;
    cell.bounds = playbackRow2[0];
    cell.label = "ALGO";
    cell.valueText = getChoiceName (resolvedAlgo, algoNames);
    cell.drawTrailingDivider = true;
    addParamCell (cell);

    if (resolvedAlgo == 1)
    {
        const auto [tonality, tonalityLocked] = sliceScope ? resolveFloat (kLockTonality, selectedSlice->tonalityHz, gTonality)
                                                           : std::pair<float, bool> { gTonality, false };
        const auto [formant, formantLocked] = sliceScope ? resolveFloat (kLockFormant, selectedSlice->formantSemitones, gFormant)
                                                         : std::pair<float, bool> { gFormant, false };
        const auto [formantComp, formantCompLocked] = sliceScope ? resolveBool (kLockFormantComp, selectedSlice->formantComp, gFormantComp)
                                                                 : std::pair<bool, bool> { gFormantComp, false };

        cell = {};
        cell.module = Module::Playback;
        cell.globalParamId = ParamIds::defaultTonality;
        cell.fieldId = IntersectProcessor::FieldTonality;
        cell.lockBit = kLockTonality;
        cell.currentValue = tonality;
        cell.minVal = 0.0f;
        cell.maxVal = 8000.0f;
        cell.step = 1.0f;
        cell.dragPerPixel = 20.0f;
        cell.textDecimals = 0;
        cell.isLocked = tonalityLocked;
        cell.bounds = playbackRow2[1];
        cell.label = "TONAL";
        cell.valueText = formatHz (tonality);
        cell.drawTrailingDivider = true;
        addParamCell (cell);

        cell = {};
        cell.module = Module::Playback;
        cell.globalParamId = ParamIds::defaultFormant;
        cell.fieldId = IntersectProcessor::FieldFormant;
        cell.lockBit = kLockFormant;
        cell.currentValue = formant;
        cell.minVal = -24.0f;
        cell.maxVal = 24.0f;
        cell.step = 0.1f;
        cell.dragPerPixel = 0.1f;
        cell.textDecimals = 1;
        cell.isLocked = formantLocked;
        cell.bounds = playbackRow2[2];
        cell.label = "FMNT";
        cell.valueText = formatSigned (formant, 1, "st");
        cell.drawTrailingDivider = true;
        addParamCell (cell);

        cell = {};
        cell.module = Module::Playback;
        cell.globalParamId = ParamIds::defaultFormantComp;
        cell.fieldId = IntersectProcessor::FieldFormantComp;
        cell.lockBit = kLockFormantComp;
        cell.currentValue = formantComp ? 1.0f : 0.0f;
        cell.minVal = 0.0f;
        cell.maxVal = 1.0f;
        cell.step = 1.0f;
        cell.textDecimals = 0;
        cell.isBoolean = true;
        cell.isLocked = formantCompLocked;
        cell.bounds = playbackRow2[3];
        cell.label = "FMNT C";
        cell.valueText = formatBool (formantComp);
        cell.drawTrailingDivider = true;
        addParamCell (cell);
    }
    else if (resolvedAlgo == 2)
    {
        const auto [grainMode, grainLocked] = sliceScope ? resolveInt (kLockGrainMode, selectedSlice->grainMode, gGrain)
                                                         : std::pair<int, bool> { gGrain, false };

        cell = {};
        cell.module = Module::Playback;
        cell.globalParamId = ParamIds::defaultGrainMode;
        cell.fieldId = IntersectProcessor::FieldGrainMode;
        cell.lockBit = kLockGrainMode;
        cell.currentValue = (float) grainMode;
        cell.minVal = 0.0f;
        cell.maxVal = 2.0f;
        cell.step = 1.0f;
        cell.choiceCount = 3;
        cell.textDecimals = 0;
        cell.isChoice = true;
        cell.isLocked = grainLocked;
        cell.bounds = playbackRow2[1];
        cell.label = "GRAIN";
        cell.valueText = getChoiceName (grainMode, grainNames);
        cell.drawTrailingDivider = true;
        addParamCell (cell);
    }

    const int stretchCellIndex = resolvedAlgo == 1 ? 4 : (resolvedAlgo == 2 ? 2 : 1);
    const int oneShotCellIndex = resolvedAlgo == 1 ? 5 : (resolvedAlgo == 2 ? 3 : 2);

    cell = {};
    cell.module = Module::Playback;
    cell.globalParamId = ParamIds::defaultStretchEnabled;
    cell.fieldId = IntersectProcessor::FieldStretchEnabled;
    cell.lockBit = kLockStretch;
    cell.currentValue = resolvedStretch ? 1.0f : 0.0f;
    cell.minVal = 0.0f;
    cell.maxVal = 1.0f;
    cell.step = 1.0f;
    cell.textDecimals = 0;
    cell.isBoolean = true;
    cell.isLocked = stretchLocked;
    cell.bounds = playbackRow2[(size_t) stretchCellIndex];
    cell.label = "STRETCH";
    cell.valueText = formatBool (resolvedStretch);
    cell.drawTrailingDivider = true;
    addParamCell (cell);

    const auto [oneShot, oneShotLocked] = sliceScope ? resolveBool (kLockOneShot, selectedSlice->oneShot, gOneShot)
                                                     : std::pair<bool, bool> { gOneShot, false };
    cell = {};
    cell.module = Module::Playback;
    cell.globalParamId = ParamIds::defaultOneShot;
    cell.fieldId = IntersectProcessor::FieldOneShot;
    cell.lockBit = kLockOneShot;
    cell.currentValue = oneShot ? 1.0f : 0.0f;
    cell.minVal = 0.0f;
    cell.maxVal = 1.0f;
    cell.step = 1.0f;
    cell.textDecimals = 0;
    cell.isBoolean = true;
    cell.isLocked = oneShotLocked;
    cell.bounds = playbackRow2[(size_t) oneShotCellIndex];
    cell.label = "1SHOT";
    cell.valueText = formatBool (oneShot);
    cell.drawTrailingDivider = false;
    addParamCell (cell);

    const auto [filterEnabled, filterEnabledLocked] = sliceScope ? resolveBool (kLockFilterEnabled, selectedSlice->filterEnabled, gFilterEnabled)
                                                                 : std::pair<bool, bool> { gFilterEnabled, false };
    cell = {};
    cell.module = Module::Filter;
    {
        auto nameFont = IntersectLookAndFeel::fitFontToWidth (
            modules[1].name, 8.5f, 7.2f, modules[1].headerBounds.getWidth() - 6, true);
        int nameW = (int) std::ceil (measureTextWidth (nameFont, modules[1].name));
        cell.bounds = juce::Rectangle<int> (
            modules[1].headerBounds.getX() + nameW + 5,
            modules[1].headerBounds.getY() + 2,
            28, modules[1].headerBounds.getHeight() - 4);
    }
    cell.globalParamId = ParamIds::defaultFilterEnabled;
    cell.fieldId = IntersectProcessor::FieldFilterEnabled;
    cell.lockBit = kLockFilterEnabled;
    cell.currentValue = filterEnabled ? 1.0f : 0.0f;
    cell.minVal = 0.0f;
    cell.maxVal = 1.0f;
    cell.step = 1.0f;
    cell.textDecimals = 0;
    cell.isBoolean = true;
    cell.isLocked = filterEnabledLocked;
    cell.isHeaderControl = true;
    cell.label = "FILTER";
    cell.valueText = filterEnabled ? "ON" : "OFF";
    addParamCell (cell);

    const auto [filterType, filterTypeLocked] = sliceScope ? resolveInt (kLockFilterType, selectedSlice->filterType, gFilterType)
                                                           : std::pair<int, bool> { gFilterType, false };
    const auto [filterSlope, filterSlopeLocked] = sliceScope ? resolveInt (kLockFilterSlope, selectedSlice->filterSlope, gFilterSlope)
                                                             : std::pair<int, bool> { gFilterSlope, false };
    const auto [filterCutoff, filterCutoffLocked] = sliceScope ? resolveFloat (kLockFilterCutoff, selectedSlice->filterCutoff, gFilterCutoff)
                                                               : std::pair<float, bool> { gFilterCutoff, false };
    const auto [filterReso, filterResoLocked] = sliceScope ? resolveFloat (kLockFilterReso, selectedSlice->filterReso, gFilterReso)
                                                           : std::pair<float, bool> { gFilterReso, false };
    const auto [filterDrive, filterDriveLocked] = sliceScope ? resolveFloat (kLockFilterDrive, selectedSlice->filterDrive, gFilterDrive)
                                                             : std::pair<float, bool> { gFilterDrive, false };
    const auto [filterKey, filterKeyLocked] = sliceScope ? resolveFloat (kLockFilterKeyTrack, selectedSlice->filterKeyTrack, gFilterKey)
                                                         : std::pair<float, bool> { gFilterKey, false };
    const auto [filterAtkSec, filterAtkLocked] = sliceScope ? resolveFloat (kLockFilterEnvAttack, selectedSlice->filterEnvAttackSec, gFilterAtkSec)
                                                            : std::pair<float, bool> { gFilterAtkSec, false };
    const auto [filterDecSec, filterDecLocked] = sliceScope ? resolveFloat (kLockFilterEnvDecay, selectedSlice->filterEnvDecaySec, gFilterDecSec)
                                                            : std::pair<float, bool> { gFilterDecSec, false };
    const auto [filterSus, filterSusLocked] = sliceScope ? resolveFloat (kLockFilterEnvSustain, selectedSlice->filterEnvSustain, gFilterSus)
                                                         : std::pair<float, bool> { gFilterSus, false };
    const auto [filterRelSec, filterRelLocked] = sliceScope ? resolveFloat (kLockFilterEnvRelease, selectedSlice->filterEnvReleaseSec, gFilterRelSec)
                                                            : std::pair<float, bool> { gFilterRelSec, false };
    const auto [filterAmt, filterAmtLocked] = sliceScope ? resolveFloat (kLockFilterEnvAmount, selectedSlice->filterEnvAmount, gFilterAmt)
                                                         : std::pair<float, bool> { gFilterAmt, false };

    auto addFilterCell = [this, filterEnabled] (const juce::Rectangle<int>& bounds,
                                                const juce::String& label,
                                                const juce::String& valueText,
                                                const juce::String& globalId,
                                                int fieldId,
                                                uint32_t lockBit,
                                                float value,
                                                float minValue,
                                                float maxValue,
                                                float stepValue,
                                                float dragPerPixel,
                                                int textDecimals,
                                                bool isChoice,
                                                int choiceCount,
                                                bool isLocked,
                                                DragMapping dragMapping = DragMapping::Linear,
                                                bool isBoolean = false,
                                                float displayScale = 1.0f,
                                                bool drawTrailingDivider = false)
    {
        Cell c;
        c.module = Module::Filter;
        c.bounds = bounds;
        c.label = label;
        c.valueText = valueText;
        c.globalParamId = globalId;
        c.fieldId = fieldId;
        c.lockBit = lockBit;
        c.dragMapping = dragMapping;
        c.currentValue = value;
        c.minVal = minValue;
        c.maxVal = maxValue;
        c.step = stepValue;
        c.dragPerPixel = dragPerPixel;
        c.textDecimals = textDecimals;
        c.isChoice = isChoice;
        c.choiceCount = choiceCount;
        c.isLocked = isLocked;
        c.isBoolean = isBoolean;
        c.displayScale = displayScale;
        c.isVisuallyDimmed = ! filterEnabled;
        c.drawTrailingDivider = drawTrailingDivider;
        addParamCell (c);
    };

    addFilterCell (filterRow1[0], "TYPE", getChoiceName (filterType, filterTypeNames),
                   ParamIds::defaultFilterType, IntersectProcessor::FieldFilterType, kLockFilterType,
                   (float) filterType, 0.0f, 3.0f, 1.0f, 0.0f, 0, true, 4, filterTypeLocked, DragMapping::Linear, false, 1.0f, true);
    addFilterCell (filterRow1[1], "SLOPE", getChoiceName (filterSlope, filterSlopeNames),
                   ParamIds::defaultFilterSlope, IntersectProcessor::FieldFilterSlope, kLockFilterSlope,
                   (float) filterSlope, 0.0f, 1.0f, 1.0f, 0.0f, 0, true, 2, filterSlopeLocked, DragMapping::Linear, false, 1.0f, true);
    addFilterCell (filterRow1[2], "CUT", formatHz (filterCutoff),
                   ParamIds::defaultFilterCutoff, IntersectProcessor::FieldFilterCutoff, kLockFilterCutoff,
                   filterCutoff, 20.0f, 20000.0f, 1.0f, 0.005f, 0, false, 0, filterCutoffLocked, DragMapping::FilterCutoff, false, 1.0f, true);
    addFilterCell (filterRow1[3], "RESO", formatPercent (filterReso, 1),
                   ParamIds::defaultFilterReso, IntersectProcessor::FieldFilterReso, kLockFilterReso,
                   filterReso, 0.0f, 100.0f, 0.1f, 0.5f, 1, false, 0, filterResoLocked, DragMapping::Linear, false, 1.0f, true);
    addFilterCell (filterRow1[4], "DRIVE", formatPercent (filterDrive, 1),
                   ParamIds::defaultFilterDrive, IntersectProcessor::FieldFilterDrive, kLockFilterDrive,
                   filterDrive, 0.0f, 100.0f, 0.1f, 0.5f, 1, false, 0, filterDriveLocked, DragMapping::Linear, false, 1.0f, true);
    addFilterCell (filterRow1[5], "KEY", formatPercent (filterKey, 1),
                   ParamIds::defaultFilterKeyTrack, IntersectProcessor::FieldFilterKeyTrack, kLockFilterKeyTrack,
                   filterKey, 0.0f, 100.0f, 0.1f, 0.5f, 1, false, 0, filterKeyLocked);

    addFilterCell (filterRow2[0], "ATK", formatMs (filterAtkSec * 1000.0f),
                   ParamIds::defaultFilterEnvAttack, IntersectProcessor::FieldFilterEnvAttack, kLockFilterEnvAttack,
                   filterAtkSec, 0.0f, 10.0f, 0.0001f, 5.0f, 1, false, 0, filterAtkLocked, DragMapping::Linear, false, 1000.0f, true);
    addFilterCell (filterRow2[1], "DEC", formatMs (filterDecSec * 1000.0f),
                   ParamIds::defaultFilterEnvDecay, IntersectProcessor::FieldFilterEnvDecay, kLockFilterEnvDecay,
                   filterDecSec, 0.0f, 10.0f, 0.0001f, 5.0f, 1, false, 0, filterDecLocked, DragMapping::Linear, false, 1000.0f, true);
    addFilterCell (filterRow2[2], "SUS", formatPercent (filterSus * 100.0f, 1),
                   ParamIds::defaultFilterEnvSustain, IntersectProcessor::FieldFilterEnvSustain, kLockFilterEnvSustain,
                   filterSus, 0.0f, 1.0f, 0.001f, 0.5f, 1, false, 0, filterSusLocked, DragMapping::Linear, false, 100.0f, true);
    addFilterCell (filterRow2[3], "REL", formatMs (filterRelSec * 1000.0f),
                   ParamIds::defaultFilterEnvRelease, IntersectProcessor::FieldFilterEnvRelease, kLockFilterEnvRelease,
                   filterRelSec, 0.0f, 10.0f, 0.0001f, 5.0f, 1, false, 0, filterRelLocked, DragMapping::Linear, false, 1000.0f, true);
    addFilterCell (filterRow2[4], "AMT", formatSigned (filterAmt, 1, "st"),
                   ParamIds::defaultFilterEnvAmount, IntersectProcessor::FieldFilterEnvAmount, kLockFilterEnvAmount,
                   filterAmt, -96.0f, 96.0f, 0.1f, 0.2f, 1, false, 0, filterAmtLocked);

    const auto [attackSec, attackLocked] = sliceScope ? resolveFloat (kLockAttack, selectedSlice->attackSec, gAttackMs / 1000.0f)
                                                      : std::pair<float, bool> { gAttackMs, false };
    const auto [decaySec, decayLocked] = sliceScope ? resolveFloat (kLockDecay, selectedSlice->decaySec, gDecayMs / 1000.0f)
                                                    : std::pair<float, bool> { gDecayMs, false };
    const auto [sustain, sustainLocked] = sliceScope ? resolveFloat (kLockSustain, selectedSlice->sustainLevel, gSustainPct / 100.0f)
                                                     : std::pair<float, bool> { gSustainPct, false };
    const auto [releaseSec, releaseLocked] = sliceScope ? resolveFloat (kLockRelease, selectedSlice->releaseSec, gReleaseMs / 1000.0f)
                                                        : std::pair<float, bool> { gReleaseMs, false };
    const auto [tail, tailLocked] = sliceScope ? resolveBool (kLockReleaseTail, selectedSlice->releaseTail, gTail)
                                               : std::pair<bool, bool> { gTail, false };

    auto addAmpCell = [this] (const juce::Rectangle<int>& bounds,
                              const juce::String& label,
                              const juce::String& valueText,
                              const juce::String& globalId,
                              int fieldId,
                              uint32_t lockBit,
                              float value,
                              float minValue,
                              float maxValue,
                              float stepValue,
                              float dragPerPixel,
                              int textDecimals,
                              bool isLocked,
                              bool isBoolean = false,
                              float displayScale = 1.0f,
                              bool drawTrailingDivider = false)
    {
        Cell c;
        c.module = Module::Amp;
        c.bounds = bounds;
        c.label = label;
        c.valueText = valueText;
        c.globalParamId = globalId;
        c.fieldId = fieldId;
        c.lockBit = lockBit;
        c.currentValue = value;
        c.minVal = minValue;
        c.maxVal = maxValue;
        c.step = stepValue;
        c.dragPerPixel = dragPerPixel;
        c.textDecimals = textDecimals;
        c.isLocked = isLocked;
        c.isBoolean = isBoolean;
        c.displayScale = displayScale;
        c.drawTrailingDivider = drawTrailingDivider;
        addParamCell (c);
    };

    addAmpCell (ampRow1[0], "ATK", sliceScope ? formatMs (attackSec * 1000.0f) : formatMs (attackSec),
                ParamIds::defaultAttack, IntersectProcessor::FieldAttack, kLockAttack,
                attackSec, 0.0f, sliceScope ? 1.0f : 1000.0f, sliceScope ? 0.0001f : 0.1f,
                2.0f, 1, attackLocked, false, sliceScope ? 1000.0f : 1.0f, true);
    addAmpCell (ampRow1[1], "DEC", sliceScope ? formatMs (decaySec * 1000.0f) : formatMs (decaySec),
                ParamIds::defaultDecay, IntersectProcessor::FieldDecay, kLockDecay,
                decaySec, 0.0f, sliceScope ? 5.0f : 5000.0f, sliceScope ? 0.0001f : 0.1f,
                5.0f, 1, decayLocked, false, sliceScope ? 1000.0f : 1.0f, true);
    addAmpCell (ampRow1[2], "SUS", sliceScope ? formatPercent (sustain * 100.0f, 1) : formatPercent (sustain, 1),
                ParamIds::defaultSustain, IntersectProcessor::FieldSustain, kLockSustain,
                sustain, 0.0f, sliceScope ? 1.0f : 100.0f, sliceScope ? 0.001f : 0.1f,
                0.5f, 1, sustainLocked, false, sliceScope ? 100.0f : 1.0f);
    addAmpCell (ampRow2[0], "REL", sliceScope ? formatMs (releaseSec * 1000.0f) : formatMs (releaseSec),
                ParamIds::defaultRelease, IntersectProcessor::FieldRelease, kLockRelease,
                releaseSec, 0.0f, sliceScope ? 5.0f : 5000.0f, sliceScope ? 0.0001f : 0.1f,
                5.0f, 1, releaseLocked, false, sliceScope ? 1000.0f : 1.0f, true);
    addAmpCell (ampRow2[1], "TAIL", formatBool (tail),
                ParamIds::defaultReleaseTail, IntersectProcessor::FieldReleaseTail, kLockReleaseTail,
                tail ? 1.0f : 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0, tailLocked, true);

    const auto [reverse, reverseLocked] = sliceScope ? resolveBool (kLockReverse, selectedSlice->reverse, gReverse)
                                                     : std::pair<bool, bool> { gReverse, false };
    const auto [loopMode, loopLocked] = sliceScope ? resolveInt (kLockLoop, selectedSlice->loopMode, gLoop)
                                                   : std::pair<int, bool> { gLoop, false };
    const auto [muteGroup, muteLocked] = sliceScope ? resolveInt (kLockMuteGroup, selectedSlice->muteGroup, gMuteGroup)
                                                    : std::pair<int, bool> { gMuteGroup, false };
    const auto [gain, gainLocked] = sliceScope ? resolveFloat (kLockVolume, selectedSlice->volume, gGain)
                                               : std::pair<float, bool> { gGain, false };
    const auto [outputBus, outputLocked] = sliceScope ? resolveInt (kLockOutputBus, selectedSlice->outputBus, 0)
                                                      : std::pair<int, bool> { 0, false };

    auto addOutputCell = [this] (const juce::Rectangle<int>& bounds,
                                 const juce::String& label,
                                 const juce::String& valueText,
                                 const juce::String& globalId,
                                 int fieldId,
                                 uint32_t lockBit,
                                 float value,
                                 float minValue,
                                 float maxValue,
                                 float stepValue,
                                 float dragPerPixel,
                                 int textDecimals,
                                 bool isLocked,
                                 bool isBoolean = false,
                                 bool isChoice = false,
                                 int choiceCount = 0,
                                 float displayOffset = 0.0f,
                                 bool drawTrailingDivider = false)
    {
        Cell c;
        c.module = Module::Output;
        c.bounds = bounds;
        c.label = label;
        c.valueText = valueText;
        c.globalParamId = globalId;
        c.fieldId = fieldId;
        c.lockBit = lockBit;
        c.currentValue = value;
        c.minVal = minValue;
        c.maxVal = maxValue;
        c.step = stepValue;
        c.dragPerPixel = dragPerPixel;
        c.textDecimals = textDecimals;
        c.isLocked = isLocked;
        c.isBoolean = isBoolean;
        c.isChoice = isChoice;
        c.choiceCount = choiceCount;
        c.displayOffset = displayOffset;
        c.drawTrailingDivider = drawTrailingDivider;
        addParamCell (c);
    };

    addOutputCell (outputRow1[0], "REV", formatBool (reverse),
                   ParamIds::defaultReverse, IntersectProcessor::FieldReverse, kLockReverse,
                   reverse ? 1.0f : 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0, reverseLocked, true, false, 0, 0.0f, true);
    addOutputCell (outputRow1[1], "LOOP", getChoiceName (loopMode, loopNames),
                   ParamIds::defaultLoop, IntersectProcessor::FieldLoop, kLockLoop,
                   (float) loopMode, 0.0f, 2.0f, 1.0f, 0.0f, 0, loopLocked, false, true, 3, 0.0f, true);
    addOutputCell (outputRow1[2], "MUTE", juce::String (muteGroup),
                   ParamIds::defaultMuteGroup, IntersectProcessor::FieldMuteGroup, kLockMuteGroup,
                   (float) muteGroup, 0.0f, 32.0f, 1.0f, 0.1f, 0, muteLocked);

    addOutputCell (outputRow2[0], "GAIN", formatGain (gain),
                   ParamIds::masterVolume, IntersectProcessor::FieldVolume, kLockVolume,
                   gain, -100.0f, 24.0f, 0.1f, 0.1f, 1, gainLocked, false, false, 0, 0.0f, true);

    if (sliceScope)
    {
        addOutputCell (outputRow2[1], "OUT", juce::String (outputBus + 1),
                       {}, IntersectProcessor::FieldOutputBus, kLockOutputBus,
                       (float) outputBus, 0.0f, 15.0f, 1.0f, 0.1f, 0, outputLocked, false, false, 0, 1.0f);
    }
    else
    {
        addOutputCell (outputRow2[1], "VOICES", juce::String (gVoices),
                       ParamIds::maxVoices, -1, 0u,
                       (float) gVoices, 1.0f, 31.0f, 1.0f, 0.1f, 0, false);
    }

}

void SignalChainBar::paint (juce::Graphics& g)
{
    rebuildLayout();

    g.fillAll (getTheme().signalChainBg);

    g.setColour (getTheme().contextBarBg);
    g.fillRect (contextBounds);
    g.setColour (getTheme().moduleBorder.withAlpha (0.8f));
    g.drawHorizontalLine (contextBounds.getY(), (float) contextBounds.getX(), (float) contextBounds.getRight());

    auto infoBounds = contextInfoBounds.reduced (0, 2);
    int infoX = infoBounds.getX();

    if (contextTitle.isNotEmpty())
    {
        g.setFont (IntersectLookAndFeel::fitFontToWidth (contextTitle, 10.0f, 8.5f,
                                                         juce::jmin (140, infoBounds.getWidth()), false));
        g.setColour (juce::Colour (0xFF586070));
        const int titleWidth = juce::jmin (128, infoBounds.getWidth());
        g.drawFittedText (contextTitle, infoX, infoBounds.getY(), titleWidth, infoBounds.getHeight(),
                          juce::Justification::centredLeft, 1);
        infoX += titleWidth + 8;
    }

    if (contextDot1Bounds.getWidth() > 0)
    {
        g.setFont (IntersectLookAndFeel::makeFont (13.0f, true));
        g.setColour (juce::Colour (0xFF586070));
        g.drawText (juce::String::charToString (0x00B7), contextDot1Bounds, juce::Justification::centred);
    }

    if (contextDot2Bounds.getWidth() > 0)
    {
        g.setFont (IntersectLookAndFeel::makeFont (13.0f, true));
        g.setColour (juce::Colour (0xFF586070));
        g.drawText (juce::String::charToString (0x00B7), contextDot2Bounds, juce::Justification::centred);
    }

    if (contextSubtitle.isNotEmpty() && infoX < infoBounds.getRight())
    {
        g.setFont (IntersectLookAndFeel::fitFontToWidth (contextSubtitle, 9.0f, 7.5f,
                                                         infoBounds.getRight() - infoX, false));
        g.setColour (juce::Colour (0xFF404858));
        g.drawFittedText (contextSubtitle, infoX, infoBounds.getY(),
                          infoBounds.getRight() - infoX, infoBounds.getHeight(),
                          juce::Justification::centredLeft, 1);
    }

    if (contextStatusBounds.getWidth() > 0 && contextStatus.isNotEmpty())
    {
        g.setFont (IntersectLookAndFeel::makeFont (8.6f, true));
        g.setColour (getTheme().overrideCount);
        g.drawText (contextStatus, contextStatusBounds.withTrimmedRight (14), juce::Justification::centredRight);
    }

    g.setColour (getTheme().darkBar);
    g.fillRect (moduleStripBounds);

    for (size_t mi = 0; mi < modules.size(); ++mi)
    {
        const auto& module = modules[mi];
        g.setFont (IntersectLookAndFeel::fitFontToWidth (module.name, 8.5f, 7.2f,
                                                         module.headerBounds.getWidth() - 6, true));
        g.setColour (module.titleColour.withAlpha (0.96f));
        g.drawFittedText (module.name, module.headerBounds.getX(), module.headerBounds.getY(),
                          module.headerBounds.getWidth(), module.headerBounds.getHeight(),
                          juce::Justification::centredLeft, 1);

        if (isSliceScopeActive() && module.overrideCount > 0)
        {
            g.setFont (IntersectLookAndFeel::makeFont (7.0f, false));
            g.setColour (getTheme().overrideCount);
            g.drawText (juce::String (module.overrideCount),
                        module.headerBounds.getX(), module.headerBounds.getY(),
                        module.headerBounds.getWidth(), module.headerBounds.getHeight(),
                        juce::Justification::centredRight);
        }

        if (mi > 0)
        {
            g.setColour (getTheme().moduleBorder);
            g.fillRect (module.bounds.getX(), module.bounds.getY(), 1, module.bounds.getHeight());
        }
    }

    for (const auto& cell : cells)
    {
        switch (cell.kind)
        {
            case CellKind::Tab:    drawTabCell (g, cell);    break;
            case CellKind::SetBpm: drawSetBpmCell (g, cell); break;
            case CellKind::Param:  drawParamCell (g, cell);  break;
        }
    }
}

void SignalChainBar::drawTabCell (juce::Graphics& g, const Cell& cell) const
{
    auto accent = (cell.tabTarget == TabTarget::Global) ? getTheme().tabGlobalActive : getTheme().tabSliceActive;
    auto inactiveBase = juce::Colour (0xFF384050);
    auto text = cell.isActive ? accent : inactiveBase.withAlpha (cell.isEnabled ? 1.0f : 0.35f);

    g.setFont (IntersectLookAndFeel::fitFontToWidth (cell.valueText, 9.5f, 8.0f, cell.bounds.getWidth() - 8, true));
    g.setColour (text);
    g.drawFittedText (cell.valueText, cell.bounds.reduced (4, 0), juce::Justification::centred, 1);

    if (cell.isActive)
    {
        const float y = (float) (cell.bounds.getBottom() - 2);
        g.setColour (accent);
        g.drawLine ((float) cell.bounds.getX(), y, (float) cell.bounds.getRight(), y, 2.0f);
    }

    if (cell.tabTarget == TabTarget::Global)
    {
        g.setColour (getTheme().moduleBorder);
        g.fillRect (cell.bounds.getRight(), cell.bounds.getY(), 1, cell.bounds.getHeight());
    }
}

void SignalChainBar::drawSetBpmCell (juce::Graphics& g, const Cell& cell) const
{
    drawMiniButton (g, cell.bounds,
                    cell.valueText,
                    getTheme().button.withAlpha (0.96f),
                    getTheme().setBpmBorder.withAlpha (0.72f),
                    getTheme().setBpmText,
                    7.0f, 6.4f,
                    cell.isEnabled);
}

void SignalChainBar::drawParamCell (juce::Graphics& g, const Cell& cell) const
{
    const bool hasContent = cell.label.isNotEmpty() || cell.valueText.isNotEmpty();
    const float dimAlpha = cell.isVisuallyDimmed ? 0.4f : 1.0f;
    const float enabledAlpha = cell.isEnabled ? 1.0f : 0.35f;
    const float alpha = dimAlpha * enabledAlpha;

    const auto contentBounds = cell.bounds.reduced (kCellInsetX, 0);

    if (cell.isHeaderControl)
    {
        const auto fill = (cell.currentValue > 0.5f
            ? getTheme().buttonHover.interpolatedWith (getTheme().filterToggleOn, 0.18f)
            : getTheme().button).withMultipliedAlpha (alpha);
        const auto outline = (cell.currentValue > 0.5f
            ? getTheme().filterToggleOn.withAlpha (0.75f)
            : getTheme().moduleBorder.withAlpha (0.82f)).withMultipliedAlpha (alpha);
        const auto text = (cell.currentValue > 0.5f
            ? getTheme().filterToggleOn.brighter (0.5f)
            : getTheme().paramLabel.withAlpha (0.96f)).withMultipliedAlpha (alpha);

        drawMiniButton (g, cell.bounds, cell.valueText, fill, outline, text, 7.0f, 6.4f, cell.isEnabled);
        return;
    }

    if (! hasContent)
        return;

    if (cell.isContextInline)
    {
        const bool hasLabel = cell.label.isNotEmpty();
        const int labelWidth = hasLabel ? 24 : 0;
        const int labelGap = hasLabel ? 4 : 0;

        auto valueFont = IntersectLookAndFeel::fitFontToWidth (cell.valueText, 9.0f, 8.0f,
                                                               contentBounds.getWidth() - labelWidth - labelGap, false);

        if (hasLabel)
        {
            auto labelFont = IntersectLookAndFeel::makeFont (7.5f, true);
            g.setFont (labelFont);
            g.setColour (getTheme().paramLabel.brighter (0.15f).withAlpha (alpha));
            g.drawText (cell.label, contentBounds.getX(), contentBounds.getY(),
                        labelWidth, contentBounds.getHeight(), juce::Justification::centredLeft);
        }

        g.setFont (valueFont);
        g.setColour ((cell.isLocked ? getTheme().overrideValue : getTheme().foreground).withAlpha (alpha));
        g.drawFittedText (cell.valueText, contentBounds.getX() + labelWidth + labelGap, contentBounds.getY(),
                          contentBounds.getWidth() - labelWidth - labelGap, contentBounds.getHeight(),
                          juce::Justification::centredLeft, 1);
        return;
    }

    if (cell.label.isNotEmpty())
    {
        g.setFont (IntersectLookAndFeel::fitFontToWidth (cell.label, 7.5f, 6.6f, contentBounds.getWidth(), true));
        g.setColour ((cell.isLocked ? getTheme().overrideLabel : getTheme().paramLabel).withAlpha (alpha));
        g.drawFittedText (cell.label,
                          contentBounds.getX(),
                          contentBounds.getY() + kLabelYOffset,
                          contentBounds.getWidth(),
                          kLabelHeight,
                          juce::Justification::centredLeft,
                          1);
    }

    if (cell.valueText.isNotEmpty())
    {
        const bool isOverride = cell.isLocked && isSliceScopeActive()
            && ! cell.isHeaderControl && ! cell.isContextInline;
        auto valueColour = isOverride ? getTheme().overrideValue : getTheme().paramValue;
        if (! isOverride && cell.isBoolean)
            valueColour = cell.currentValue > 0.5f ? getTheme().paramValueOn : getTheme().paramValueOff.brighter (0.6f);
        if (cell.isReadOnly)
            valueColour = valueColour.withMultipliedAlpha (0.55f);
        valueColour = valueColour.withMultipliedAlpha (alpha);

        g.setFont (IntersectLookAndFeel::fitFontToWidth (cell.valueText, 10.5f, 8.5f, contentBounds.getWidth(), false));
        g.setColour (valueColour);
        g.drawFittedText (cell.valueText,
                          contentBounds.getX(),
                          contentBounds.getY() + kValueYOffset,
                          contentBounds.getWidth(),
                          juce::jmax (kMinValueHeight, contentBounds.getHeight() - kValueYOffset),
                          juce::Justification::centredLeft,
                          1);
    }

}

void SignalChainBar::toggleBooleanCell (const Cell& cell)
{
    applyCellValue (cell, cell.currentValue > 0.5f ? 0.0f : 1.0f, ! isSliceScopeActive());
}

void SignalChainBar::cycleChoiceCell (const Cell& cell)
{
    const int current = juce::jlimit (0, juce::jmax (0, cell.choiceCount - 1), (int) std::round (cell.currentValue));
    const int next = cell.choiceCount > 0 ? (current + 1) % cell.choiceCount : current;
    applyCellValue (cell, (float) next, ! isSliceScopeActive());
}

void SignalChainBar::applyCellValue (const Cell& cell, float storedValue, bool oneShotGlobal)
{
    storedValue = clampStoredValue (cell, storedValue);

    if (isSliceScopeActive() && cell.fieldId >= 0)
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdSetSliceParam;
        cmd.intParam1 = cell.fieldId;
        cmd.floatParam1 = storedValue;
        processor.pushCommand (cmd);
        return;
    }

    if (cell.globalParamId.isEmpty())
        return;

    if (auto* param = processor.apvts.getParameter (cell.globalParamId))
    {
        if (oneShotGlobal)
            param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (storedValue));
        if (oneShotGlobal)
            param->endChangeGesture();
    }
}

void SignalChainBar::clearSliceOverride (uint32_t lockBit)
{
    if (lockBit == 0)
        return;

    IntersectProcessor::Command cmd;
    cmd.type = IntersectProcessor::CmdToggleLock;
    cmd.intParam1 = (int) lockBit;
    processor.pushCommand (cmd);
}

void SignalChainBar::beginGlobalGesture (const Cell& cell)
{
    if (cell.globalParamId.isEmpty())
        return;

    if (auto* param = processor.apvts.getParameter (cell.globalParamId))
    {
        param->beginChangeGesture();
        activeGlobalParamId = cell.globalParamId;
        globalGestureActive = true;
    }
}

void SignalChainBar::endGlobalGesture()
{
    if (! globalGestureActive || activeGlobalParamId.isEmpty())
        return;

    if (auto* param = processor.apvts.getParameter (activeGlobalParamId))
        param->endChangeGesture();

    activeGlobalParamId.clear();
    globalGestureActive = false;
}

void SignalChainBar::showSetBpmPopup()
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

    auto* topLevel = getTopLevelComponent();
    const float menuScale = IntersectLookAndFeel::getMenuScale();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withParentComponent (topLevel)
                            .withStandardItemHeight ((int) (24.0f * menuScale)),
        [this] (int result)
        {
            if (result <= 0 || result > 9)
                return;

            const float bars[] = { 0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
            const float barCount = bars[result];

            if (isSliceScopeActive())
            {
                IntersectProcessor::Command cmd;
                cmd.type = IntersectProcessor::CmdStretch;
                cmd.floatParam1 = barCount;
                processor.pushCommand (cmd);
            }
            else if (processor.sampleData.isLoaded())
            {
                const float sampleRate = processor.getSampleRate() > 0.0 ? (float) processor.getSampleRate() : 44100.0f;
                const float newBpm = GrainEngine::calcStretchBpm (0,
                                                                  processor.sampleData.getNumFrames(),
                                                                  barCount,
                                                                  sampleRate);
                if (auto* bpmParam = processor.apvts.getParameter (ParamIds::defaultBpm))
                {
                    bpmParam->beginChangeGesture();
                    bpmParam->setValueNotifyingHost (bpmParam->convertTo0to1 (newBpm));
                    bpmParam->endChangeGesture();
                }
                if (auto* algoParam = processor.apvts.getParameter (ParamIds::defaultAlgorithm))
                {
                    algoParam->beginChangeGesture();
                    algoParam->setValueNotifyingHost (algoParam->convertTo0to1 (1.0f));
                    algoParam->endChangeGesture();
                }
            }

            repaint();
        });
}

void SignalChainBar::showTextEditor (const Cell& cell)
{
    textEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*textEditor);
    auto valueBounds = cell.bounds.reduced (kCellInsetX, 0).withTrimmedTop (kValueYOffset);
    textEditor->setBounds (valueBounds);
    textEditor->setFont (IntersectLookAndFeel::fitFontToWidth (cell.valueText, 10.5f, 8.5f,
                                                                valueBounds.getWidth(), false));
    textEditor->setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    textEditor->setColour (juce::TextEditor::textColourId, getTheme().paramValue.brighter (0.3f));
    textEditor->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    textEditor->setColour (juce::TextEditor::focusedOutlineColourId, getTheme().accent.withAlpha (0.4f));
    textEditor->setColour (juce::TextEditor::highlightColourId, getTheme().accent.withAlpha (0.25f));
    textEditor->setJustification (juce::Justification::centredLeft);
    textEditor->setIndents (0, 0);
    textEditor->setText (formatTrimmed (storedToDisplay (cell, cell.currentValue), cell.textDecimals), false);
    textEditor->selectAll();
    textEditor->grabKeyboardFocus();

    juce::Component::SafePointer<SignalChainBar> safeThis (this);
    textEditor->onReturnKey = [safeThis, cell]
    {
        if (safeThis == nullptr || safeThis->textEditor == nullptr)
            return;

        float displayValue = safeThis->textEditor->getText().getFloatValue();
        displayValue = safeThis->clampDisplayValue (cell, displayValue);
        safeThis->applyCellValue (cell, safeThis->displayToStored (cell, displayValue), ! safeThis->isSliceScopeActive());
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

void SignalChainBar::mouseDown (const juce::MouseEvent& e)
{
    rebuildLayout();

    if (textEditor != nullptr)
        textEditor.reset();

    endGlobalGesture();
    activeDragCell = -1;

    const auto pos = e.getPosition();
    for (int i = (int) cells.size() - 1; i >= 0; --i)
    {
        const auto& cell = cells[(size_t) i];
        if (! cell.bounds.contains (pos))
            continue;

        if (cell.kind == CellKind::Tab)
        {
            if (! cell.isEnabled)
                return;

            scope = (cell.tabTarget == TabTarget::Slice) ? Scope::Slice : Scope::Global;
            repaint();
            return;
        }

        if (cell.kind == CellKind::SetBpm)
        {
            if (cell.isEnabled)
                showSetBpmPopup();
            return;
        }

        const bool clearableOverride = isSliceScopeActive()
            && cell.kind == CellKind::Param
            && cell.isLocked
            && cell.lockBit != 0
            && ! cell.isContextInline
            && ! cell.isHeaderControl;
        if (clearableOverride)
        {
            auto labelBounds = juce::Rectangle<int> (cell.bounds.getX() + 3, cell.bounds.getY(),
                                                     cell.bounds.getWidth() - 6, 10);
            if (e.mods.isPopupMenu() || labelBounds.contains (pos))
            {
                clearSliceOverride (cell.lockBit);
                repaint();
                return;
            }
        }

        if (! cell.isEnabled || cell.isReadOnly)
            return;

        if (cell.isBoolean)
        {
            toggleBooleanCell (cell);
            repaint();
            return;
        }

        if (cell.isChoice)
        {
            cycleChoiceCell (cell);
            repaint();
            return;
        }

        activeDragCell = i;
        dragStartY = pos.y;
        dragStartInteractionValue = storedToInteraction (cell, cell.currentValue);

        if (isSliceScopeActive())
        {
            IntersectProcessor::Command gestureCmd;
            gestureCmd.type = IntersectProcessor::CmdBeginGesture;
            processor.pushCommand (gestureCmd);
        }
        else
        {
            beginGlobalGesture (cell);
        }
        return;
    }
}

void SignalChainBar::mouseDrag (const juce::MouseEvent& e)
{
    if (activeDragCell < 0)
        return;

    rebuildLayout();
    if (activeDragCell >= (int) cells.size())
        return;

    const auto& cell = cells[(size_t) activeDragCell];
    if (cell.kind != CellKind::Param || ! cell.isEnabled || cell.isReadOnly)
        return;

    const float deltaY = (float) (dragStartY - e.y);
    const float dragStep = cell.dragPerPixel > 0.0f
        ? cell.dragPerPixel
        : juce::jmax (0.0001f, (storedToInteraction (cell, cell.maxVal) - storedToInteraction (cell, cell.minVal)) / 200.0f);
    const float snap = e.mods.isShiftDown()
        ? juce::jmax (cell.step * 5.0f, 1.0e-4f)
        : juce::jmax (cell.step, 1.0e-4f);

    float interactionValue = dragStartInteractionValue + deltaY * dragStep;
    interactionValue = clampInteractionValue (cell, interactionValue);

    float storedValue = interactionToStored (cell, interactionValue);
    storedValue = clampStoredValue (cell, std::round (storedValue / snap) * snap);
    applyCellValue (cell, storedValue, false);
    repaint();
}

void SignalChainBar::mouseUp (const juce::MouseEvent&)
{
    activeDragCell = -1;
    endGlobalGesture();
}

void SignalChainBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    rebuildLayout();

    const auto pos = e.getPosition();
    for (int i = (int) cells.size() - 1; i >= 0; --i)
    {
        const auto& cell = cells[(size_t) i];
        if (! cell.bounds.contains (pos))
            continue;

        if (cell.kind != CellKind::Param || ! cell.isEnabled || cell.isReadOnly
            || cell.isBoolean || cell.isChoice)
            return;

        showTextEditor (cell);
        return;
    }
}
