#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../audio/StemSeparation.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

class IntersectProcessor;

/**
    The full-view stem separation screen that replaces the editor area below the sample lane,
    like the file browser.

    Models (with inline download) on the left | grouped stem checklist with search on the right |
    device / mode / output / START along the bottom. Starting a job and downloading go straight to
    the processor. The editor saves the remembered model, stem choices and mode when it closes.
*/
class StemView : public juce::Component,
                 private juce::Timer
{
public:
    explicit StemView (IntersectProcessor& p);
    ~StemView() override;

    /** Re-reads the catalog and installed models and targets sampleId. */
    void openForSample (int sampleId);
    int getTargetSampleId() const noexcept { return targetSampleId; }
    void refreshThemeColours();

    // ---- Persisted UI choices (the editor saves and restores these) ----
    juce::String getLastModelId() const;
    void setLastModelId (const juce::String& modelId) { lastModelId = modelId; }
    /** One "modelId=name1,name2" entry per model with a remembered selection. */
    juce::StringArray getRememberedSelections() const;
    void setRememberedSelections (const juce::StringArray& entries);
    StemExportMode getExportMode() const noexcept { return exportMode; }
    void setExportMode (StemExportMode mode) noexcept { exportMode = mode; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void visibilityChanged() override;

    std::function<void()> onCloseRequested;

private:
    class ModelList;
    class StemChecklist;

    struct OptionCell
    {
        juce::Rectangle<int> bounds;
        juce::String label;
        juce::String value;
        bool enabled = true;
    };

    void timerCallback() override;
    void reloadCatalog();
    void selectModel (int index);
    const StemModelCatalogEntry* getSelectedModel() const;
    bool isInstalled (const StemModelId& modelId) const;
    bool isDownloading (const StemModelId& modelId) const;
    std::set<juce::String>& selectionForSelectedModel();
    void selectionChanged();
    void updateControls();
    void updateDeviceCell();
    void startSeparation();
    void chooseOutputFolder();
    juce::String getTargetSampleName() const;
    juce::String getStartBlockReason() const;

    IntersectProcessor& processor;
    int targetSampleId = -1;

    std::vector<StemModelCatalogEntry> catalog;
    std::vector<StemModelId> installedModels;
    int selectedModelIndex = -1;
    juce::String lastModelId;
    std::map<StemModelId, std::set<juce::String>> selections;   // stem names per model

    StemComputeDevice device = StemComputeDevice::cpu;
    StemExportMode exportMode = StemExportMode::separate;
    juce::File customOutputFolder;
    bool ortSupported = false;
    bool ortInstalled = false;
    bool wasDownloading = false;
    bool wasJobRunning = false;

    std::unique_ptr<ModelList> modelList;
    std::unique_ptr<StemChecklist> checklist;
    juce::Viewport modelViewport;
    juce::Viewport stemViewport;
    juce::TextEditor searchEditor;
    juce::TextButton allButton { "ALL" };
    juce::TextButton noneButton { "NONE" };
    juce::TextButton downloadButton { "DOWNLOAD" };
    juce::TextButton browseButton { "..." };
    juce::TextButton startButton { "START" };
    juce::TextButton closeButton { "CLOSE" };
    std::unique_ptr<juce::FileChooser> folderChooser;

    OptionCell deviceCell;
    OptionCell modeCell;
    OptionCell outputCell;
    juce::Rectangle<int> detailArea;
    juce::Rectangle<int> progressArea;
    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> messageArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemView)
};
