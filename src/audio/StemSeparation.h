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
    unknown,
    instrumental
};

enum class StemModelId
{
    bsRoformer2stem = 0,
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
    StemModelId id = StemModelId::bsRoformer2stem;
    const char* menuLabel = "";
    const char* fileName = "";
    const char* downloadUrl = "";
    int numModelOutputs = 1;
    StemRole modelOutputRoles[4] = { StemRole::unknown, StemRole::unknown, StemRole::unknown, StemRole::unknown };
    bool computeResidual = false;
    StemRole residualRole = StemRole::unknown;
    double sampleRate = 44100.0;
    int chunkSize = 352800;
    int nFft = 2048;
    int hopLength = 441;
    int dimF = 1024;
    int numOverlap = 2;
};

inline juce::String stemRoleToString (StemRole role)
{
    switch (role)
    {
        case StemRole::drums:        return "drums";
        case StemRole::bass:         return "bass";
        case StemRole::other:        return "other";
        case StemRole::vocals:       return "vocals";
        case StemRole::unknown:      return "unknown";
        case StemRole::instrumental: return "instrumental";
    }
    return "unknown";
}

juce::String stemModelIdToString (StemModelId modelId);
juce::String stemModelMenuLabel (StemModelId modelId);
juce::String stemComputeDeviceToString (StemComputeDevice device);
StemComputeDevice stemComputeDeviceFromString (const juce::String& text);
const StemModelCatalogEntry* findStemModelCatalogEntry (StemModelId modelId);
const std::array<StemModelCatalogEntry, 1>& getStemModelCatalog();
juce::File getDefaultStemModelFolder();
juce::File resolveStemModelFile (const juce::File& modelFolder, StemModelId modelId);
std::vector<StemModelId> scanInstalledStemModels (const juce::File& modelFolder);
