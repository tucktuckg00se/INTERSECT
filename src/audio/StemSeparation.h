#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

enum class StemRole
{
    drums = 0,
    bass,
    other,
    vocals,
    guitar,
    piano,
    unknown,
    instrumental
};

enum class StemModelId
{
    bsRoformerSw6stem = 0,
};

enum class StemComputeDevice
{
    cpu = 0,
    gpu,
};

enum class StemJobState
{
    idle = 0,
    preparing,
    separating,
    writing,
    importing,
    cancelled,
    failed,
    completed
};

enum class StemModelDownloadState
{
    idle = 0,
    downloading,
    completed,
    cancelled,
    failed,
};

struct StemJobResult
{
    std::vector<juce::File> stemFiles;
    std::vector<StemRole> stemRoles;
    juce::String errorMessage;
};

struct StemMetadata
{
    int parentSourceSampleId = -1;
    StemRole role = StemRole::unknown;
    bool isGenerated = false;
};

struct StemModelCatalogEntry
{
    StemModelId id = StemModelId::bsRoformerSw6stem;
    juce::String menuLabel;
    juce::String fileName;
    juce::String downloadUrl;
    int numModelOutputs = 1;
    StemRole modelOutputRoles[6] = { StemRole::unknown, StemRole::unknown, StemRole::unknown,
                                     StemRole::unknown, StemRole::unknown, StemRole::unknown };
    bool computeResidual = false;
    StemRole residualRole = StemRole::unknown;
    double sampleRate = 44100.0;
    int chunkSize = 131072;
    float overlapRatio = 0.5f;
};

using StemSelectionMask = uint32_t;

inline juce::String stemRoleToString (StemRole role)
{
    switch (role)
    {
        case StemRole::drums:        return "drums";
        case StemRole::bass:         return "bass";
        case StemRole::other:        return "other";
        case StemRole::vocals:       return "vocals";
        case StemRole::guitar:       return "guitar";
        case StemRole::piano:        return "piano";
        case StemRole::unknown:      return "unknown";
        case StemRole::instrumental: return "instrumental";
    }
    return "unknown";
}

StemRole stemRoleFromString (const juce::String& text);
juce::String stemModelIdToString (StemModelId modelId);
bool stemModelIdFromString (const juce::String& text, StemModelId& modelId);
juce::String stemModelMenuLabel (StemModelId modelId);
juce::String stemComputeDeviceToString (StemComputeDevice device);
StemComputeDevice stemComputeDeviceFromString (const juce::String& text);
const StemModelCatalogEntry* findStemModelCatalogEntry (StemModelId modelId);
const std::array<StemModelCatalogEntry, 1>& getStemModelCatalog();
juce::String getStemModelManifestFileName();
juce::String getStemModelManifestDownloadUrl();
juce::File getDefaultStemModelFolder();
juce::File getStemModelManifestFile (const juce::File& modelFolder);
juce::File resolveStemModelFile (const juce::File& modelFolder, StemModelId modelId);
StemModelCatalogEntry getEffectiveStemModelCatalogEntry (StemModelId modelId, const juce::File& modelFolder);
std::vector<StemModelId> scanInstalledStemModels (const juce::File& modelFolder);
StemSelectionMask stemSelectionBitForIndex (int outputIndex);
bool isStemOutputSelected (StemSelectionMask mask, int outputIndex);
