#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Constants.h"
#include "../params/GlobalParamSnapshot.h"
#include <array>
#include <functional>
#include <vector>

class IntersectProcessor;
struct Slice;

class SignalChainBar : public juce::Component
{
public:
    explicit SignalChainBar (IntersectProcessor& p);

    enum class Module
    {
        Playback = 0,
        Filter,
        Amp,
        Output,
    };

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    void markLayoutDirty() { layoutDirty = true; }
    bool isExpanded() const { return expanded; }
    float getDesiredHeight() const;
    std::function<void()> onHeightChanged;

private:
    enum class Scope
    {
        Global,
        Slice,
    };

    enum class CellKind
    {
        Param,
        Tab,
        SetBpm,
    };

    enum class DragMapping
    {
        Linear,
        FilterCutoff,
    };

    enum class TabTarget
    {
        None,
        Global,
        Slice,
    };

    struct Cell
    {
        CellKind kind = CellKind::Param;
        TabTarget tabTarget = TabTarget::None;
        Module module = Module::Playback;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> overrideBounds;
        juce::String label;
        juce::String valueText;
        juce::String globalParamId;
        int fieldId = -1;
        uint32_t lockBit = 0;
        DragMapping dragMapping = DragMapping::Linear;
        float currentValue = 0.0f;
        float minVal = 0.0f;
        float maxVal = 1.0f;
        float step = 1.0f;
        float displayScale = 1.0f;
        float displayOffset = 0.0f;
        float dragPerPixel = 0.0f;
        int choiceCount = 0;
        int textDecimals = 0;
        bool isBoolean = false;
        bool isChoice = false;
        bool isReadOnly = false;
        bool isLocked = false;
        bool isEnabled = true;
        bool isActive = false;
        bool isHeaderControl = false;
        bool isContextInline = false;
        bool isVisuallyDimmed = false;
        bool drawTrailingDivider = false;
        bool isSliceScopeCell = false;
    };

    struct ModuleLayout
    {
        Module module = Module::Playback;
        juce::String name;
        juce::Colour titleColour;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> headerBounds;
        juce::Rectangle<int> bodyBounds;
        int overrideCount = 0;
    };

    struct LayoutInput
    {
        GlobalParamSnapshot globals;
        const Slice* selectedSlice = nullptr;
        int numSlices = 0;
        int selectedSliceIndex = -1;
        int rootNote = kDefaultRootNote;
        int sampleNumFrames = 0;
        bool sampleLoaded = false;
        bool sampleMissing = false;
        bool hasValidSlice = false;
        bool sliceScope = false;
        float sampleRate = 44100.0f;
    };

    void rebuildLayout();
    void rebuildModuleStrip (const LayoutInput& input,
                             const juce::Rectangle<int>& stripBounds,
                             std::array<ModuleLayout, 4>& targetModules);
    void rebuildContextBar (const LayoutInput& input);
    void rebuildPlaybackModule (const LayoutInput& input,
                                const std::pair<juce::Rectangle<int>, juce::Rectangle<int>>& rows,
                                const ModuleLayout& moduleLayout);
    void rebuildFilterModule (const LayoutInput& input,
                              const std::pair<juce::Rectangle<int>, juce::Rectangle<int>>& rows,
                              const ModuleLayout& moduleLayout);
    void rebuildAmpModule (const LayoutInput& input,
                           const std::pair<juce::Rectangle<int>, juce::Rectangle<int>>& rows);
    void rebuildOutputModule (const LayoutInput& input,
                              const std::pair<juce::Rectangle<int>, juce::Rectangle<int>>& rows);
    void syncScopeFromSelection();
    bool isSliceScopeActive() const;

    float storedToDisplay (const Cell& cell, float storedValue) const;
    float displayToStored (const Cell& cell, float displayValue) const;
    float clampStoredValue (const Cell& cell, float storedValue) const;
    float clampDisplayValue (const Cell& cell, float displayValue) const;
    float storedToInteraction (const Cell& cell, float storedValue) const;
    float interactionToStored (const Cell& cell, float interactionValue) const;
    float clampInteractionValue (const Cell& cell, float interactionValue) const;

    void addTabCell (const juce::Rectangle<int>& bounds,
                     const juce::String& text,
                     TabTarget target,
                     bool isActive,
                     bool isEnabled);
    void addParamCell (const Cell& cell);

    void drawTabCell (juce::Graphics& g, const Cell& cell) const;
    void drawSetBpmCell (juce::Graphics& g, const Cell& cell) const;
    void drawParamCell (juce::Graphics& g, const Cell& cell) const;

    void toggleBooleanCell (const Cell& cell);
    void cycleChoiceCell (const Cell& cell);
    void applyCellValue (const Cell& cell, float storedValue, bool oneShotGlobal);
    void clearSliceOverride (uint32_t lockBit);

    void beginGlobalGesture (const Cell& cell);
    void endGlobalGesture();

    void dismissTextEditor();
    void showSetBpmPopup (bool sliceScope);
    void showTextEditor (const Cell& cell);
    void showRootEditor();

    int countModuleOverrides (const ModuleLayout& module, uint32_t lockMask) const;
    int countEffectiveModuleOverrides (Module module, const Slice& slice, const GlobalParamSnapshot& globals) const;
    int countAllOverrides (uint32_t lockMask) const;
    int countAllEffectiveOverrides (const Slice& slice, const GlobalParamSnapshot& globals) const;

    IntersectProcessor& processor;
    Scope scope = Scope::Global;
    bool lastHadValidSlice = false;
    int activeDragCell = -1;
    int dragStartY = 0;
    float dragStartInteractionValue = 0.0f;
    juce::String activeGlobalParamId;
    bool globalGestureActive = false;

    juce::Rectangle<int> contextBounds;
    juce::Rectangle<int> contextInfoBounds;
    juce::Rectangle<int> contextStatusBounds;
    juce::Rectangle<int> contextDot1Bounds;
    juce::Rectangle<int> contextDot2Bounds;
    juce::Rectangle<int> contextSlicesBounds;
    juce::Rectangle<int> contextRootBounds;
    juce::Rectangle<int> moduleStripBounds;
    juce::Rectangle<int> globalStripBounds;
    juce::Rectangle<int> sliceStripBounds;
    juce::Rectangle<int> separatorBounds;
    juce::Rectangle<int> expandToggleBounds;
    juce::String contextTitle;
    juce::String contextSubtitle;
    juce::String contextStatus;

    bool expanded = false;
    bool draggingRoot = false;
    int rootDragStartY = 0;
    float rootDragStartValue = (float) kDefaultRootNote;

    std::vector<Cell> cells;
    std::array<ModuleLayout, 4> modules {};
    std::array<ModuleLayout, 4> sliceModules {};
    std::unique_ptr<juce::TextEditor> textEditor;

    // Layout cache: rebuild only when inputs have changed.
    bool layoutDirty = true;
    uint32_t lastSnapshotVersion = 0;
    juce::Rectangle<int> lastBounds;
    void resized() override { layoutDirty = true; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalChainBar)
};
