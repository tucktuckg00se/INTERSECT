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

// Model ids are the manifest's `model_id` strings (e.g. "bs-roformer-sw-6stem"), so models
// published in the manifest work without a plugin update.
using StemModelId = juce::String;

enum class StemRuntimeKind
{
    waveform = 0,   // [1, 2, chunk] audio in, [1, stems, 2, chunk] audio out; STFT inside the model
    spectral,       // host STFT: [1, 4, bins, frames] spectrum in, [1, stems * 4, bins, frames] out
    spectralMask,   // host STFT: [1, frames, bins * 4] in, [1, stems, frames, maskBins, 2, 2] complex
                    // masks out, applied to a maskNFft STFT of the mix
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
    linuxArm64Cpu,   // appended so existing values keep their numbers
    winArm64Cpu,
};

struct OrtBundleCatalogEntry
{
    OrtBundleId id = OrtBundleId::linuxX64Cpu;
    juce::String menuLabel;         // e.g. "NVIDIA CUDA 12"
    juce::String directoryName;     // folder name under ort/ on disk
    juce::String providerName;      // "CUDA" / "DML" / "CoreML" / "MIGraphX" / "" for CPU-only
    juce::String platformTag;       // "linux-x64" / "linux-arm64" / "win-x64" / "win-arm64" / "macos-arm64"
    juce::String archiveFileName;   // tarball / zip name
    juce::String downloadUrl;       // populated by manifest at runtime (blank in built-in catalog)
    juce::int64  downloadBytes = 0;
    juce::String libraryFileName;   // primary ORT runtime library file name inside the bundle
};

struct StemJobResult
{
    std::vector<juce::File> stemFiles;
    std::vector<juce::String> stemNames;
    juce::String warningMessage;
    juce::String errorMessage;
};

struct StemMetadata
{
    static constexpr int kMaxNameBytes = 48;

    int parentSourceSampleId = -1;
    StemRole role = StemRole::unknown;
    bool isGenerated = false;
    // Fixed-size so the audio thread can copy metadata without allocating.
    std::array<char, kMaxNameBytes> name {};

    void setName (const juce::String& text)
    {
        name.fill (0);
        text.copyToUTF8 (name.data(), name.size());
    }

    juce::String getName() const { return juce::String::fromUTF8 (name.data()); }
};

struct StemOutputDesc
{
    juce::String name;    // file-name and metadata id, e.g. "lead-vocal"
    juce::String label;   // display text, e.g. "Lead Vocal"
    juce::String group;   // display grouping, e.g. "Vocals"; empty = ungrouped
};

struct StemSpectralParams
{
    int nFft = 4096;
    int hopSize = 1024;
    bool hannWindow = false;      // rectangular when false
    bool normalized = true;       // forward and inverse scaled by 1/sqrt(nFft)
    int frames = 476;             // STFT frames per segment (model input width)
    int segmentSamples = 485100;  // audio samples per segment
    int strideSamples = 121275;   // segment advance
    bool normalizeInput = true;   // run on the mix less its mean over its deviation, scale back after
    int maskNFft = 2048;          // spectralMask only: STFT size the output masks apply to
};

// How much a GPU helps a model, for the stem view's requirements line.
enum class StemGpuAdvice
{
    unknown,
    notNeeded,     // runs well on the CPU
    optional,      // faster on a GPU, fine without
    recommended,   // slow on the CPU
};

// What a model needs to run, as measured with INTERSECT's CPU settings on a fast desktop CPU.
// Zero means not known.
struct StemModelRequirements
{
    float ramGb = 0.0f;               // free memory needed while separating
    float cpuRealtimeFactor = 0.0f;   // seconds of CPU processing per second of audio
    StemGpuAdvice gpu = StemGpuAdvice::unknown;
};

struct StemModelCatalogEntry
{
    StemModelId id;
    juce::String menuLabel;
    juce::String fileName;
    juce::String downloadUrl;
    juce::int64 downloadBytes = 0;
    juce::String sha256;          // lowercase hex of the model file; empty = not verified
    juce::String license;
    juce::String attribution;
    juce::String notes;
    StemModelRequirements requirements;
    std::vector<StemOutputDesc> outputs;
    juce::StringArray groupOrder;  // display order of output groups; unlisted groups follow
    StemRuntimeKind runtimeKind = StemRuntimeKind::waveform;
    StemSpectralParams spectral;
    double sampleRate = 44100.0;
    int chunkSize = 131072;
    float overlapRatio = 0.5f;

    int getNumOutputs() const noexcept { return (int) outputs.size(); }
    bool isValid() const noexcept { return id.isNotEmpty() && fileName.isNotEmpty() && ! outputs.empty(); }
};

// Selected model output indices, ascending and unique.
using StemSelection = std::vector<int>;

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
juce::String stemOutputLabelFromName (const juce::String& name);
juce::String getDefaultStemModelId();
// Maps legacy ids (e.g. "bsroformer4stem_fp32") to their current model id; trims otherwise.
StemModelId normaliseStemModelId (const juce::String& text);
juce::String stemComputeDeviceToString (StemComputeDevice device);
StemComputeDevice stemComputeDeviceFromString (const juce::String& text);
juce::String getAvailableGpuProviderName();
juce::String stemExportModeToString (StemExportMode mode);
StemExportMode stemExportModeFromString (const juce::String& text);
const std::vector<StemModelCatalogEntry>& getBuiltInStemModelCatalog();
juce::String getStemModelManifestFileName();
juce::String getStemModelManifestDownloadUrl();
juce::File getDefaultStemModelFolder();
juce::File getStemModelManifestFile (const juce::File& modelFolder);

// Built-in catalog overlaid with the downloaded manifest in modelFolder. The manifest can
// override built-in entries and add models whose runtime kind this build supports.
std::vector<StemModelCatalogEntry> getEffectiveStemModelCatalog (const juce::File& modelFolder);
// Invalid (empty id) entry when the model is unknown.
StemModelCatalogEntry getEffectiveStemModelCatalogEntry (const StemModelId& modelId, const juce::File& modelFolder);
juce::File resolveStemModelFile (const juce::File& modelFolder, const StemModelId& modelId);
std::vector<StemModelId> scanInstalledStemModels (const juce::File& modelFolder);

// Sorted, unique, in-range copy of selection.
StemSelection normaliseStemSelection (StemSelection selection, int numOutputs);

// Parses one manifest `models[]` object. Returns an invalid entry when it can't be used.
StemModelCatalogEntry parseStemModelManifestEntry (const juce::var& manifestModel, const StemModelCatalogEntry& base);

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
