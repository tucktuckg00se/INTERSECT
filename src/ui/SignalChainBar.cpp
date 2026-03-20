#include "SignalChainBar.h"
#include "IntersectLookAndFeel.h"
#include "../Constants.h"
#include "../PluginProcessor.h"
#include "../audio/GrainEngine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
constexpr int kContextHeight = 26;
constexpr int kModuleHeaderHeight = 16;
constexpr int kContextTabGap = 0;
constexpr int kContextTextGap = 12;
constexpr int kCellInsetX = 3;
constexpr int kCellGap = 6;
constexpr int kGroupGap = 10;
constexpr int kLabelHeight = 9;
constexpr int kValueYOffset = 9;
constexpr int kMinValueHeight = 13;

const juce::NormalisableRange<float>& getFilterCutoffDragRange()
{
    static const juce::NormalisableRange<float> range (kMinFilterCutoffHz, kMaxFilterCutoffHz, 1.0f, 0.25f);
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
    return juce::String (juce::roundToInt (valueMs)) + "ms";
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

std::pair<juce::Rectangle<int>, juce::Rectangle<int>> splitRows (juce::Rectangle<int> bounds)
{
    juce::FlexBox rows;
    rows.flexDirection = juce::FlexBox::Direction::column;
    rows.flexWrap = juce::FlexBox::Wrap::noWrap;
    rows.items.add (juce::FlexItem().withFlex (1.0f).withMinHeight (18.0f));
    rows.items.add (juce::FlexItem().withFlex (1.0f).withMinHeight (18.0f));
    rows.performLayout (bounds.toFloat());

    return { toIntBounds (rows.items[0].currentBounds),
             toIntBounds (rows.items[1].currentBounds) };
}

template <typename ValueType>
std::pair<ValueType, bool> resolveLockedValue (const Slice* selectedSlice,
                                               uint32_t bit,
                                               ValueType sliceValue,
                                               ValueType globalValue)
{
    const bool locked = selectedSlice != nullptr && (selectedSlice->lockMask & bit) != 0;
    return std::pair<ValueType, bool> { locked ? sliceValue : globalValue, locked };
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

int SignalChainBar::countEffectiveModuleOverrides (Module module,
                                                   const Slice& slice,
                                                   const GlobalParamSnapshot& globals) const
{
    if ((slice.lockMask) == 0)
        return 0;

    auto checkBool = [&] (uint32_t bit, bool sliceVal, bool globalVal) -> int
    {
        if ((slice.lockMask & bit) == 0) return 0;
        return (sliceVal != globalVal) ? 1 : 0;
    };

    auto checkInt = [&] (uint32_t bit, int sliceVal, int globalVal) -> int
    {
        if ((slice.lockMask & bit) == 0) return 0;
        return (sliceVal != globalVal) ? 1 : 0;
    };

    auto checkFloat = [&] (uint32_t bit, float sliceVal, float globalVal) -> int
    {
        if ((slice.lockMask & bit) == 0) return 0;
        return (std::abs (sliceVal - globalVal) > 1e-4f) ? 1 : 0;
    };

    int count = 0;

    switch (module)
    {
        case Module::Playback:
            count += checkFloat (kLockBpm, slice.bpm, globals.bpm);
            count += checkFloat (kLockPitch, slice.pitchSemitones, globals.pitchSemitones);
            count += checkFloat (kLockCentsDetune, slice.centsDetune, globals.centsDetune);
            count += checkInt (kLockAlgorithm, slice.algorithm, globals.algorithm);
            count += checkBool (kLockStretch, slice.stretchEnabled, globals.stretchEnabled);
            count += checkFloat (kLockTonality, slice.tonalityHz, globals.tonalityHz);
            count += checkFloat (kLockFormant, slice.formantSemitones, globals.formantSemitones);
            count += checkBool (kLockFormantComp, slice.formantComp, globals.formantComp);
            count += checkInt (kLockGrainMode, slice.grainMode, globals.grainMode);
            count += checkBool (kLockOneShot, slice.oneShot, globals.oneShot);
            break;

        case Module::Filter:
            count += checkBool (kLockFilterEnabled, slice.filterEnabled, globals.filterEnabled);
            count += checkInt (kLockFilterType, slice.filterType, globals.filterType);
            count += checkInt (kLockFilterSlope, slice.filterSlope, globals.filterSlope);
            count += checkFloat (kLockFilterCutoff, slice.filterCutoff, globals.filterCutoffHz);
            count += checkFloat (kLockFilterReso, slice.filterReso, globals.filterReso);
            count += checkFloat (kLockFilterDrive, slice.filterDrive, globals.filterDrive);
            count += checkFloat (kLockFilterKeyTrack, slice.filterKeyTrack, globals.filterKeyTrack);
            count += checkFloat (kLockFilterEnvAttack, slice.filterEnvAttackSec, globals.filterEnvAttackSec);
            count += checkFloat (kLockFilterEnvDecay, slice.filterEnvDecaySec, globals.filterEnvDecaySec);
            count += checkFloat (kLockFilterEnvSustain, slice.filterEnvSustain, globals.filterEnvSustain);
            count += checkFloat (kLockFilterEnvRelease, slice.filterEnvReleaseSec, globals.filterEnvReleaseSec);
            count += checkFloat (kLockFilterEnvAmount, slice.filterEnvAmount, globals.filterEnvAmount);
            break;

        case Module::Amp:
            count += checkFloat (kLockAttack, slice.attackSec, globals.attackSec);
            count += checkFloat (kLockDecay, slice.decaySec, globals.decaySec);
            count += checkFloat (kLockSustain, slice.sustainLevel, globals.sustain);
            count += checkFloat (kLockRelease, slice.releaseSec, globals.releaseSec);
            count += checkBool (kLockReleaseTail, slice.releaseTail, globals.releaseTail);
            break;

        case Module::Output:
            count += checkBool (kLockReverse, slice.reverse, globals.reverse);
            count += checkInt (kLockLoop, slice.loopMode, globals.loopMode);
            count += checkInt (kLockMuteGroup, slice.muteGroup, globals.muteGroup);
            count += checkFloat (kLockVolume, slice.volume, globals.volumeDb);
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

int SignalChainBar::countAllEffectiveOverrides (const Slice& slice,
                                                const GlobalParamSnapshot& globals) const
{
    int count = 0;
    for (auto moduleId : { Module::Playback, Module::Filter, Module::Amp, Module::Output })
        count += countEffectiveModuleOverrides (moduleId, slice, globals);
    return count;
}

void SignalChainBar::rebuildLayout()
{
    cells.clear();
    syncScopeFromSelection();

    const auto& ui = processor.getUiSliceSnapshot();

    LayoutInput input;
    input.globals = GlobalParamSnapshot::loadFrom (processor.apvts, ui.rootNote);
    input.numSlices = ui.numSlices;
    input.selectedSliceIndex = ui.selectedSlice;
    input.rootNote = ui.rootNote;
    input.sampleNumFrames = ui.sampleNumFrames;
    input.sampleLoaded = ui.sampleLoaded;
    input.sampleMissing = ui.sampleMissing;
    input.hasValidSlice = ui.selectedSlice >= 0 && ui.selectedSlice < ui.numSlices;
    input.sliceScope = isSliceScopeActive();
    input.sampleRate = processor.getSampleRate() > 0.0 ? (float) processor.getSampleRate() : 44100.0f;
    input.selectedSlice = input.hasValidSlice ? &ui.slices[(size_t) ui.selectedSlice] : nullptr;

    contextTitle.clear();
    contextSubtitle.clear();
    contextStatus.clear();
    contextStatusBounds = {};
    contextDot1Bounds = {};
    contextDot2Bounds = {};
    contextSlicesBounds = {};
    contextRootBounds = {};

    juce::FlexBox shell;
    shell.flexDirection = juce::FlexBox::Direction::column;
    shell.flexWrap = juce::FlexBox::Wrap::noWrap;
    shell.items.add (juce::FlexItem().withFlex (1.0f));
    shell.items.add (juce::FlexItem().withHeight ((float) kContextHeight));
    shell.performLayout (getLocalBounds().toFloat());

    moduleStripBounds = toIntBounds (shell.items[0].currentBounds);
    contextBounds = toIntBounds (shell.items[1].currentBounds);

    rebuildContextBar (input);

    const std::array<float, 4> moduleWeights { 2.2f, 2.0f, 1.0f, 1.1f };
    const std::array<juce::String, 4> names { "PLAYBACK", "FILTER", "AMP", "OUTPUT" };
    const std::array<juce::Colour, 4> colours {
        getTheme().color1,
        getTheme().color2,
        getTheme().color3,
        getTheme().color4
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
    }
    moduleRow.performLayout (moduleStripBounds.toFloat());

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
        module.overrideCount = input.sliceScope && input.selectedSlice != nullptr
            ? countEffectiveModuleOverrides (module.module, *input.selectedSlice, input.globals)
            : 0;
    }

    rebuildPlaybackModule (input, splitRows (modules[0].bodyBounds));
    rebuildFilterModule (input, splitRows (modules[1].bodyBounds));
    rebuildAmpModule (input, splitRows (modules[2].bodyBounds));
    rebuildOutputModule (input, splitRows (modules[3].bodyBounds));
}

void SignalChainBar::rebuildContextBar (const LayoutInput& input)
{
    const int globalTabWidth = 56;
    const juce::String sliceTabText = input.hasValidSlice
        ? "SLICE " + juce::String (input.selectedSliceIndex + 1)
        : "SLICE";
    const int sliceTabWidth = input.hasValidSlice ? 76 : 56;

    juce::FlexBox contextRow;
    contextRow.flexDirection = juce::FlexBox::Direction::row;
    contextRow.flexWrap = juce::FlexBox::Wrap::noWrap;
    contextRow.alignItems = juce::FlexBox::AlignItems::stretch;

    contextRow.items.add (juce::FlexItem().withWidth ((float) globalTabWidth));
    contextRow.items.add (juce::FlexItem().withWidth ((float) kContextTabGap));
    contextRow.items.add (juce::FlexItem().withWidth ((float) sliceTabWidth));
    contextRow.items.add (juce::FlexItem().withWidth ((float) kContextTextGap));

    if (input.sliceScope && input.selectedSlice != nullptr)
    {
        const int timeItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (42.0f));
        const int dot1ItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (10.0f));
        const int noteItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (24.0f));
        const int dot2ItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (10.0f));
        const int midiItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (50.0f));
        contextRow.items.add (juce::FlexItem().withFlex (1.0f));
        const int statusItemIndex = contextRow.items.size();
        contextRow.items.add (juce::FlexItem().withWidth (90.0f));

        contextRow.performLayout (contextBounds.toFloat());

        addTabCell (toIntBounds (contextRow.items[0].currentBounds), "GLOBAL", TabTarget::Global, ! input.sliceScope, true);
        addTabCell (toIntBounds (contextRow.items[2].currentBounds), sliceTabText, TabTarget::Slice, input.sliceScope, input.hasValidSlice);

        contextInfoBounds = toIntBounds (contextRow.items[timeItemIndex].currentBounds);
        contextDot1Bounds = toIntBounds (contextRow.items[dot1ItemIndex].currentBounds);
        contextDot2Bounds = toIntBounds (contextRow.items[dot2ItemIndex].currentBounds);
        contextStatusBounds = toIntBounds (contextRow.items[statusItemIndex].currentBounds);

        const int overrideCount = countAllEffectiveOverrides (*input.selectedSlice, input.globals);
        const float lenSec = (float) (input.selectedSlice->endSample - input.selectedSlice->startSample) / input.sampleRate;
        contextTitle = formatTrimmed (lenSec, 2) + "s";
        contextStatus = overrideCount > 0 ? juce::String (overrideCount) + " overrides" : juce::String();

        Cell noteCell;
        noteCell.module = Module::Playback;
        noteCell.bounds = toIntBounds (contextRow.items[noteItemIndex].currentBounds);
        noteCell.valueText = midiNoteName (input.selectedSlice->midiNote);
        noteCell.isContextInline = true;
        noteCell.fieldId = IntersectProcessor::FieldMidiNote;
        noteCell.currentValue = (float) input.selectedSlice->midiNote;
        noteCell.minVal = 0.0f;
        noteCell.maxVal = 127.0f;
        noteCell.step = 1.0f;
        noteCell.dragPerPixel = 0.25f;
        addParamCell (noteCell);

        Cell midiCell;
        midiCell.module = Module::Playback;
        midiCell.bounds = toIntBounds (contextRow.items[midiItemIndex].currentBounds);
        midiCell.label = "MIDI";
        midiCell.valueText = juce::String (input.selectedSlice->midiNote);
        midiCell.isContextInline = true;
        midiCell.fieldId = IntersectProcessor::FieldMidiNote;
        midiCell.currentValue = (float) input.selectedSlice->midiNote;
        midiCell.minVal = 0.0f;
        midiCell.maxVal = 127.0f;
        midiCell.step = 1.0f;
        midiCell.dragPerPixel = 0.25f;
        addParamCell (midiCell);
        return;
    }

    const int infoItemIndex = contextRow.items.size();
    contextRow.items.add (juce::FlexItem().withFlex (1.0f).withMinWidth (40.0f));
    const int slicesItemIndex = contextRow.items.size();
    contextRow.items.add (juce::FlexItem().withWidth (58.0f));
    contextRow.items.add (juce::FlexItem().withWidth (8.0f));   // gap
    const int rootItemIndex = contextRow.items.size();
    contextRow.items.add (juce::FlexItem().withWidth (42.0f));
    contextRow.items.add (juce::FlexItem().withWidth (10.0f));  // trailing pad
    contextRow.performLayout (contextBounds.toFloat());

    addTabCell (toIntBounds (contextRow.items[0].currentBounds), "GLOBAL", TabTarget::Global, ! input.sliceScope, true);
    addTabCell (toIntBounds (contextRow.items[2].currentBounds), sliceTabText, TabTarget::Slice, input.sliceScope, input.hasValidSlice);

    contextInfoBounds = toIntBounds (contextRow.items[infoItemIndex].currentBounds);
    contextSlicesBounds = toIntBounds (contextRow.items[slicesItemIndex].currentBounds);
    contextRootBounds = toIntBounds (contextRow.items[rootItemIndex].currentBounds);

    if (input.sampleMissing)
        contextSubtitle = "MISSING SAMPLE, RELINK REQUIRED";
    else if (! input.sampleLoaded)
        contextSubtitle = "NO SAMPLE LOADED";
}

void SignalChainBar::rebuildPlaybackModule (const LayoutInput& input,
                                            const std::pair<juce::Rectangle<int>, juce::Rectangle<int>>& rows)
{
    const auto* selectedSlice = input.selectedSlice;
    const auto& globals = input.globals;
    const auto algoNames = juce::StringArray { "Repitch", "Stretch", "Bungee" };
    const auto grainNames = juce::StringArray { "Fast", "Normal", "Smooth" };
    const auto row1 = makeRowCells (rows.first, {
        { 1.0f, kCellGap }, { 0.92f, kCellGap }, { 0.9f, 0 }
    });

    Cell setBpmCell;
    setBpmCell.kind = CellKind::SetBpm;
    setBpmCell.module = Module::Playback;
    auto nameFont = IntersectLookAndFeel::fitFontToWidth (
        modules[0].name, 8.5f, 7.2f, modules[0].headerBounds.getWidth() - 6, true);
    const int nameW = (int) std::ceil (measureTextWidth (nameFont, modules[0].name));
    setBpmCell.bounds = juce::Rectangle<int> (
        modules[0].headerBounds.getX() + nameW + 5,
        modules[0].headerBounds.getY() + 2,
        38, modules[0].headerBounds.getHeight() - 4);
    setBpmCell.valueText = "SET BPM";
    setBpmCell.isEnabled = input.sliceScope ? input.hasValidSlice : input.sampleLoaded;
    addParamCell (setBpmCell);

    const auto [resolvedBpm, bpmLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockBpm, selectedSlice->bpm, globals.bpm)
        : std::pair<float, bool> { globals.bpm, false };
    const auto [resolvedPitch, pitchLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockPitch, selectedSlice->pitchSemitones, globals.pitchSemitones)
        : std::pair<float, bool> { globals.pitchSemitones, false };
    const auto [resolvedCents, centsLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockCentsDetune, selectedSlice->centsDetune, globals.centsDetune)
        : std::pair<float, bool> { globals.centsDetune, false };
    const auto [resolvedAlgo, algoLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockAlgorithm, selectedSlice->algorithm, globals.algorithm)
        : std::pair<int, bool> { globals.algorithm, false };
    const auto [resolvedStretch, stretchLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockStretch, selectedSlice->stretchEnabled, globals.stretchEnabled)
        : std::pair<bool, bool> { globals.stretchEnabled, false };

    const auto row2 = resolvedAlgo == 1
        ? makeRowCells (rows.second, {
            { 1.45f, kGroupGap }, { 0.96f, kCellGap }, { 0.92f, kCellGap }, { 0.8f, kGroupGap }, { 1.04f, kCellGap }, { 0.74f, 0 }
        })
        : (resolvedAlgo == 2
            ? makeRowCells (rows.second, {
                { 1.46f, kGroupGap }, { 1.06f, kGroupGap }, { 1.06f, kCellGap }, { 0.78f, 0 }
            })
            : makeRowCells (rows.second, {
                { 1.46f, kGroupGap }, { 1.18f, kCellGap }, { 0.82f, 0 }
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
    cell.bounds = row1[0];
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
    cell.dragPerPixel = 0.5f;
    cell.textDecimals = 2;
    cell.isLocked = pitchLocked;
    cell.isReadOnly = repitchStretch;
    cell.bounds = row1[1];
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
    cell.bounds = row1[2];
    cell.label = "TUNE";
    cell.valueText = formatSigned (displayCents, 1, "ct");
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
    cell.isChoice = true;
    cell.isLocked = algoLocked;
    cell.bounds = row2[0];
    cell.label = "ALGO";
    cell.valueText = getChoiceName (resolvedAlgo, algoNames);
    cell.drawTrailingDivider = true;
    addParamCell (cell);

    if (resolvedAlgo == 1)
    {
        const auto [tonality, tonalityLocked] = input.sliceScope
            ? resolveLockedValue (selectedSlice, kLockTonality, selectedSlice->tonalityHz, globals.tonalityHz)
            : std::pair<float, bool> { globals.tonalityHz, false };
        const auto [formant, formantLocked] = input.sliceScope
            ? resolveLockedValue (selectedSlice, kLockFormant, selectedSlice->formantSemitones, globals.formantSemitones)
            : std::pair<float, bool> { globals.formantSemitones, false };
        const auto [formantComp, formantCompLocked] = input.sliceScope
            ? resolveLockedValue (selectedSlice, kLockFormantComp, selectedSlice->formantComp, globals.formantComp)
            : std::pair<bool, bool> { globals.formantComp, false };

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
        cell.isLocked = tonalityLocked;
        cell.bounds = row2[1];
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
        cell.bounds = row2[2];
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
        cell.isBoolean = true;
        cell.isLocked = formantCompLocked;
        cell.bounds = row2[3];
        cell.label = "FMNT C";
        cell.valueText = formatBool (formantComp);
        cell.drawTrailingDivider = true;
        addParamCell (cell);
    }
    else if (resolvedAlgo == 2)
    {
        const auto [grainMode, grainLocked] = input.sliceScope
            ? resolveLockedValue (selectedSlice, kLockGrainMode, selectedSlice->grainMode, globals.grainMode)
            : std::pair<int, bool> { globals.grainMode, false };

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
        cell.isChoice = true;
        cell.isLocked = grainLocked;
        cell.bounds = row2[1];
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
    cell.isBoolean = true;
    cell.isLocked = stretchLocked;
    cell.bounds = row2[(size_t) stretchCellIndex];
    cell.label = "STRETCH";
    cell.valueText = formatBool (resolvedStretch);
    cell.drawTrailingDivider = true;
    addParamCell (cell);

    const auto [oneShot, oneShotLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockOneShot, selectedSlice->oneShot, globals.oneShot)
        : std::pair<bool, bool> { globals.oneShot, false };
    cell = {};
    cell.module = Module::Playback;
    cell.globalParamId = ParamIds::defaultOneShot;
    cell.fieldId = IntersectProcessor::FieldOneShot;
    cell.lockBit = kLockOneShot;
    cell.currentValue = oneShot ? 1.0f : 0.0f;
    cell.minVal = 0.0f;
    cell.maxVal = 1.0f;
    cell.step = 1.0f;
    cell.isBoolean = true;
    cell.isLocked = oneShotLocked;
    cell.bounds = row2[(size_t) oneShotCellIndex];
    cell.label = "1SHOT";
    cell.valueText = formatBool (oneShot);
    addParamCell (cell);
}

void SignalChainBar::rebuildFilterModule (const LayoutInput& input,
                                          const std::pair<juce::Rectangle<int>, juce::Rectangle<int>>& rows)
{
    const auto* selectedSlice = input.selectedSlice;
    const auto& globals = input.globals;
    const auto filterTypeNames = juce::StringArray { "LP", "HP", "BP", "NT" };
    const auto filterSlopeNames = juce::StringArray { "12dB", "24dB" };
    const auto row1 = makeRowCells (rows.first, {
        { 0.82f, kCellGap }, { 0.9f, kCellGap }, { 1.18f, kCellGap }, { 0.96f, kCellGap }, { 0.94f, kCellGap }, { 0.84f, 0 }
    });
    const auto row2 = makeRowCells (rows.second, {
        { 0.92f, kCellGap }, { 0.92f, kCellGap }, { 0.82f, kCellGap }, { 0.95f, kGroupGap }, { 1.06f, 0 }
    });

    const auto [filterEnabled, filterEnabledLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterEnabled, selectedSlice->filterEnabled, globals.filterEnabled)
        : std::pair<bool, bool> { globals.filterEnabled, false };
    const auto [filterType, filterTypeLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterType, selectedSlice->filterType, globals.filterType)
        : std::pair<int, bool> { globals.filterType, false };
    const auto [filterSlope, filterSlopeLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterSlope, selectedSlice->filterSlope, globals.filterSlope)
        : std::pair<int, bool> { globals.filterSlope, false };
    const auto [filterCutoff, filterCutoffLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterCutoff, selectedSlice->filterCutoff, globals.filterCutoffHz)
        : std::pair<float, bool> { globals.filterCutoffHz, false };
    const auto [filterReso, filterResoLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterReso, selectedSlice->filterReso, globals.filterReso)
        : std::pair<float, bool> { globals.filterReso, false };
    const auto [filterDrive, filterDriveLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterDrive, selectedSlice->filterDrive, globals.filterDrive)
        : std::pair<float, bool> { globals.filterDrive, false };
    const auto [filterKey, filterKeyLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterKeyTrack, selectedSlice->filterKeyTrack, globals.filterKeyTrack)
        : std::pair<float, bool> { globals.filterKeyTrack, false };
    const auto [filterAtkSec, filterAtkLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterEnvAttack, selectedSlice->filterEnvAttackSec, globals.filterEnvAttackSec)
        : std::pair<float, bool> { globals.filterEnvAttackSec, false };
    const auto [filterDecSec, filterDecLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterEnvDecay, selectedSlice->filterEnvDecaySec, globals.filterEnvDecaySec)
        : std::pair<float, bool> { globals.filterEnvDecaySec, false };
    const auto [filterSus, filterSusLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterEnvSustain, selectedSlice->filterEnvSustain, globals.filterEnvSustain)
        : std::pair<float, bool> { globals.filterEnvSustain, false };
    const auto [filterRelSec, filterRelLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterEnvRelease, selectedSlice->filterEnvReleaseSec, globals.filterEnvReleaseSec)
        : std::pair<float, bool> { globals.filterEnvReleaseSec, false };
    const auto [filterAmt, filterAmtLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockFilterEnvAmount, selectedSlice->filterEnvAmount, globals.filterEnvAmount)
        : std::pair<float, bool> { globals.filterEnvAmount, false };

    Cell cell;
    cell.module = Module::Filter;
    auto nameFont = IntersectLookAndFeel::fitFontToWidth (
        modules[1].name, 8.5f, 7.2f, modules[1].headerBounds.getWidth() - 6, true);
    const int nameW = (int) std::ceil (measureTextWidth (nameFont, modules[1].name));
    cell.bounds = juce::Rectangle<int> (
        modules[1].headerBounds.getX() + nameW + 5,
        modules[1].headerBounds.getY() + 2,
        28, modules[1].headerBounds.getHeight() - 4);
    cell.globalParamId = ParamIds::defaultFilterEnabled;
    cell.fieldId = IntersectProcessor::FieldFilterEnabled;
    cell.lockBit = kLockFilterEnabled;
    cell.currentValue = filterEnabled ? 1.0f : 0.0f;
    cell.minVal = 0.0f;
    cell.maxVal = 1.0f;
    cell.step = 1.0f;
    cell.isBoolean = true;
    cell.isLocked = filterEnabledLocked;
    cell.isHeaderControl = true;
    cell.valueText = filterEnabled ? "ON" : "OFF";
    addParamCell (cell);

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

    addFilterCell (row1[0], "TYPE", getChoiceName (filterType, filterTypeNames),
                   ParamIds::defaultFilterType, IntersectProcessor::FieldFilterType, kLockFilterType,
                   (float) filterType, 0.0f, 3.0f, 1.0f, 0.0f, 0, true, 4, filterTypeLocked, DragMapping::Linear, false, 1.0f, true);
    addFilterCell (row1[1], "SLOPE", getChoiceName (filterSlope, filterSlopeNames),
                   ParamIds::defaultFilterSlope, IntersectProcessor::FieldFilterSlope, kLockFilterSlope,
                   (float) filterSlope, 0.0f, 1.0f, 1.0f, 0.0f, 0, true, 2, filterSlopeLocked, DragMapping::Linear, false, 1.0f, true);
    addFilterCell (row1[2], "CUT", formatHz (filterCutoff),
                   ParamIds::defaultFilterCutoff, IntersectProcessor::FieldFilterCutoff, kLockFilterCutoff,
                   filterCutoff, kMinFilterCutoffHz, kMaxFilterCutoffHz, 1.0f, 0.005f, 0, false, 0, filterCutoffLocked, DragMapping::FilterCutoff, false, 1.0f, true);
    addFilterCell (row1[3], "RESO", formatPercent (filterReso, 1),
                   ParamIds::defaultFilterReso, IntersectProcessor::FieldFilterReso, kLockFilterReso,
                   filterReso, 0.0f, 100.0f, 0.1f, 0.5f, 1, false, 0, filterResoLocked, DragMapping::Linear, false, 1.0f, true);
    addFilterCell (row1[4], "DRIVE", formatPercent (filterDrive, 1),
                   ParamIds::defaultFilterDrive, IntersectProcessor::FieldFilterDrive, kLockFilterDrive,
                   filterDrive, 0.0f, 100.0f, 0.1f, 0.5f, 1, false, 0, filterDriveLocked, DragMapping::Linear, false, 1.0f, true);
    addFilterCell (row1[5], "KEY", formatPercent (filterKey, 1),
                   ParamIds::defaultFilterKeyTrack, IntersectProcessor::FieldFilterKeyTrack, kLockFilterKeyTrack,
                   filterKey, 0.0f, 100.0f, 0.1f, 0.5f, 1, false, 0, filterKeyLocked);

    const float filterAtkDisplayValue = filterAtkSec * 1000.0f;
    const float filterDecDisplayValue = filterDecSec * 1000.0f;
    const float filterSusValue = input.sliceScope ? filterSus : globals.filterEnvSustain * 100.0f;
    const float filterSusDisplayValue = input.sliceScope ? filterSus * 100.0f : globals.filterEnvSustain * 100.0f;
    const float filterRelDisplayValue = filterRelSec * 1000.0f;

    addFilterCell (row2[0], "ATK", formatMs (filterAtkDisplayValue),
                   ParamIds::defaultFilterEnvAttack, IntersectProcessor::FieldFilterEnvAttack, kLockFilterEnvAttack,
                   input.sliceScope ? filterAtkSec : filterAtkDisplayValue,
                   0.0f, input.sliceScope ? 10.0f : 10000.0f, input.sliceScope ? 0.001f : 1.0f,
                   1.0f, 0, false, 0, filterAtkLocked, DragMapping::Linear, false, input.sliceScope ? 1000.0f : 1.0f, true);
    addFilterCell (row2[1], "DEC", formatMs (filterDecDisplayValue),
                   ParamIds::defaultFilterEnvDecay, IntersectProcessor::FieldFilterEnvDecay, kLockFilterEnvDecay,
                   input.sliceScope ? filterDecSec : filterDecDisplayValue,
                   0.0f, input.sliceScope ? 10.0f : 10000.0f, input.sliceScope ? 0.001f : 1.0f,
                   1.0f, 0, false, 0, filterDecLocked, DragMapping::Linear, false, input.sliceScope ? 1000.0f : 1.0f, true);
    addFilterCell (row2[2], "SUS", formatPercent (filterSusDisplayValue, 1),
                   ParamIds::defaultFilterEnvSustain, IntersectProcessor::FieldFilterEnvSustain, kLockFilterEnvSustain,
                   filterSusValue, 0.0f, input.sliceScope ? 1.0f : 100.0f, input.sliceScope ? 0.001f : 0.1f,
                   0.5f, 1, false, 0, filterSusLocked, DragMapping::Linear, false, input.sliceScope ? 100.0f : 1.0f, true);
    addFilterCell (row2[3], "REL", formatMs (filterRelDisplayValue),
                   ParamIds::defaultFilterEnvRelease, IntersectProcessor::FieldFilterEnvRelease, kLockFilterEnvRelease,
                   input.sliceScope ? filterRelSec : filterRelDisplayValue,
                   0.0f, input.sliceScope ? 10.0f : 10000.0f, input.sliceScope ? 0.001f : 1.0f,
                   1.0f, 0, false, 0, filterRelLocked, DragMapping::Linear, false, input.sliceScope ? 1000.0f : 1.0f, true);
    addFilterCell (row2[4], "AMT", formatSigned (filterAmt, 1, "st"),
                   ParamIds::defaultFilterEnvAmount, IntersectProcessor::FieldFilterEnvAmount, kLockFilterEnvAmount,
                   filterAmt, -96.0f, 96.0f, 0.1f, 0.2f, 1, false, 0, filterAmtLocked);
}

void SignalChainBar::rebuildAmpModule (const LayoutInput& input,
                                       const std::pair<juce::Rectangle<int>, juce::Rectangle<int>>& rows)
{
    const auto* selectedSlice = input.selectedSlice;
    const auto& globals = input.globals;
    const auto row1 = makeRowCells (rows.first, {
        { 1.0f, kCellGap }, { 1.0f, kCellGap }, { 0.92f, 0 }
    });
    const auto row2 = makeRowCells (rows.second, {
        { 1.02f, kCellGap }, { 0.82f, 0 }
    });

    const auto [attackSec, attackLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockAttack, selectedSlice->attackSec, globals.attackSec)
        : std::pair<float, bool> { globals.attackSec, false };
    const auto [decaySec, decayLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockDecay, selectedSlice->decaySec, globals.decaySec)
        : std::pair<float, bool> { globals.decaySec, false };
    const auto [sustain, sustainLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockSustain, selectedSlice->sustainLevel, globals.sustain)
        : std::pair<float, bool> { globals.sustain, false };
    const auto [releaseSec, releaseLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockRelease, selectedSlice->releaseSec, globals.releaseSec)
        : std::pair<float, bool> { globals.releaseSec, false };
    const auto [tail, tailLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockReleaseTail, selectedSlice->releaseTail, globals.releaseTail)
        : std::pair<bool, bool> { globals.releaseTail, false };

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

    const float attackDisplayValue = attackSec * 1000.0f;
    const float decayDisplayValue = decaySec * 1000.0f;
    const float sustainValue = input.sliceScope ? sustain : globals.sustain * 100.0f;
    const float sustainDisplayValue = input.sliceScope ? sustain * 100.0f : globals.sustain * 100.0f;
    const float releaseDisplayValue = releaseSec * 1000.0f;

    addAmpCell (row1[0], "ATK", formatMs (attackDisplayValue),
                ParamIds::defaultAttack, IntersectProcessor::FieldAttack, kLockAttack,
                input.sliceScope ? attackSec : attackDisplayValue,
                0.0f, input.sliceScope ? 1.0f : 1000.0f, input.sliceScope ? 0.001f : 1.0f,
                1.0f, 0, attackLocked, false, input.sliceScope ? 1000.0f : 1.0f, true);
    addAmpCell (row1[1], "DEC", formatMs (decayDisplayValue),
                ParamIds::defaultDecay, IntersectProcessor::FieldDecay, kLockDecay,
                input.sliceScope ? decaySec : decayDisplayValue,
                0.0f, input.sliceScope ? 5.0f : 5000.0f, input.sliceScope ? 0.001f : 1.0f,
                1.0f, 0, decayLocked, false, input.sliceScope ? 1000.0f : 1.0f, true);
    addAmpCell (row1[2], "SUS", formatPercent (sustainDisplayValue, 1),
                ParamIds::defaultSustain, IntersectProcessor::FieldSustain, kLockSustain,
                sustainValue, 0.0f, input.sliceScope ? 1.0f : 100.0f, input.sliceScope ? 0.001f : 0.1f,
                0.5f, 1, sustainLocked, false, input.sliceScope ? 100.0f : 1.0f);
    addAmpCell (row2[0], "REL", formatMs (releaseDisplayValue),
                ParamIds::defaultRelease, IntersectProcessor::FieldRelease, kLockRelease,
                input.sliceScope ? releaseSec : releaseDisplayValue,
                0.0f, input.sliceScope ? 5.0f : 5000.0f, input.sliceScope ? 0.001f : 1.0f,
                1.0f, 0, releaseLocked, false, input.sliceScope ? 1000.0f : 1.0f, true);
    addAmpCell (row2[1], "TAIL", formatBool (tail),
                ParamIds::defaultReleaseTail, IntersectProcessor::FieldReleaseTail, kLockReleaseTail,
                tail ? 1.0f : 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0, tailLocked, true);
}

void SignalChainBar::rebuildOutputModule (const LayoutInput& input,
                                          const std::pair<juce::Rectangle<int>, juce::Rectangle<int>>& rows)
{
    const auto* selectedSlice = input.selectedSlice;
    const auto& globals = input.globals;
    const auto loopNames = juce::StringArray { "Off", "Loop", "PP" };
    const auto row1 = makeRowCells (rows.first, {
        { 0.78f, kCellGap }, { 0.94f, kCellGap }, { 0.84f, 0 }
    });
    const auto row2 = makeRowCells (rows.second, {
        { 1.0f, kCellGap }, { 0.96f, 0 }
    });

    const auto [reverse, reverseLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockReverse, selectedSlice->reverse, globals.reverse)
        : std::pair<bool, bool> { globals.reverse, false };
    const auto [loopMode, loopLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockLoop, selectedSlice->loopMode, globals.loopMode)
        : std::pair<int, bool> { globals.loopMode, false };
    const auto [muteGroup, muteLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockMuteGroup, selectedSlice->muteGroup, globals.muteGroup)
        : std::pair<int, bool> { globals.muteGroup, false };
    const auto [gain, gainLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockVolume, selectedSlice->volume, globals.volumeDb)
        : std::pair<float, bool> { globals.volumeDb, false };
    const auto [outputBus, outputLocked] = input.sliceScope
        ? resolveLockedValue (selectedSlice, kLockOutputBus, selectedSlice->outputBus, 0)
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

    addOutputCell (row1[0], "REV", formatBool (reverse),
                   ParamIds::defaultReverse, IntersectProcessor::FieldReverse, kLockReverse,
                   reverse ? 1.0f : 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0, reverseLocked, true, false, 0, 0.0f, true);
    addOutputCell (row1[1], "LOOP", getChoiceName (loopMode, loopNames),
                   ParamIds::defaultLoop, IntersectProcessor::FieldLoop, kLockLoop,
                   (float) loopMode, 0.0f, 2.0f, 1.0f, 0.0f, 0, loopLocked, false, true, 3, 0.0f, true);
    addOutputCell (row1[2], "MUTE", juce::String (muteGroup),
                   ParamIds::defaultMuteGroup, IntersectProcessor::FieldMuteGroup, kLockMuteGroup,
                   (float) muteGroup, 0.0f, 32.0f, 1.0f, 0.25f, 0, muteLocked);

    addOutputCell (row2[0], "GAIN", formatGain (gain),
                   ParamIds::masterVolume, IntersectProcessor::FieldVolume, kLockVolume,
                   gain, -100.0f, 24.0f, 0.1f, 0.3f, 1, gainLocked, false, false, 0, 0.0f, true);

    if (input.sliceScope)
    {
        addOutputCell (row2[1], "OUT", juce::String (outputBus + 1),
                       {}, IntersectProcessor::FieldOutputBus, kLockOutputBus,
                       (float) outputBus, 0.0f, 15.0f, 1.0f, 0.25f, 0, outputLocked, false, false, 0, 1.0f);
        return;
    }

    addOutputCell (row2[1], "VOICES", juce::String (globals.maxVoices),
                   ParamIds::maxVoices, -1, 0u,
                   (float) globals.maxVoices, 1.0f, 31.0f, 1.0f, 0.25f, 0, false);
}

void SignalChainBar::paint (juce::Graphics& g)
{
    // Rebuild layout when state affecting cells has changed:
    // resize, snapshot version change, or explicit dirty flag from interactions.
    const auto snapshotVer = processor.getUiSliceSnapshotVersion();
    const auto bounds = getLocalBounds();
    if (layoutDirty || snapshotVer != lastSnapshotVersion || bounds != lastBounds)
    {
        rebuildLayout();
        layoutDirty = false;
        lastSnapshotVersion = snapshotVer;
        lastBounds = bounds;
    }

    g.fillAll (getTheme().surface1);

    g.setColour (getTheme().surface1);
    g.fillRect (contextBounds);
    g.setColour (getTheme().surface3.withAlpha (0.8f));
    g.drawHorizontalLine (contextBounds.getY(), (float) contextBounds.getX(), (float) contextBounds.getRight());

    auto infoBounds = contextInfoBounds.reduced (0, 2);
    int infoX = infoBounds.getX();

    if (contextTitle.isNotEmpty())
    {
        g.setFont (IntersectLookAndFeel::fitFontToWidth (contextTitle, 10.0f, 8.5f,
                                                         juce::jmin (140, infoBounds.getWidth()), false));
        g.setColour (getTheme().text0);
        const int titleWidth = juce::jmin (128, infoBounds.getWidth());
        g.drawFittedText (contextTitle, infoX, infoBounds.getY(), titleWidth, infoBounds.getHeight(),
                          juce::Justification::centredLeft, 1);
        infoX += titleWidth + 8;
    }

    if (contextDot1Bounds.getWidth() > 0)
    {
        g.setFont (IntersectLookAndFeel::makeFont (13.0f, true));
        g.setColour (getTheme().text0);
        g.drawText (juce::String::charToString (0x00B7), contextDot1Bounds, juce::Justification::centred);
    }

    if (contextDot2Bounds.getWidth() > 0)
    {
        g.setFont (IntersectLookAndFeel::makeFont (13.0f, true));
        g.setColour (getTheme().text0);
        g.drawText (juce::String::charToString (0x00B7), contextDot2Bounds, juce::Justification::centred);
    }

    if (contextSubtitle.isNotEmpty() && infoX < infoBounds.getRight())
    {
        g.setFont (IntersectLookAndFeel::fitFontToWidth (contextSubtitle, 9.0f, 7.5f,
                                                         infoBounds.getRight() - infoX, false));
        g.setColour (getTheme().text0);
        g.drawFittedText (contextSubtitle, infoX, infoBounds.getY(),
                          infoBounds.getRight() - infoX, infoBounds.getHeight(),
                          juce::Justification::centredLeft, 1);
    }

    if (contextStatusBounds.getWidth() > 0 && contextStatus.isNotEmpty())
    {
        g.setFont (IntersectLookAndFeel::makeFont (8.6f, true));
        g.setColour (getTheme().color5);
        g.drawText (contextStatus, contextStatusBounds.withTrimmedRight (14), juce::Justification::centredRight);
    }

    auto drawContextMetric = [&] (const juce::Rectangle<int>& metricBounds, const juce::String& label,
                                   const juce::String& value, juce::Colour valueColour)
    {
        if (metricBounds.getWidth() <= 0)
            return;
        auto labelFont = IntersectLookAndFeel::makeFont (8.0f, true);
        auto valueFont = IntersectLookAndFeel::fitFontToWidth (value, 10.0f, 8.5f,
                                                                metricBounds.getWidth() - 14, false);
        const int labelW = juce::roundToInt (measureTextWidth (labelFont, label)) + 1;
        const int pairY = metricBounds.getY() + (metricBounds.getHeight() - 12) / 2;

        g.setFont (labelFont);
        g.setColour (getTheme().text0.withAlpha (0.6f));
        g.drawText (label, metricBounds.getX(), pairY, labelW, 12,
                    juce::Justification::centredLeft);

        g.setFont (valueFont);
        g.setColour (valueColour);
        g.drawText (value, metricBounds.getX() + labelW + 3, pairY,
                    metricBounds.getWidth() - labelW - 3, 12,
                    juce::Justification::centredLeft);
    };

    {
        const auto& ui = processor.getUiSliceSnapshot();
        const bool rootEditable = (ui.numSlices == 0);
        drawContextMetric (contextSlicesBounds, "SLICES", juce::String (ui.numSlices),
                           getTheme().text0);
        drawContextMetric (contextRootBounds, "ROOT", juce::String (ui.rootNote),
                           rootEditable ? getTheme().text0 : getTheme().text2.withAlpha (0.6f));
    }

    g.setColour (getTheme().surface2);
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
            g.setColour (getTheme().color5);
            g.drawText (juce::String (module.overrideCount),
                        module.headerBounds.getX(), module.headerBounds.getY(),
                        module.headerBounds.getWidth(), module.headerBounds.getHeight(),
                        juce::Justification::centredRight);
        }

        if (mi > 0)
        {
            g.setColour (getTheme().surface3);
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
    auto accent = getTheme().accent;
    auto inactiveBase = getTheme().text0;
    auto text = cell.isActive ? accent : inactiveBase.withAlpha (cell.isEnabled ? 1.0f : 0.35f);

    g.setFont (IntersectLookAndFeel::fitFontToWidth (cell.valueText, 9.5f, 8.0f, cell.bounds.getWidth() - 8, true));
    g.setColour (text);
    g.drawFittedText (cell.valueText, cell.bounds.reduced (4, 0), juce::Justification::centred, 1);

    if (cell.isActive)
    {
        g.setColour (accent);
        auto barBounds = cell.bounds.reduced (2, 0);
        g.fillRect (barBounds.getX(), cell.bounds.getBottom() - 4, barBounds.getWidth(), 2);
    }

    if (cell.tabTarget == TabTarget::Global)
    {
        g.setColour (getTheme().surface3);
        g.fillRect (cell.bounds.getRight(), cell.bounds.getY(), 1, cell.bounds.getHeight());
    }
}

void SignalChainBar::drawSetBpmCell (juce::Graphics& g, const Cell& cell) const
{
    drawMiniButton (g, cell.bounds,
                    cell.valueText,
                    getTheme().surface4.withAlpha (0.96f),
                    getTheme().surface3.withAlpha (0.72f),
                    getTheme().accent,
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
            ? getTheme().surface5.interpolatedWith (getTheme().color2, 0.18f)
            : getTheme().surface4).withMultipliedAlpha (alpha);
        const auto outline = (cell.currentValue > 0.5f
            ? getTheme().color2.withAlpha (0.75f)
            : getTheme().surface3.withAlpha (0.82f)).withMultipliedAlpha (alpha);
        const auto text = (cell.currentValue > 0.5f
            ? getTheme().color2.brighter (0.5f)
            : getTheme().text0.withAlpha (0.96f)).withMultipliedAlpha (alpha);

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
            g.setColour (getTheme().text0.brighter (0.15f).withAlpha (alpha));
            g.drawText (cell.label, contentBounds.getX(), contentBounds.getY(),
                        labelWidth, contentBounds.getHeight(), juce::Justification::centredLeft);
        }

        g.setFont (valueFont);
        g.setColour ((cell.isLocked ? getTheme().text1 : getTheme().text2).withAlpha (alpha));
        g.drawFittedText (cell.valueText, contentBounds.getX() + labelWidth + labelGap, contentBounds.getY(),
                          contentBounds.getWidth() - labelWidth - labelGap, contentBounds.getHeight(),
                          juce::Justification::centredLeft, 1);
        return;
    }

    if (cell.label.isNotEmpty())
    {
        g.setFont (IntersectLookAndFeel::fitFontToWidth (cell.label, 7.5f, 6.6f, contentBounds.getWidth(), true));
        g.setColour ((cell.isLocked ? getTheme().color5 : getTheme().text0).withAlpha (alpha));
        g.drawFittedText (cell.label,
                          contentBounds.getX(),
                          contentBounds.getY(),
                          contentBounds.getWidth(),
                          kLabelHeight,
                          juce::Justification::centredLeft,
                          1);
    }

    if (cell.valueText.isNotEmpty())
    {
        const bool isOverride = cell.isLocked && isSliceScopeActive()
            && ! cell.isHeaderControl && ! cell.isContextInline;
        auto valueColour = getTheme().text1;
        if (! isOverride && cell.isBoolean)
            valueColour = cell.currentValue > 0.5f ? getTheme().accent : getTheme().surface3.brighter (0.6f);
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
        cmd.sliceIdx = processor.sliceManager.selectedSlice.load();
        processor.pushCommand (cmd);
        layoutDirty = true;
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
        layoutDirty = true;
    }
}

void SignalChainBar::clearSliceOverride (uint32_t lockBit)
{
    if (lockBit == 0)
        return;

    IntersectProcessor::Command cmd;
    cmd.type = IntersectProcessor::CmdToggleLock;
    cmd.intParam1 = (int) lockBit;
    cmd.sliceIdx = processor.sliceManager.selectedSlice.load();
    processor.pushCommand (cmd);
    layoutDirty = true;
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

void SignalChainBar::dismissTextEditor()
{
    if (textEditor == nullptr)
        return;

    // This must only run after any active TextEditor callback has returned.
    // Clearing the callback members here destroys their std::function storage.
    textEditor->onReturnKey = nullptr;
    textEditor->onEscapeKey = nullptr;
    textEditor->onFocusLost = nullptr;
    textEditor.reset();
}

void SignalChainBar::showSetBpmPopup()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addSectionHeader ("Set BPM");
    menu.addItem (1, "16 Bars");
    menu.addItem (2, "8 Bars");
    menu.addItem (3, "4 Bars");
    menu.addItem (4, "2 Bars");
    menu.addItem (5, "1 Bar");
    menu.addItem (6, "1/2 Note");
    menu.addItem (7, "1/4 Note");
    menu.addItem (8, "1/8 Note");
    menu.addItem (9, "1/16 Note");

    auto* topLevel = getTopLevelComponent();
    const float menuScale = IntersectLookAndFeel::getMenuScale();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withDeletionCheck (*this)
                            .withParentComponent (topLevel)
                            .withMaximumNumColumns (1)
                            .withMinimumWidth ((int) std::round (156.0f * menuScale))
                            .withStandardItemHeight ((int) std::round (24.0f * menuScale)),
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
                cmd.sliceIdx = processor.sliceManager.selectedSlice.load();
                processor.pushCommand (cmd);
                layoutDirty = true;
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
    dismissTextEditor();
    textEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*textEditor);
    auto valueBounds = cell.bounds.reduced (kCellInsetX, 0).withTrimmedTop (kValueYOffset).expanded (1, 1);
    textEditor->setBounds (valueBounds);
    textEditor->setFont (IntersectLookAndFeel::fitFontToWidth (cell.valueText, 10.5f, 8.5f,
                                                                valueBounds.getWidth(), false));
    textEditor->setColour (juce::TextEditor::backgroundColourId, getTheme().surface2.brighter (0.12f).withAlpha (0.98f));
    textEditor->setColour (juce::TextEditor::textColourId, getTheme().text1.brighter (0.3f));
    textEditor->setColour (juce::TextEditor::outlineColourId, getTheme().surface5.withAlpha (0.85f));
    textEditor->setColour (juce::TextEditor::focusedOutlineColourId, getTheme().accent.withAlpha (0.9f));
    textEditor->setColour (juce::TextEditor::highlightColourId, getTheme().accent.withAlpha (0.25f));
    textEditor->setJustification (juce::Justification::centredLeft);
    textEditor->setBorder (juce::BorderSize<int> (1, 4, 1, 4));
    textEditor->setIndents (0, 0);
    textEditor->setEscapeAndReturnKeysConsumed (true);
    textEditor->setText (formatTrimmed (storedToDisplay (cell, cell.currentValue), cell.textDecimals), false);
    textEditor->selectAll();
    textEditor->grabKeyboardFocus();

    juce::Component::SafePointer<SignalChainBar> safeThis (this);
    auto dismissEditorLater = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr)
                return;

            safeThis->dismissTextEditor();
            safeThis->repaint();
        });
    };

    textEditor->onReturnKey = [safeThis, cell]
    {
        if (safeThis == nullptr || safeThis->textEditor == nullptr)
            return;

        float displayValue = safeThis->textEditor->getText().getFloatValue();
        displayValue = safeThis->clampDisplayValue (cell, displayValue);
        auto storedValue = safeThis->displayToStored (cell, displayValue);
        if (cell.step > 0.0f)
            storedValue = std::round (storedValue / cell.step) * cell.step;

        juce::MessageManager::callAsync ([safeThis, cell, storedValue]
        {
            if (safeThis == nullptr)
                return;

            safeThis->dismissTextEditor();
            safeThis->applyCellValue (cell, storedValue, ! safeThis->isSliceScopeActive());
            safeThis->repaint();
        });
    };

    textEditor->onEscapeKey = [safeThis, dismissEditorLater]
    {
        if (safeThis == nullptr)
            return;

        dismissEditorLater();
    };

    textEditor->onFocusLost = [safeThis, dismissEditorLater]
    {
        if (safeThis == nullptr)
            return;

        dismissEditorLater();
    };
}

void SignalChainBar::showRootEditor()
{
    const auto& ui = processor.getUiSliceSnapshot();
    auto editorBounds = contextRootBounds.reduced (0, 4);

    dismissTextEditor();
    textEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*textEditor);
    textEditor->setBounds (editorBounds);
    textEditor->setFont (IntersectLookAndFeel::makeFont (10.0f));
    textEditor->setColour (juce::TextEditor::backgroundColourId, getTheme().surface1.brighter (0.15f));
    textEditor->setColour (juce::TextEditor::textColourId, getTheme().text2);
    textEditor->setColour (juce::TextEditor::outlineColourId, getTheme().surface5.withAlpha (0.85f));
    textEditor->setColour (juce::TextEditor::focusedOutlineColourId, getTheme().accent.withAlpha (0.9f));
    textEditor->setBorder (juce::BorderSize<int> (1, 4, 1, 4));
    textEditor->setEscapeAndReturnKeysConsumed (true);
    textEditor->setText (juce::String (ui.rootNote), false);
    textEditor->selectAll();
    textEditor->grabKeyboardFocus();

    juce::Component::SafePointer<SignalChainBar> safeThis (this);
    auto dismissEditorLater = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr)
                return;

            safeThis->dismissTextEditor();
            safeThis->repaint();
        });
    };

    textEditor->onReturnKey = [safeThis]
    {
        if (safeThis == nullptr || safeThis->textEditor == nullptr)
            return;
        const int newRootNote = juce::jlimit (0, 127, safeThis->textEditor->getText().getIntValue());

        juce::MessageManager::callAsync ([safeThis, newRootNote]
        {
            if (safeThis == nullptr)
                return;

            safeThis->dismissTextEditor();

            IntersectProcessor::Command cmd;
            cmd.type = IntersectProcessor::CmdSetRootNote;
            cmd.intParam1 = newRootNote;
            safeThis->processor.pushCommand (cmd);
            safeThis->repaint();
        });
    };
    textEditor->onEscapeKey = [safeThis, dismissEditorLater]
    {
        if (safeThis == nullptr)
            return;

        dismissEditorLater();
    };
    textEditor->onFocusLost = [safeThis, dismissEditorLater]
    {
        if (safeThis == nullptr)
            return;

        dismissEditorLater();
    };
}

void SignalChainBar::mouseDown (const juce::MouseEvent& e)
{
    rebuildLayout();

    dismissTextEditor();

    endGlobalGesture();
    activeDragCell = -1;
    draggingRoot = false;

    const auto pos = e.getPosition();

    // ROOT drag interaction (only when no slices exist)
    if (contextRootBounds.getWidth() > 0 && contextRootBounds.contains (pos))
    {
        const auto& ui = processor.getUiSliceSnapshot();
        if (ui.numSlices == 0)
        {
            draggingRoot = true;
            rootDragStartY = e.y;
            rootDragStartValue = (float) ui.rootNote;
            return;
        }
    }
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
            layoutDirty = true;
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
    if (draggingRoot)
    {
        const float deltaY = (float) (rootDragStartY - e.y);
        const int newVal = juce::jlimit (0, 127, (int) (rootDragStartValue + deltaY * (127.0f / 200.0f)));
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdSetRootNote;
        cmd.intParam1 = newVal;
        processor.pushCommand (cmd);
        return;
    }

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

    float interactionValue = dragStartInteractionValue + deltaY * dragStep;
    interactionValue = clampInteractionValue (cell, interactionValue);

    float storedValue = interactionToStored (cell, interactionValue);

    if (e.mods.isShiftDown())
    {
        const float fineSnap = juce::jmax (cell.step, 1.0e-4f);
        storedValue = clampStoredValue (cell, std::round (storedValue / fineSnap) * fineSnap);
    }
    else
    {
        float displayValue = storedToDisplay (cell, storedValue);
        displayValue = std::round (displayValue);
        storedValue = clampStoredValue (cell, displayToStored (cell, displayValue));
    }
    applyCellValue (cell, storedValue, false);
    repaint();
}

void SignalChainBar::mouseUp (const juce::MouseEvent&)
{
    activeDragCell = -1;
    draggingRoot = false;
    endGlobalGesture();
}

void SignalChainBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    rebuildLayout();

    // ROOT double-click text editor
    if (contextRootBounds.getWidth() > 0 && contextRootBounds.contains (e.getPosition()))
    {
        const auto& ui = processor.getUiSliceSnapshot();
        if (ui.numSlices == 0)
        {
            showRootEditor();
            return;
        }
    }

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
