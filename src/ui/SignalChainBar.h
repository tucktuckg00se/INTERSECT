#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>

class IntersectProcessor;

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

    void rebuildLayout();
    void syncScopeFromSelection();
    bool isSliceScopeActive() const;

    float storedToDisplay (const Cell& cell, float storedValue) const;
    float displayToStored (const Cell& cell, float displayValue) const;
    float clampStoredValue (const Cell& cell, float storedValue) const;
    float clampDisplayValue (const Cell& cell, float displayValue) const;

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

    void showSetBpmPopup();
    void showTextEditor (const Cell& cell);

    int countModuleOverrides (const ModuleLayout& module, uint32_t lockMask) const;
    int countAllOverrides (uint32_t lockMask) const;

    IntersectProcessor& processor;
    Scope scope = Scope::Global;
    bool lastHadValidSlice = false;
    int activeDragCell = -1;
    int dragStartY = 0;
    float dragStartDisplayValue = 0.0f;
    juce::String activeGlobalParamId;
    bool globalGestureActive = false;

    juce::Rectangle<int> contextBounds;
    juce::Rectangle<int> contextInfoBounds;
    juce::Rectangle<int> contextStatusBounds;
    juce::Rectangle<int> moduleStripBounds;
    juce::String contextTitle;
    juce::String contextSubtitle;
    juce::String contextStatus;

    std::vector<Cell> cells;
    std::array<ModuleLayout, 4> modules {};
    std::unique_ptr<juce::TextEditor> textEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalChainBar)
};
