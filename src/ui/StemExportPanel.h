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

    void close();
    int hitTestCell (juce::Point<int> pos) const;

    IntersectProcessor& processor;
    int targetSampleId;

    OptionCell modelCell;
    OptionCell deviceCell;
    OptionCell outputCell;

    std::vector<StemModelId> installedModels;
    int selectedModelIndex = 0;
    StemComputeDevice selectedDevice = StemComputeDevice::cpu;
    juce::File customOutputFolder;
    bool useCustomFolder = false;

    juce::TextButton separateBtn { "SEPARATE" };
    juce::TextButton browseBtn   { "..." };
    juce::TextButton cancelBtn   { "CANCEL" };

    std::unique_ptr<juce::FileChooser> fileChooser;
};
