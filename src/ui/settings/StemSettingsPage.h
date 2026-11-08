#pragma once
#include "SettingsPages.h"
#include "../../audio/StemSeparation.h"

/**
    Stem separation settings: model folder, default compute device, ONNX Runtime bundles and the
    model downloads (kept here as well as in STEMS, which needs a sample to open).

    The manifest and model folder are read on open, when a download finishes and when a new
    manifest arrives, never every tick; ticks only refresh download progress.
*/
class StemSettingsPage final : public SettingsPage
{
public:
    StemSettingsPage (IntersectProcessor& p, SettingsHost& h);
    ~StemSettingsPage() override;

    void reload() override;
    void tick() override;
    void refreshThemeColours() override;

protected:
    void layout (SettingsUi::RowLayout& rows) override;

private:
    void reloadCatalog();
    void rebuildRows();
    void updateRows();
    void chooseModelFolder();
    void downloadMissing();
    void onBundleAction (size_t index);
    void onModelAction (size_t index);

    bool isModelInstalled (const StemModelId& id) const;
    bool hasModelUpdate (const StemModelId& id) const;
    bool isModelDownloading (const StemModelId& id) const;
    bool isAnyModelDownloading() const;
    bool isOrtDownloading() const;
    std::vector<StemModelId> getMissingOrOutdatedModels() const;

    PathField folderField;
    juce::TextButton chooseButton { "CHOOSE..." };
    juce::TextButton defaultButton { "DEFAULT" };
    juce::TextButton openButton { "OPEN" };
    SegmentedControl deviceChips;
    juce::String gpuProvider;
    juce::TextButton downloadMissingButton { "DOWNLOAD MISSING" };
    juce::TextButton cancelButton { "CANCEL" };
    juce::String installedSummary;

    std::vector<OrtBundleCatalogEntry> bundles;
    std::vector<OrtBundleId> installedBundles;
    juce::String activeBundleDir;
    std::vector<std::unique_ptr<ItemRow>> bundleRows;

    std::vector<StemModelCatalogEntry> catalog;
    std::vector<StemModelId> installedModels;
    std::vector<StemModelId> updateModels;
    std::vector<std::unique_ptr<ItemRow>> modelRows;

    bool wasModelDownloading = false;
    bool wasOrtDownloading = false;
    int seenManifestGeneration = -1;
    std::unique_ptr<juce::FileChooser> chooser;
};
