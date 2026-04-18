#include "StemSeparation.h"

namespace
{
constexpr auto kStemModelManifestFileName = "intersect_stem_models_manifest.json";
constexpr auto kStemModelManifestDownloadUrl = "https://github.com/tucktuckg00se/intersect-stem-models/releases/download/v0.1.0/intersect_stem_models_manifest.json";

constexpr auto kOrtBundleManifestFileName    = "intersect_ort_bundles_manifest.json";
constexpr auto kOrtBundleManifestDownloadUrl = "https://github.com/tucktuckg00se/intersect-ort-providers/releases/latest/download/intersect_ort_bundles_manifest.json";

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

juce::String getAvailableGpuProviderName()
{
    const auto root = getDefaultOrtRootFolder();
    const auto dirName = readActiveOrtBundleDirectoryName (root);
    if (dirName.isEmpty())
        return {};

    for (const auto& entry : getOrtBundleCatalog())
        if (entry.directoryName == dirName)
            return entry.providerName;

    return {};
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

// ── ORT bundle catalog ──────────────────────────────────────────────────

juce::String getCurrentPlatformTag()
{
   #if JUCE_WINDOWS
    return "win-x64";
   #elif JUCE_MAC
    #if JUCE_ARM
     return "macos-arm64";
    #else
     return "macos-x64";
    #endif
   #elif JUCE_LINUX
    return "linux-x64";
   #else
    return {};
   #endif
}

static const std::vector<OrtBundleCatalogEntry>& buildOrtBundleCatalog()
{
    // libonnxruntime file names inside each bundle. These are the file names
    // produced by the official ORT 1.24.x packages (Linux ships a versioned
    // SONAME; macOS ships an unversioned symlink; Windows is just .dll).
    // Archive files are .zip across platforms so the plugin can use juce::ZipFile
    // for extraction (no reliance on system tar). The intersect-ort-providers
    // builder repo repackages upstream tarballs into these zips.
    static const std::vector<OrtBundleCatalogEntry> catalog =
    {
        { OrtBundleId::winX64DirectMl,     "DirectML (GPU)",     "win-x64-directml",     "DML",      "win-x64",     "onnxruntime-win-x64-directml.zip",    "", 0, "onnxruntime.dll" },
        { OrtBundleId::winX64Cpu,          "CPU only",           "win-x64-cpu",          "",         "win-x64",     "onnxruntime-win-x64-cpu.zip",         "", 0, "onnxruntime.dll" },
        { OrtBundleId::linuxX64Cpu,        "CPU only",           "linux-x64-cpu",        "",         "linux-x64",   "onnxruntime-linux-x64-cpu.zip",       "", 0, "libonnxruntime.so" },
        { OrtBundleId::linuxX64Cuda12,     "NVIDIA CUDA 12",     "linux-x64-cuda12",     "CUDA",     "linux-x64",   "onnxruntime-linux-x64-cuda12.zip",    "", 0, "libonnxruntime.so" },
        { OrtBundleId::linuxX64Cuda13,     "NVIDIA CUDA 13",     "linux-x64-cuda13",     "CUDA",     "linux-x64",   "onnxruntime-linux-x64-cuda13.zip",    "", 0, "libonnxruntime.so" },
        { OrtBundleId::linuxX64Migraphx,   "AMD MIGraphX",       "linux-x64-migraphx",   "MIGraphX", "linux-x64",   "onnxruntime-linux-x64-migraphx.zip",  "", 0, "libonnxruntime.so" },
        { OrtBundleId::macosArm64,         "CoreML",             "macos-arm64",          "CoreML",   "macos-arm64", "onnxruntime-macos-arm64.zip",         "", 0, "libonnxruntime.dylib" },
    };
    return catalog;
}

const std::vector<OrtBundleCatalogEntry>& getOrtBundleCatalog()
{
    return buildOrtBundleCatalog();
}

const OrtBundleCatalogEntry* findOrtBundleCatalogEntry (OrtBundleId id)
{
    for (const auto& entry : getOrtBundleCatalog())
        if (entry.id == id)
            return &entry;

    return nullptr;
}

std::vector<OrtBundleCatalogEntry> getOrtBundlesForCurrentPlatform()
{
    const auto tag = getCurrentPlatformTag();
    std::vector<OrtBundleCatalogEntry> out;
    if (tag.isEmpty())
        return out;

    for (const auto& entry : getOrtBundleCatalog())
        if (entry.platformTag == tag)
            out.push_back (entry);

    return out;
}

juce::File getDefaultOrtRootFolder()
{
   #if JUCE_LINUX
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile (".local").getChildFile ("share")
        .getChildFile ("INTERSECT").getChildFile ("ort");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("INTERSECT").getChildFile ("ort");
   #endif
}

juce::File getOrtActiveMarkerFile (const juce::File& ortRoot)
{
    return ortRoot.getChildFile ("active");
}

juce::File getOrtBundleInstallFolder (const juce::File& ortRoot,
                                      const OrtBundleCatalogEntry& entry,
                                      const juce::String& ortVersion)
{
    return ortRoot.getChildFile (entry.directoryName).getChildFile (ortVersion);
}

static juce::File findBundleLibraryFile (const juce::File& bundleFolder, const juce::String& baseLibName)
{
    // Support both the plain file name and versioned variants (e.g. libonnxruntime.so.1.24.2).
    if (bundleFolder.getChildFile (baseLibName).existsAsFile())
        return bundleFolder.getChildFile (baseLibName);

    auto libDir = bundleFolder.getChildFile ("lib");
    if (libDir.isDirectory())
    {
        if (libDir.getChildFile (baseLibName).existsAsFile())
            return libDir.getChildFile (baseLibName);

        // Look for versioned variants (libonnxruntime.so.1, libonnxruntime.so.1.24.2, ...).
        juce::Array<juce::File> matches;
        libDir.findChildFiles (matches, juce::File::findFiles, false, baseLibName + "*");
        for (const auto& f : matches)
            if (f.existsAsFile())
                return f;
    }

    // Windows: the dll may live at bundle root or under bin/.
    auto binDir = bundleFolder.getChildFile ("bin");
    if (binDir.isDirectory() && binDir.getChildFile (baseLibName).existsAsFile())
        return binDir.getChildFile (baseLibName);

    return {};
}

std::vector<OrtBundleId> scanInstalledOrtBundles (const juce::File& ortRoot)
{
    std::vector<OrtBundleId> installed;
    if (! ortRoot.isDirectory())
        return installed;

    for (const auto& entry : getOrtBundleCatalog())
    {
        const auto bundleDir = ortRoot.getChildFile (entry.directoryName);
        if (! bundleDir.isDirectory())
            continue;

        // Any versioned subfolder that contains the library counts as installed.
        juce::Array<juce::File> versionDirs;
        bundleDir.findChildFiles (versionDirs, juce::File::findDirectories, false);
        for (const auto& versionDir : versionDirs)
        {
            if (findBundleLibraryFile (versionDir, entry.libraryFileName).existsAsFile())
            {
                installed.push_back (entry.id);
                break;
            }
        }
    }

    return installed;
}

juce::String readActiveOrtBundleDirectoryName (const juce::File& ortRoot)
{
    const auto marker = getOrtActiveMarkerFile (ortRoot);
    if (! marker.exists())
        return {};

    // The marker may be a file containing the directory name or a symlink to
    // the versioned bundle folder. Handle both.
    if (marker.existsAsFile())
    {
        auto text = marker.loadFileAsString().trim();
        // If it contains a path separator, take the first path component.
        text = text.replaceCharacter ('\\', '/');
        const auto firstSeg = text.upToFirstOccurrenceOf ("/", false, false);
        return firstSeg.isEmpty() ? text : firstSeg;
    }

    if (marker.isDirectory())
    {
        // Symlink dereferenced by isDirectory(). Use the link target if we can
        // read it, otherwise fall back to reading the link file name.
        auto linked = marker.getLinkedTarget();
        if (linked.exists())
        {
            // linked path is like .../ort/<dirname>/<version>
            auto parent = linked.getParentDirectory();
            return parent.getFileName();
        }
    }

    return {};
}

bool writeActiveOrtBundleDirectoryName (const juce::File& ortRoot, const juce::String& directoryName)
{
    if (directoryName.isEmpty())
        return false;

    if (! ortRoot.createDirectory())
        return false;

    const auto marker = getOrtActiveMarkerFile (ortRoot);
    if (marker.exists())
        marker.deleteRecursively();

    return marker.replaceWithText (directoryName);
}

juce::String getOrtBundleManifestFileName()
{
    return kOrtBundleManifestFileName;
}

juce::String getOrtBundleManifestDownloadUrl()
{
    return kOrtBundleManifestDownloadUrl;
}

juce::File getOrtBundleManifestFile (const juce::File& ortRoot)
{
    return ortRoot.getChildFile (getOrtBundleManifestFileName());
}

static bool applyOrtManifestEntryToCatalogEntry (const juce::var& manifestBundle, OrtBundleCatalogEntry& entry)
{
    auto* object = manifestBundle.getDynamicObject();
    if (object == nullptr)
        return false;

    const auto idField = object->getProperty ("bundle_id").toString().trim();
    if (idField.isEmpty() || idField != entry.directoryName)
        return false;

    if (auto text = object->getProperty ("download_url").toString(); text.isNotEmpty())
        entry.downloadUrl = text;
    if (object->hasProperty ("download_bytes"))
        entry.downloadBytes = (juce::int64) (double) object->getProperty ("download_bytes");
    if (auto text = object->getProperty ("library_file").toString(); text.isNotEmpty())
        entry.libraryFileName = text;

    return true;
}

OrtBundleCatalogEntry getEffectiveOrtBundleCatalogEntry (OrtBundleId id, const juce::File& ortRoot)
{
    OrtBundleCatalogEntry entry {};
    if (const auto* builtIn = findOrtBundleCatalogEntry (id))
        entry = *builtIn;
    else
        return entry;

    const auto manifestFile = getOrtBundleManifestFile (ortRoot);
    if (! manifestFile.existsAsFile())
        return entry;

    juce::var parsed = juce::JSON::parse (manifestFile.loadFileAsString());
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return entry;

    if (auto* bundlesArray = root->getProperty ("bundles").getArray())
        for (const auto& bundleVar : *bundlesArray)
            if (applyOrtManifestEntryToCatalogEntry (bundleVar, entry))
                break;

    return entry;
}

juce::File resolveActiveOrtLibraryFile (const juce::File& ortRoot)
{
    const auto dirName = readActiveOrtBundleDirectoryName (ortRoot);
    if (dirName.isEmpty())
        return {};

    const auto bundleDir = ortRoot.getChildFile (dirName);
    if (! bundleDir.isDirectory())
        return {};

    const OrtBundleCatalogEntry* entry = nullptr;
    for (const auto& e : getOrtBundleCatalog())
        if (e.directoryName == dirName)
        {
            entry = &e;
            break;
        }

    if (entry == nullptr)
        return {};

    // Pick the most-recent version folder inside the bundle directory.
    juce::Array<juce::File> versionDirs;
    bundleDir.findChildFiles (versionDirs, juce::File::findDirectories, false);

    juce::File bestMatch;
    juce::Time bestTime;
    for (const auto& versionDir : versionDirs)
    {
        const auto candidate = findBundleLibraryFile (versionDir, entry->libraryFileName);
        if (! candidate.existsAsFile())
            continue;

        const auto mtime = versionDir.getLastModificationTime();
        if (bestMatch == juce::File() || mtime > bestTime)
        {
            bestMatch = candidate;
            bestTime = mtime;
        }
    }

    return bestMatch;
}
