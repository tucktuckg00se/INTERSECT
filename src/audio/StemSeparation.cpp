#include "StemSeparation.h"

namespace
{
constexpr auto kStemModelManifestFileName = "intersect_stem_models_manifest.json";
constexpr auto kStemModelManifestDownloadUrl = "https://github.com/tucktuckg00se/intersect-stem-models/releases/download/v0.1.0/intersect_stem_models_manifest.json";

const std::array<StemModelCatalogEntry, 1> kStemModelCatalog =
{{
    { StemModelId::bsRoformerSw6stem,
      "BS-RoFormer SW 6-Stem",
      "bs_roformer_sw_6stem.onnx",
      "https://github.com/tucktuckg00se/intersect-stem-models/releases/download/v0.1.0/bs_roformer_sw_6stem.onnx",
      6,
      { StemRole::bass, StemRole::drums, StemRole::other, StemRole::vocals, StemRole::guitar, StemRole::piano },
      false,
      StemRole::unknown,
      44100.0, 131072, 0.5f },
}};

bool applyManifestEntryToCatalogEntry (const juce::var& manifestModel, StemModelCatalogEntry& entry)
{
    auto* object = manifestModel.getDynamicObject();
    if (object == nullptr)
        return false;

    // Match by model_id field
    StemModelId manifestId {};
    auto idField = object->getProperty ("model_id").toString();
    if (idField.isEmpty())
        idField = object->getProperty ("id").toString();
    if (! stemModelIdFromString (idField, manifestId) || manifestId != entry.id)
        return false;

    if (auto text = object->getProperty ("name").toString(); text.isNotEmpty())
        entry.menuLabel = text;

    // Artifacts block
    if (auto* artifacts = object->getProperty ("artifacts").getDynamicObject())
    {
        if (auto text = artifacts->getProperty ("release_asset_name").toString(); text.isNotEmpty())
            entry.fileName = text;
        if (auto text = artifacts->getProperty ("release_asset_url").toString(); text.isNotEmpty())
            entry.downloadUrl = text;
    }

    // Runtime block
    if (auto* runtime = object->getProperty ("runtime").getDynamicObject())
    {
        if (auto* stemsArray = runtime->getProperty ("stems").getArray())
        {
            entry.numModelOutputs = juce::jlimit (0, 6, (int) stemsArray->size());
            for (int i = 0; i < 6; ++i)
                entry.modelOutputRoles[i] = i < entry.numModelOutputs
                                                ? stemRoleFromString ((*stemsArray)[(size_t) i].toString())
                                                : StemRole::unknown;
        }

        if (runtime->hasProperty ("sample_rate_hz"))
            entry.sampleRate = (double) runtime->getProperty ("sample_rate_hz");
        if (runtime->hasProperty ("chunk_size_samples"))
            entry.chunkSize = (int) runtime->getProperty ("chunk_size_samples");
        if (runtime->hasProperty ("recommended_overlap_ratio"))
            entry.overlapRatio = (float) (double) runtime->getProperty ("recommended_overlap_ratio");
    }

    return true;
}
}

StemRole stemRoleFromString (const juce::String& text)
{
    const auto trimmed = text.trim();
    if (trimmed.equalsIgnoreCase ("drums")) return StemRole::drums;
    if (trimmed.equalsIgnoreCase ("bass")) return StemRole::bass;
    if (trimmed.equalsIgnoreCase ("other")) return StemRole::other;
    if (trimmed.equalsIgnoreCase ("vocals")) return StemRole::vocals;
    if (trimmed.equalsIgnoreCase ("guitar")) return StemRole::guitar;
    if (trimmed.equalsIgnoreCase ("piano")) return StemRole::piano;
    if (trimmed.equalsIgnoreCase ("instrumental")) return StemRole::instrumental;
    return StemRole::unknown;
}

juce::String stemModelIdToString (StemModelId modelId)
{
    switch (modelId)
    {
        case StemModelId::bsRoformerSw6stem: return "bs-roformer-sw-6stem";
    }

    return "unknown";
}

bool stemModelIdFromString (const juce::String& text, StemModelId& modelId)
{
    const auto trimmed = text.trim();
    if (trimmed.equalsIgnoreCase ("bs-roformer-sw-6stem"))
    {
        modelId = StemModelId::bsRoformerSw6stem;
        return true;
    }

    // Legacy fallback: old model IDs map to the single available model
    if (trimmed.equalsIgnoreCase ("bsroformer4stem_uint8")
        || trimmed.equalsIgnoreCase ("bsroformer4stem_fp32"))
    {
        modelId = StemModelId::bsRoformerSw6stem;
        return true;
    }

    return false;
}

juce::String stemModelMenuLabel (StemModelId modelId)
{
    if (const auto* entry = findStemModelCatalogEntry (modelId))
        return entry->menuLabel;

    return "Unknown model";
}

juce::String stemComputeDeviceToString (StemComputeDevice device)
{
    switch (device)
    {
        case StemComputeDevice::cpu: return "CPU";
        case StemComputeDevice::gpu: return "GPU";
    }

    return "CPU";
}

StemComputeDevice stemComputeDeviceFromString (const juce::String& text)
{
    if (text.trim().equalsIgnoreCase ("gpu"))
        return StemComputeDevice::gpu;

    return StemComputeDevice::cpu;
}

juce::String stemExportModeToString (StemExportMode mode)
{
    switch (mode)
    {
        case StemExportMode::combine:  return "Combine";
        case StemExportMode::separate: return "Separate";
    }

    return "Combine";
}

StemExportMode stemExportModeFromString (const juce::String& text)
{
    if (text.trim().equalsIgnoreCase ("separate"))
        return StemExportMode::separate;

    return StemExportMode::combine;
}

const StemModelCatalogEntry* findStemModelCatalogEntry (StemModelId modelId)
{
    for (const auto& entry : kStemModelCatalog)
        if (entry.id == modelId)
            return &entry;

    return nullptr;
}

const std::array<StemModelCatalogEntry, 1>& getStemModelCatalog()
{
    return kStemModelCatalog;
}

juce::String getStemModelManifestFileName()
{
    return kStemModelManifestFileName;
}

juce::String getStemModelManifestDownloadUrl()
{
    return kStemModelManifestDownloadUrl;
}

juce::File getDefaultStemModelFolder()
{
#if JUCE_LINUX
    auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    return home.getChildFile (".local")
               .getChildFile ("share")
               .getChildFile ("INTERSECT")
               .getChildFile ("models");
#else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("INTERSECT")
        .getChildFile ("models");
#endif
}

juce::File getStemModelManifestFile (const juce::File& modelFolder)
{
    return modelFolder.getChildFile (getStemModelManifestFileName());
}

juce::File resolveStemModelFile (const juce::File& modelFolder, StemModelId modelId)
{
    const auto entry = getEffectiveStemModelCatalogEntry (modelId, modelFolder);
    return modelFolder.getChildFile (entry.fileName);
}

StemModelCatalogEntry getEffectiveStemModelCatalogEntry (StemModelId modelId, const juce::File& modelFolder)
{
    if (const auto* builtInEntry = findStemModelCatalogEntry (modelId))
    {
        auto entry = *builtInEntry;
        const auto manifestFile = getStemModelManifestFile (modelFolder);
        if (! manifestFile.existsAsFile())
            return entry;

        juce::String manifestText = manifestFile.loadFileAsString();
        if (manifestText.isEmpty())
            return entry;

        juce::var parsed = juce::JSON::parse (manifestText);
        auto* rootObject = parsed.getDynamicObject();
        if (rootObject == nullptr)
            return entry;

        if (auto* modelsArray = rootObject->getProperty ("models").getArray())
            for (const auto& modelVar : *modelsArray)
                if (applyManifestEntryToCatalogEntry (modelVar, entry))
                    break;

        return entry;
    }

    return {};
}

std::vector<StemModelId> scanInstalledStemModels (const juce::File& modelFolder)
{
    std::vector<StemModelId> installed;
    installed.reserve (kStemModelCatalog.size());

    for (const auto& entry : kStemModelCatalog)
    {
        if (modelFolder.getChildFile (entry.fileName).existsAsFile())
            installed.push_back (entry.id);
    }

    return installed;
}

StemSelectionMask stemSelectionBitForIndex (int outputIndex)
{
    return outputIndex >= 0 && outputIndex < 32 ? (StemSelectionMask (1u) << (uint32_t) outputIndex)
                                                : StemSelectionMask (0);
}

bool isStemOutputSelected (StemSelectionMask mask, int outputIndex)
{
    return (mask & stemSelectionBitForIndex (outputIndex)) != 0;
}
