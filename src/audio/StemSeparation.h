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

enum class StemExportMode
{
    combine = 0,
    separate,
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

enum class OrtBundleId
{
    linuxX64Cpu = 0,
    linuxX64Cuda12,
    linuxX64Cuda13,
    linuxX64Migraphx,
    winX64DirectMl,
    winX64Cpu,
    macosArm64,
};

struct OrtBundleCatalogEntry
{
    OrtBundleId id = OrtBundleId::linuxX64Cpu;
    juce::String menuLabel;         // e.g. "NVIDIA CUDA 12"
    juce::String directoryName;     // folder name under ort/ on disk
    juce::String providerName;      // "CUDA" / "DML" / "CoreML" / "MIGraphX" / "" for CPU-only
    juce::String platformTag;       // "linux-x64" / "win-x64" / "macos-arm64"
    juce::String archiveFileName;   // tarball / zip name
    juce::String downloadUrl;       // populated by manifest at runtime (blank in built-in catalog)
    juce::int64  downloadBytes = 0;
    juce::String libraryFileName;   // primary ORT runtime library file name inside the bundle
};

struct StemJobResult
{
    std::vector<juce::File> stemFiles;
    std::vector<StemRole> stemRoles;
    juce::String warningMessage;
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
juce::String getAvailableGpuProviderName();
juce::String stemExportModeToString (StemExportMode mode);
StemExportMode stemExportModeFromString (const juce::String& text);
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

// ── ONNX Runtime bundle management ──────────────────────────────────────
//
// ORT runtimes are not shipped with the plugin. Users download a bundle
// (CPU-only or GPU-enabled) via the Stem Separation menu. Bundles live in
// <appdata>/INTERSECT/ort/<directoryName>/<version>/ and the "active"
// symlink/marker selects which one the plugin loads at runtime.

juce::File getDefaultOrtRootFolder();
juce::File getOrtActiveMarkerFile (const juce::File& ortRoot);
juce::File getOrtBundleInstallFolder (const juce::File& ortRoot,
                                      const OrtBundleCatalogEntry& entry,
                                      const juce::String& ortVersion);

const std::vector<OrtBundleCatalogEntry>& getOrtBundleCatalog();
const OrtBundleCatalogEntry* findOrtBundleCatalogEntry (OrtBundleId id);

// Bundles whose platformTag matches the current build.
std::vector<OrtBundleCatalogEntry> getOrtBundlesForCurrentPlatform();

// Inspect the on-disk bundle directory to see which bundles are installed
// (i.e. the bundle folder exists and the primary library file is present).
std::vector<OrtBundleId> scanInstalledOrtBundles (const juce::File& ortRoot);

// Read / write the active bundle marker. Returns an empty optional if no
// active bundle is set.
juce::String readActiveOrtBundleDirectoryName (const juce::File& ortRoot);
bool writeActiveOrtBundleDirectoryName (const juce::File& ortRoot, const juce::String& directoryName);

// Full path to the ORT shared library for the currently active bundle, or
// an invalid juce::File if no bundle is installed.
juce::File resolveActiveOrtLibraryFile (const juce::File& ortRoot);

juce::String getCurrentPlatformTag();

juce::String getOrtBundleManifestFileName();
juce::String getOrtBundleManifestDownloadUrl();
juce::File   getOrtBundleManifestFile (const juce::File& ortRoot);

// Resolves the effective catalog entry (built-in + overrides from the
// manifest on disk). Leaves downloadUrl / downloadBytes blank on miss.
OrtBundleCatalogEntry getEffectiveOrtBundleCatalogEntry (OrtBundleId id, const juce::File& ortRoot);
