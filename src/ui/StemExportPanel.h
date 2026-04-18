#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../audio/StemSeparation.h"
#include <memory>
#include <vector>

class IntersectProcessor;

class StemExportPanel : public juce::Component
{
public:
    StemExportPanel (IntersectProcessor& p, int sampleId);
    ~StemExportPanel() override;
    int getTargetSampleId() const noexcept { return targetSampleId; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    struct OptionCell
    {
        juce::Rectangle<int> bounds;
        juce::String label;
        juce::String displayValue;
    };

    struct StemToggle
    {
        juce::Rectangle<int> bounds;
        juce::String label;
        bool selected = false;
    };

    void close();
    int hitTestCell (juce::Point<int> pos) const;
    int hitTestStemToggle (juce::Point<int> pos) const;
    void rebuildStemToggles();
    void updateSelectedModelDisplay();
    void updateStartButtonState();
    StemSelectionMask getStemSelectionMask() const;

    IntersectProcessor& processor;
    int targetSampleId;

    OptionCell modelCell;
    OptionCell deviceCell;
    OptionCell modeCell;
    OptionCell outputCell;

    std::vector<StemModelId> installedModels;
    std::vector<StemRole> availableRoles;
    std::vector<StemToggle> stemToggles;
    int selectedModelIndex = 0;
    StemComputeDevice selectedDevice = StemComputeDevice::cpu;
    StemExportMode selectedExportMode = StemExportMode::combine;
    juce::File customOutputFolder;
    bool useCustomFolder = false;
    bool ortAvailable = false;
    bool ortSupportedOnPlatform = false;
    bool ortBundleInstalled = false;

    juce::TextButton startBtn    { "START" };
    juce::TextButton browseBtn   { "..." };
    juce::TextButton cancelBtn   { "CANCEL" };

    std::unique_ptr<juce::FileChooser> fileChooser;
};
