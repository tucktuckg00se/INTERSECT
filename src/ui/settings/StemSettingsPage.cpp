#include "StemSettingsPage.h"
#include "../IntersectLookAndFeel.h"
#include "../../PluginProcessor.h"
#include <algorithm>

using namespace SettingsUi;

namespace
{
juce::String separatorDot()
{
    return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
}

juce::String formatBytes (juce::int64 bytes)
{
    if (bytes <= 0)
        return {};
    if (bytes >= 1000 * 1000 * 1000)
        return juce::String ((double) bytes / 1.0e9, 1) + " GB";
    return juce::String (juce::roundToInt ((double) bytes / 1.0e6)) + " MB";
}

juce::String formatPercent (float progress)
{
    return juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, progress) * 100.0f)) + "%";
}

template <typename T>
bool vectorContains (const std::vector<T>& items, const T& item)
{
    return std::find (items.begin(), items.end(), item) != items.end();
}
} // namespace

StemSettingsPage::StemSettingsPage (IntersectProcessor& p, SettingsHost& h)
    : SettingsPage (p, h)
{
    for (auto* c : std::initializer_list<juce::Component*> { &folderField, &chooseButton, &defaultButton, &openButton,
                                                             &deviceChips, &downloadMissingButton, &cancelButton })
        addAndMakeVisible (*c);

    chooseButton.setTooltip ("Pick the folder stem models are downloaded to and loaded from");
    chooseButton.onClick = [this] { chooseModelFolder(); };
    defaultButton.setTooltip ("Use the default model folder");
    defaultButton.onClick = [this]
    {
        processor.setStemModelFolder ({});
        host.persistSettings();
        reload();
    };
    openButton.setTooltip ("Show in the file manager");
    openButton.onClick = [this]
    {
        const auto dir = processor.getResolvedStemModelFolder();
        if (dir.createDirectory().wasOk())
            dir.startAsProcess();
        else
            processor.showTransientStatusMessage ("Couldn't create " + dir.getFullPathName(), true);
    };

    deviceChips.onChange = [this] (int index)
    {
        processor.setStemComputeDevice (index == 1 ? StemComputeDevice::gpu : StemComputeDevice::cpu);
        host.persistSettings();
    };

    downloadMissingButton.onClick = [this] { downloadMissing(); };
    cancelButton.setTooltip ("Cancel the model download");
    cancelButton.onClick = [this]
    {
        processor.cancelStemModelDownload();
        updateRows();
    };

    refreshThemeColours();
}

StemSettingsPage::~StemSettingsPage() = default;

//==============================================================================
// State

bool StemSettingsPage::isModelInstalled (const StemModelId& id) const { return vectorContains (installedModels, id); }
bool StemSettingsPage::hasModelUpdate (const StemModelId& id) const   { return vectorContains (updateModels, id); }

bool StemSettingsPage::isAnyModelDownloading() const
{
    return processor.getUiSliceSnapshot().stemDownloadState == StemModelDownloadState::downloading;
}

bool StemSettingsPage::isModelDownloading (const StemModelId& id) const
{
    return isAnyModelDownloading() && processor.getStemModelDownloadJob().getCurrentModelId() == id;
}

bool StemSettingsPage::isOrtDownloading() const
{
    return processor.getOrtBundleDownloadJob().getState() == StemModelDownloadState::downloading;
}

std::vector<StemModelId> StemSettingsPage::getMissingOrOutdatedModels() const
{
    std::vector<StemModelId> models = updateModels;
    for (const auto& entry : catalog)
        if (! isModelInstalled (entry.id) && entry.downloadUrl.isNotEmpty() && ! vectorContains (models, entry.id))
            models.push_back (entry.id);
    return models;
}

//==============================================================================

void StemSettingsPage::reload()
{
    // Model fixes are published as new manifests; pick them up while settings are open.
    processor.refreshStemModelManifestIfStale();
    seenManifestGeneration = processor.getStemManifestGeneration();
    wasModelDownloading = isAnyModelDownloading();
    wasOrtDownloading = isOrtDownloading();
    reloadCatalog();
}

void StemSettingsPage::reloadCatalog()
{
    const auto modelFolder = processor.getResolvedStemModelFolder();
    catalog = getEffectiveStemModelCatalog (modelFolder);
    installedModels = processor.getInstalledStemModels();
    updateModels = processor.getStemModelUpdates();

    bundles = getOrtBundlesForCurrentPlatform();
    installedBundles = processor.getInstalledOrtBundles();
    activeBundleDir = processor.getActiveOrtBundleDirectoryName();

    folderField.setPath (modelFolder, {});
    setButtonEnabled (defaultButton, processor.getStemModelFolder() != juce::File());

    gpuProvider = getAvailableGpuProviderName();   // reads the active-runtime file: not per layout
    deviceChips.setOptions ({ "CPU", gpuProvider.isNotEmpty() ? "GPU (" + gpuProvider + ")" : juce::String ("GPU") });
    deviceChips.setSelectedIndex (processor.getStemComputeDevice() == StemComputeDevice::gpu ? 1 : 0);

    rebuildRows();
    updateRows();
    relayout();
}

void StemSettingsPage::rebuildRows()
{
    auto resize = [this] (std::vector<std::unique_ptr<ItemRow>>& rows, size_t count, bool isBundle)
    {
        if (rows.size() == count)
            return;

        rows.clear();
        for (size_t i = 0; i < count; ++i)
        {
            auto row = std::make_unique<ItemRow>();
            row->onAction = [this, i, isBundle] { isBundle ? onBundleAction (i) : onModelAction (i); };
            addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
    };

    resize (bundleRows, bundles.size(), true);
    resize (modelRows, catalog.size(), false);
}

void StemSettingsPage::updateRows()
{
    // ---- ONNX Runtime ----
    const bool ortDownloading = isOrtDownloading();
    const auto ortProgress = processor.getOrtBundleDownloadJob().getProgress();
    const auto downloadingBundle = processor.getOrtBundleDownloadJob().getCurrentBundleId();

    for (size_t i = 0; i < bundles.size() && i < bundleRows.size(); ++i)
    {
        const auto& entry = bundles[i];
        auto& row = *bundleRows[i];
        const bool installed = vectorContains (installedBundles, entry.id);
        const bool active = installed && entry.directoryName == activeBundleDir;
        const bool downloadingThis = ortDownloading && downloadingBundle == entry.id;

        juce::String status;
        if (downloadingThis)
            status = "Downloading " + formatPercent (ortProgress);
        else if (active)
            status = "Active";
        else if (installed)
            status = "Installed";
        else if (const auto size = formatBytes (entry.downloadBytes); size.isNotEmpty())
            status = "Not installed" + separatorDot() + size;
        else
            status = "Not installed";

        row.setContent (entry.menuLabel, status, active);
        row.setProgress (downloadingThis ? ortProgress : -1.0f);
        if (downloadingThis)
            row.setAction ("CANCEL", true, false);
        else if (active)
            row.setAction ({}, false, false);
        else if (installed)
            row.setAction ("ACTIVATE", true, false);
        else
            row.setAction ("DOWNLOAD", ! ortDownloading, false);
    }

    // ---- Models ----
    const bool modelDownloading = isAnyModelDownloading();
    const auto modelProgress = processor.getUiSliceSnapshot().stemDownloadProgress;

    for (size_t i = 0; i < catalog.size() && i < modelRows.size(); ++i)
    {
        const auto& entry = catalog[i];
        auto& row = *modelRows[i];
        const bool installed = isModelInstalled (entry.id);
        const bool update = hasModelUpdate (entry.id);
        const bool downloadingThis = isModelDownloading (entry.id);

        juce::String status;
        status << entry.getNumOutputs() << (entry.getNumOutputs() == 1 ? " stem" : " stems");
        if (downloadingThis)
            status << separatorDot() << "Downloading " << formatPercent (modelProgress);
        else if (update)
            status << separatorDot() << "Update available";
        else if (installed)
            status << separatorDot() << "Installed";
        else if (const auto size = formatBytes (entry.downloadBytes); size.isNotEmpty())
            status << separatorDot() << size;
        else
            status << separatorDot() << "Not installed";

        row.setContent (entry.menuLabel, status, update || (installed && ! downloadingThis));
        row.setProgress (downloadingThis ? modelProgress : -1.0f);
        if (downloadingThis)
            row.setAction ("CANCEL", true, false);
        else if (update)
            row.setAction ("UPDATE", ! modelDownloading, true);
        else if (! installed && entry.downloadUrl.isNotEmpty())
            row.setAction ("DOWNLOAD", ! modelDownloading, false);
        else
            row.setAction ({}, false, false);
    }

    // ---- Summary + bulk actions ----
    const auto pending = getMissingOrOutdatedModels();
    int installedCount = 0;
    for (const auto& entry : catalog)
        installedCount += isModelInstalled (entry.id) ? 1 : 0;

    auto summary = juce::String (installedCount) + " of " + juce::String ((int) catalog.size()) + " installed";
    if (! updateModels.empty())
        summary << separatorDot() << (int) updateModels.size() << (updateModels.size() == 1 ? " update" : " updates");

    const auto bulkText = pending.empty() ? juce::String ("ALL INSTALLED")
                        : updateModels.empty() ? juce::String ("DOWNLOAD MISSING")
                                               : juce::String ("DOWNLOAD MISSING AND UPDATES");
    const bool layoutChanged = summary != installedSummary
                            || bulkText != downloadMissingButton.getButtonText()
                            || modelDownloading != cancelButton.isVisible();

    installedSummary = summary;
    downloadMissingButton.setButtonText (bulkText);
    setButtonEnabled (downloadMissingButton, ! pending.empty() && ! modelDownloading);
    cancelButton.setVisible (modelDownloading);

    if (layoutChanged)
        relayout();
}

void StemSettingsPage::tick()
{
    const bool modelDownloading = isAnyModelDownloading();
    const bool ortDownloading = isOrtDownloading();
    bool reloadNeeded = false;

    // A finished download can add a model, a newer manifest or a runtime bundle.
    if (modelDownloading != wasModelDownloading)
    {
        wasModelDownloading = modelDownloading;
        reloadNeeded = reloadNeeded || ! modelDownloading;
    }
    if (ortDownloading != wasOrtDownloading)
    {
        wasOrtDownloading = ortDownloading;
        reloadNeeded = reloadNeeded || ! ortDownloading;
    }
    if (const auto generation = processor.getStemManifestGeneration();
        generation != seenManifestGeneration && ! modelDownloading)
    {
        seenManifestGeneration = generation;
        reloadNeeded = true;
    }

    if (reloadNeeded)
        reloadCatalog();
    else
        updateRows();   // cheap: rows repaint only when their text or progress changed
}

//==============================================================================
// Actions

void StemSettingsPage::chooseModelFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Select Stem Model Folder", processor.getResolvedStemModelFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<StemSettingsPage> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;

            const auto folder = fc.getResult();
            if (folder.isDirectory())
            {
                safe->processor.setStemModelFolder (folder);
                safe->host.persistSettings();
                safe->reload();
            }
        });
}

void StemSettingsPage::downloadMissing()
{
    const auto models = getMissingOrOutdatedModels();
    if (models.empty())
        processor.showTransientStatusMessage ("All stem models are already installed", false);
    else
        processor.startStemModelDownload (models);
    updateRows();
}

void StemSettingsPage::onBundleAction (size_t index)
{
    if (index >= bundles.size())
        return;

    const auto& entry = bundles[index];
    const bool installed = vectorContains (installedBundles, entry.id);

    if (isOrtDownloading() && processor.getOrtBundleDownloadJob().getCurrentBundleId() == entry.id)
        processor.cancelOrtBundleDownload();
    else if (installed)
        processor.setActiveOrtBundle (entry.id);   // the status line says a restart is needed
    else
        processor.startOrtBundleDownload (entry.id);

    reloadCatalog();
}

void StemSettingsPage::onModelAction (size_t index)
{
    if (index >= catalog.size())
        return;

    const auto& entry = catalog[index];
    if (isModelDownloading (entry.id))
        processor.cancelStemModelDownload();
    else if (! isAnyModelDownloading())
        processor.startStemModelDownload ({ entry.id });

    updateRows();
}

//==============================================================================

void StemSettingsPage::refreshThemeColours()
{
    for (auto* button : { &chooseButton, &defaultButton, &openButton, &downloadMissingButton, &cancelButton })
        styleButton (*button);
    for (auto* rows : { &bundleRows, &modelRows })
        for (auto& row : *rows)
            row->refreshThemeColours();
    repaint();
}

void StemSettingsPage::layout (RowLayout& rows)
{
    rows.section ("Stem separation");

    folderField.setBounds (placeButtonsRight (rows.row ("Model folder"), { &chooseButton, &defaultButton, &openButton })
                               .withTrimmedRight (4));
    rows.hint (folderField.isMissing() ? juce::String ("Folder not found")
               : processor.getStemModelFolder() != juce::File() ? juce::String ("Custom folder; DEFAULT goes back to the standard one")
                                                                : juce::String ("Where models are downloaded to and loaded from"),
               folderField.isMissing());

    deviceChips.setBounds (rows.row ("Compute").removeFromLeft (260));
    rows.hint (gpuProvider.isEmpty() && deviceChips.getSelectedIndex() == 1
                   ? juce::String ("No GPU runtime is active, so STEMS will use the CPU. Install one below")
                   : juce::String ("The device STEMS starts with; you can still switch it there"));

    if (! bundles.empty())
    {
        rows.section ("ONNX Runtime");
        for (auto& row : bundleRows)
        {
            row->setBounds (rows.fullRow (ItemRow::kHeight));
            rows.gap (2 - kRowGap);
        }
        rows.gap (kRowGap - 2);
        rows.hint ("A newly activated runtime is used after INTERSECT restarts");
    }

    rows.section ("Models");
    auto summaryRow = rows.fullRow();
    const auto textArea = placeButtonsRight (summaryRow, { &cancelButton, &downloadMissingButton });
    texts.push_back ({ installedSummary, textArea, TextStyle::value });
    rows.gap (6 - kRowGap);

    for (auto& row : modelRows)
    {
        row->setBounds (rows.fullRow (ItemRow::kHeight));
        rows.gap (2 - kRowGap);
    }
}
