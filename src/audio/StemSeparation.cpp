#include "StemSeparation.h"
#include <algorithm>

namespace
{
constexpr auto kStemModelManifestFileName = "intersect_stem_models_manifest.json";
constexpr auto kStemModelManifestDownloadUrl = "https://github.com/tucktuckg00se/intersect-stem-models/releases/download/v0.2.0/intersect_stem_models_manifest.json";

constexpr auto kOrtBundleManifestFileName    = "intersect_ort_bundles_manifest.json";
constexpr auto kOrtBundleManifestDownloadUrl = "https://github.com/tucktuckg00se/intersect-ort-providers/releases/latest/download/intersect_ort_bundles_manifest.json";

constexpr auto kDefaultStemModelId = "bs-roformer-sw-6stem";

std::vector<StemOutputDesc> makeOutputs (std::initializer_list<const char*> names, const juce::String& group = {})
{
    std::vector<StemOutputDesc> outputs;
    for (const auto* name : names)
        outputs.push_back ({ name, stemOutputLabelFromName (name), group });
    return outputs;
}

std::vector<StemModelCatalogEntry> makeBuiltInCatalog()
{
    std::vector<StemModelCatalogEntry> catalog;

    StemModelCatalogEntry sw;
    sw.id = kDefaultStemModelId;
    sw.menuLabel = "BS-RoFormer SW 6-Stem";
    sw.fileName = "bs_roformer_sw_6stem.onnx";
    sw.downloadUrl = "https://github.com/tucktuckg00se/intersect-stem-models/releases/download/v0.1.0/bs_roformer_sw_6stem.onnx";
    sw.downloadBytes = 739269627;
    sw.sha256 = "718c3304a4a051bc8b1adfa776ce0aeab54165e207d0a5f5890d683c16188e5e";
    sw.license = "MIT";
    sw.attribution = "jarredou";
    sw.notes = "Full-size BS-RoFormer, with separate guitar and piano stems.";
    sw.requirements = { 6.0f, 3.5f, StemGpuAdvice::recommended };
    sw.outputs = makeOutputs ({ "bass", "drums", "other", "vocals", "guitar", "piano" });
    sw.runtimeKind = StemRuntimeKind::waveform;
    sw.sampleRate = 44100.0;
    sw.chunkSize = 131072;
    sw.overlapRatio = 0.5f;
    catalog.push_back (std::move (sw));

    // SCNet (starrytong, MIT; ONNX export by audiojs, pinned revision). The network without its
    // STFT: the host computes it (StemStft) with SCNet's own settings.
    StemModelCatalogEntry scnet;
    scnet.id = "scnet";
    scnet.menuLabel = "SCNet";
    scnet.fileName = "scnet.int8.onnx";
    scnet.downloadUrl = "https://huggingface.co/audiojs/scnet/resolve/e71bcdb42bce1d62b7fae25ba90fcbcf90f73f46/scnet.int8.onnx";
    scnet.downloadBytes = 12900277;
    scnet.sha256 = "98228931494151762a1c4ab1ec7899a894b1f81fd4a509921a9d2f9bacc50845";
    scnet.license = "MIT";
    scnet.attribution = "starrytong, ONNX by audiojs";
    scnet.notes = "The smallest and fastest model here (8-bit weights).";
    scnet.requirements = { 3.0f, 0.6f, StemGpuAdvice::notNeeded };
    scnet.outputs = makeOutputs ({ "drums", "bass", "other", "vocals" });
    scnet.runtimeKind = StemRuntimeKind::spectral;
    scnet.spectral = {};   // defaults are SCNet's: 4096 / 1024, rectangular, normalized, 476 frames, 11 s every 2.75 s
    scnet.sampleRate = 44100.0;
    catalog.push_back (std::move (scnet));

    // Mini-BS-RoFormer V2.5 (HiDolen, MIT), the author's own ONNX export at a pinned revision. It
    // estimates complex masks from a 4096-point STFT; the host applies them to a 2048-point STFT.
    StemModelCatalogEntry mini;
    mini.id = "mini-bs-roformer-v2.5";
    mini.menuLabel = "Mini-BS-RoFormer V2.5";
    mini.fileName = "mini_bs_roformer_v25.onnx";
    mini.downloadUrl = "https://huggingface.co/HiDolen/Mini-BS-RoFormer-V2.5-46.2M/resolve/38dafc78834607aec57d8c0cb3568523f9ea5563/onnx/freq_weights.onnx";
    mini.downloadBytes = 96538997;
    mini.sha256 = "c019650a7fb9e6a208353fead62471ed6ffdf20a3069ddadd8278edeb411d556";
    mini.license = "MIT";
    mini.attribution = "HiDolen";
    mini.notes = "Compact BS-RoFormer: much faster than full-size models, slightly lower quality.";
    mini.requirements = { 5.0f, 0.8f, StemGpuAdvice::optional };
    mini.outputs = makeOutputs ({ "bass", "drums", "other", "vocals" });
    mini.runtimeKind = StemRuntimeKind::spectralMask;
    mini.spectral.nFft = 4096;
    mini.spectral.hopSize = 512;
    mini.spectral.hannWindow = true;
    mini.spectral.normalized = false;
    mini.spectral.frames = 690;
    mini.spectral.segmentSamples = 352800;   // 8 s
    mini.spectral.strideSamples = 176400;
    mini.spectral.normalizeInput = false;
    mini.spectral.maskNFft = 2048;
    mini.sampleRate = 44100.0;
    catalog.push_back (std::move (mini));

    // MVSep Mega 53 (ZFTurbo / MVSep, MIT per MSST issue #245), exported by intersect-stem-models
    // v0.2.0. Stems are in the model's (alphabetical) output order; display groups are ours.
    StemModelCatalogEntry mega;
    mega.id = "mvsep-mega-53";
    mega.menuLabel = "MVSep Mega 53";
    mega.fileName = "mvsep_mega_53.onnx";
    mega.downloadUrl = "https://github.com/tucktuckg00se/intersect-stem-models/releases/download/v0.2.0/mvsep_mega_53.onnx";
    mega.downloadBytes = 1415950413;
    mega.sha256 = "d3b1c838303a44cffdb7358c536ec99ebf8ded1f1c34075144c413839b40101a";
    mega.license = "MIT";
    mega.attribution = "ZFTurbo / MVSep";
    mega.notes = "53 instruments. Stems overlap and don't add up to the mix. Slow to load.";
    mega.requirements = { 9.0f, 5.0f, StemGpuAdvice::recommended };
    // { name, group, label override }
    struct MegaStem { const char* name = nullptr; const char* group = nullptr; const char* label = nullptr; };
    static constexpr MegaStem megaStems[] = {
        { "accordion", "Keys" },
        { "acoustic-guitar", "Guitars & Plucked" },
        { "back-vocal", "Vocals" },
        { "banjo", "Guitars & Plucked" },
        { "bass", "Bass" },
        { "bassoon", "Woodwinds" },
        { "bells", "Percussion" },
        { "bowed_strings", "Strings" },
        { "brass", "Brass" },
        { "cello", "Strings" },
        { "clarinet", "Woodwinds" },
        { "congas", "Percussion" },
        { "digital-piano", "Keys" },
        { "dobro", "Guitars & Plucked" },
        { "double-bass", "Bass" },
        { "drums", "Drums" },
        { "electric-guitar", "Guitars & Plucked" },
        { "flute", "Woodwinds" },
        { "french-horn", "Brass" },
        { "glockenspiel", "Percussion" },
        { "guitar", "Guitars & Plucked" },
        { "harmonica", "Woodwinds" },
        { "harp", "Guitars & Plucked" },
        { "harpsichord", "Keys" },
        { "hh", "Drums", "Hi-Hat" },
        { "keys", "Keys" },
        { "kick", "Drums" },
        { "lead-vocal", "Vocals" },
        { "mandolin", "Guitars & Plucked" },
        { "marimba", "Percussion" },
        { "oboe", "Woodwinds" },
        { "organ", "Keys" },
        { "percussion", "Percussion" },
        { "piano", "Keys" },
        { "saxophone", "Woodwinds" },
        { "sitar", "Guitars & Plucked" },
        { "snare", "Drums" },
        { "strings", "Strings" },
        { "synth", "Synth" },
        { "tambourine", "Percussion" },
        { "timpani", "Percussion" },
        { "toms", "Drums" },
        { "triangle", "Percussion" },
        { "trombone", "Brass" },
        { "trumpet", "Brass" },
        { "tuba", "Brass" },
        { "ukulele", "Guitars & Plucked" },
        { "viola", "Strings" },
        { "violin", "Strings" },
        { "vocal", "Vocals" },
        { "wind", "Woodwinds" },
        { "wind-chimes", "Percussion" },
        { "woodwind", "Woodwinds" },
    };
    for (const auto& stem : megaStems)
        mega.outputs.push_back ({ stem.name, stem.label != nullptr ? juce::String (stem.label) : stemOutputLabelFromName (stem.name), stem.group });
    mega.groupOrder = { "Vocals", "Drums", "Percussion", "Bass", "Guitars & Plucked", "Keys", "Synth", "Strings", "Brass", "Woodwinds" };
    mega.runtimeKind = StemRuntimeKind::waveform;
    mega.sampleRate = 44100.0;
    mega.chunkSize = 131072;
    mega.overlapRatio = 0.5f;
    catalog.push_back (std::move (mega));

    return catalog;
}

juce::String readManifestModelId (const juce::var& manifestModel)
{
    auto* object = manifestModel.getDynamicObject();
    if (object == nullptr)
        return {};

    auto idField = object->getProperty ("model_id").toString();
    if (idField.isEmpty())
        idField = object->getProperty ("id").toString();
    return normaliseStemModelId (idField);
}

bool parseStemOutputs (const juce::var& stemsVar, std::vector<StemOutputDesc>& outputs)
{
    auto* stemsArray = stemsVar.getArray();
    if (stemsArray == nullptr || stemsArray->isEmpty())
        return false;

    std::vector<StemOutputDesc> parsed;
    parsed.reserve ((size_t) stemsArray->size());
    for (const auto& stemVar : *stemsArray)
    {
        StemOutputDesc desc;
        if (auto* stemObject = stemVar.getDynamicObject())
        {
            desc.name = stemObject->getProperty ("name").toString().trim();
            desc.label = stemObject->getProperty ("label").toString().trim();
            desc.group = stemObject->getProperty ("group").toString().trim();
        }
        else
        {
            desc.name = stemVar.toString().trim();
        }

        if (desc.name.isEmpty())
            return false;
        if (desc.label.isEmpty())
            desc.label = stemOutputLabelFromName (desc.name);
        parsed.push_back (std::move (desc));
    }

    outputs = std::move (parsed);
    return true;
}

bool parseRuntimeKind (const juce::String& text, StemRuntimeKind& kind)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || trimmed.equalsIgnoreCase ("waveform"))
    {
        kind = StemRuntimeKind::waveform;
        return true;
    }
    if (trimmed.equalsIgnoreCase ("spectral"))
    {
        kind = StemRuntimeKind::spectral;
        return true;
    }
    if (trimmed.equalsIgnoreCase ("spectral_mask"))
    {
        kind = StemRuntimeKind::spectralMask;
        return true;
    }
    return false;
}

void parseSpectralParams (const juce::DynamicObject& stft, StemSpectralParams& params)
{
    auto readInt = [&stft] (const char* key, int& value)
    {
        if (stft.hasProperty (key))
            value = (int) stft.getProperty (key);
    };

    readInt ("n_fft", params.nFft);
    readInt ("hop_length", params.hopSize);
    readInt ("frames", params.frames);
    readInt ("segment_samples", params.segmentSamples);
    readInt ("stride_samples", params.strideSamples);
    readInt ("mask_n_fft", params.maskNFft);
    if (stft.hasProperty ("normalize_input"))
        params.normalizeInput = (bool) stft.getProperty ("normalize_input");
    if (stft.hasProperty ("window"))
        params.hannWindow = stft.getProperty ("window").toString().equalsIgnoreCase ("hann");
    if (stft.hasProperty ("normalized"))
        params.normalized = (bool) stft.getProperty ("normalized");
}

bool isPowerOfTwoAtLeast16 (int n)
{
    return n >= 16 && (n & (n - 1)) == 0;
}

bool areSpectralParamsUsable (const StemSpectralParams& p, StemRuntimeKind kind)
{
    if (! isPowerOfTwoAtLeast16 (p.nFft) || p.hopSize <= 0 || p.hopSize > p.nFft
        || p.frames <= 1 || p.segmentSamples <= 0 || p.strideSamples <= 0
        || p.strideSamples > p.segmentSamples)
        return false;

    // The STFT runs over the segment, zero-padded up to the frame count; both must agree.
    const int signal = juce::jmax (p.segmentSamples, (p.frames - 1) * p.hopSize);
    if (1 + signal / p.hopSize != p.frames || signal <= p.nFft / 2)
        return false;

    return kind != StemRuntimeKind::spectralMask || isPowerOfTwoAtLeast16 (p.maskNFft);
}

juce::var loadManifestModels (const juce::File& modelFolder)
{
    const auto manifestFile = getStemModelManifestFile (modelFolder);
    if (! manifestFile.existsAsFile())
        return {};

    const auto parsed = juce::JSON::parse (manifestFile.loadFileAsString());
    if (auto* rootObject = parsed.getDynamicObject())
        return rootObject->getProperty ("models");
    return {};
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

juce::String stemOutputLabelFromName (const juce::String& name)
{
    // "lead-vocal" / "bowed_strings" -> "Lead Vocal" / "Bowed Strings"
    auto words = juce::StringArray::fromTokens (name.trim(), "-_ ", {});
    words.removeEmptyStrings();
    for (auto& word : words)
        word = word.substring (0, 1).toUpperCase() + word.substring (1);
    return words.joinIntoString (" ");
}

juce::String getDefaultStemModelId()
{
    return kDefaultStemModelId;
}

StemModelId normaliseStemModelId (const juce::String& text)
{
    const auto trimmed = text.trim().toLowerCase();

    // Legacy ids from the 4-stem builds map to the model that replaced them.
    if (trimmed == "bsroformer4stem_uint8" || trimmed == "bsroformer4stem_fp32")
        return kDefaultStemModelId;

    return trimmed;
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

const std::vector<StemModelCatalogEntry>& getBuiltInStemModelCatalog()
{
    static const auto catalog = makeBuiltInCatalog();
    return catalog;
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

StemModelCatalogEntry parseStemModelManifestEntry (const juce::var& manifestModel, const StemModelCatalogEntry& base)
{
    auto* object = manifestModel.getDynamicObject();
    if (object == nullptr)
        return {};

    auto entry = base;
    entry.id = readManifestModelId (manifestModel);
    if (entry.id.isEmpty())
        return {};

    if (auto text = object->getProperty ("name").toString(); text.isNotEmpty())
        entry.menuLabel = text;
    if (auto text = object->getProperty ("license").toString(); text.isNotEmpty())
        entry.license = text;
    if (auto text = object->getProperty ("attribution").toString(); text.isNotEmpty())
        entry.attribution = text;
    if (auto text = object->getProperty ("notes").toString(); text.isNotEmpty())
        entry.notes = text;

    // "requirements": { "ram_gb": 5, "cpu_realtime_factor": 3.5, "gpu": "recommended" }; each optional.
    if (auto* req = object->getProperty ("requirements").getDynamicObject())
    {
        if (const auto ram = (float) (double) req->getProperty ("ram_gb"); ram > 0.0f)
            entry.requirements.ramGb = ram;
        if (const auto factor = (float) (double) req->getProperty ("cpu_realtime_factor"); factor > 0.0f)
            entry.requirements.cpuRealtimeFactor = factor;
        const auto gpu = req->getProperty ("gpu").toString().trim();
        if (gpu.equalsIgnoreCase ("not_needed"))
            entry.requirements.gpu = StemGpuAdvice::notNeeded;
        else if (gpu.equalsIgnoreCase ("optional"))
            entry.requirements.gpu = StemGpuAdvice::optional;
        else if (gpu.equalsIgnoreCase ("recommended"))
            entry.requirements.gpu = StemGpuAdvice::recommended;
    }

    if (auto* artifacts = object->getProperty ("artifacts").getDynamicObject())
    {
        if (auto text = artifacts->getProperty ("release_asset_name").toString(); text.isNotEmpty())
            entry.fileName = text;
        if (auto text = artifacts->getProperty ("release_asset_url").toString(); text.isNotEmpty())
            entry.downloadUrl = text;
        if (artifacts->hasProperty ("release_asset_bytes"))
            entry.downloadBytes = (juce::int64) artifacts->getProperty ("release_asset_bytes");
        if (auto* sha = artifacts->getProperty ("sha256").getDynamicObject())
            if (auto text = sha->getProperty ("onnx").toString().trim().toLowerCase(); text.length() == 64)
                entry.sha256 = text;
    }

    if (auto* runtime = object->getProperty ("runtime").getDynamicObject())
    {
        // Unknown runtime kinds come from manifests newer than this build: skip the model.
        if (! parseRuntimeKind (runtime->getProperty ("kind").toString(), entry.runtimeKind))
            return {};

        if (runtime->hasProperty ("stems") && ! parseStemOutputs (runtime->getProperty ("stems"), entry.outputs))
            return {};

        if (auto* groups = runtime->getProperty ("stem_groups").getArray())
        {
            entry.groupOrder.clear();
            for (const auto& group : *groups)
                entry.groupOrder.addIfNotAlreadyThere (group.toString().trim());
            entry.groupOrder.removeEmptyStrings();
        }

        if (runtime->hasProperty ("sample_rate_hz"))
            entry.sampleRate = (double) runtime->getProperty ("sample_rate_hz");
        if (runtime->hasProperty ("chunk_size_samples"))
            entry.chunkSize = (int) runtime->getProperty ("chunk_size_samples");
        if (runtime->hasProperty ("recommended_overlap_ratio"))
            entry.overlapRatio = (float) (double) runtime->getProperty ("recommended_overlap_ratio");
        if (auto* stft = runtime->getProperty ("stft").getDynamicObject())
            parseSpectralParams (*stft, entry.spectral);
    }

    if (entry.menuLabel.isEmpty())
        entry.menuLabel = entry.id;

    const bool runtimeUsable = entry.runtimeKind == StemRuntimeKind::waveform
                                   ? entry.chunkSize > 0 && entry.overlapRatio >= 0.0f && entry.overlapRatio < 1.0f
                                   : areSpectralParamsUsable (entry.spectral, entry.runtimeKind);
    if (! entry.isValid() || ! runtimeUsable || entry.sampleRate <= 0.0)
        return {};

    return entry;
}

std::vector<StemModelCatalogEntry> getEffectiveStemModelCatalog (const juce::File& modelFolder)
{
    auto catalog = getBuiltInStemModelCatalog();

    const auto models = loadManifestModels (modelFolder);
    if (auto* modelsArray = models.getArray())
    {
        for (const auto& modelVar : *modelsArray)
        {
            const auto id = readManifestModelId (modelVar);
            if (id.isEmpty())
                continue;

            auto existing = std::find_if (catalog.begin(), catalog.end(),
                                          [&id] (const StemModelCatalogEntry& e) { return e.id == id; });
            const auto parsed = parseStemModelManifestEntry (modelVar, existing != catalog.end() ? *existing
                                                                                                  : StemModelCatalogEntry {});
            if (! parsed.isValid())
                continue;   // keep the built-in entry when the manifest's version is unusable

            if (existing != catalog.end())
                *existing = parsed;
            else
                catalog.push_back (parsed);
        }
    }

    return catalog;
}

StemModelCatalogEntry getEffectiveStemModelCatalogEntry (const StemModelId& modelId, const juce::File& modelFolder)
{
    const auto id = normaliseStemModelId (modelId);
    for (auto& entry : getEffectiveStemModelCatalog (modelFolder))
        if (entry.id == id)
            return entry;

    return {};
}

juce::File resolveStemModelFile (const juce::File& modelFolder, const StemModelId& modelId)
{
    const auto entry = getEffectiveStemModelCatalogEntry (modelId, modelFolder);
    return entry.isValid() ? modelFolder.getChildFile (entry.fileName) : juce::File();
}

StemSelection normaliseStemSelection (StemSelection selection, int numOutputs)
{
    selection.erase (std::remove_if (selection.begin(), selection.end(),
                                     [numOutputs] (int index) { return index < 0 || index >= numOutputs; }),
                     selection.end());
    std::sort (selection.begin(), selection.end());
    selection.erase (std::unique (selection.begin(), selection.end()), selection.end());
    return selection;
}

std::vector<StemModelId> scanInstalledStemModels (const juce::File& modelFolder)
{
    std::vector<StemModelId> installed;
    for (const auto& entry : getEffectiveStemModelCatalog (modelFolder))
        if (modelFolder.getChildFile (entry.fileName).existsAsFile())
            installed.push_back (entry.id);

    return installed;
}

// ── ORT bundle catalog ──────────────────────────────────────────────────

juce::String getCurrentPlatformTag()
{
    // The runtime must match the plugin's own architecture: an ARM64 process can't load x64 libraries.
   #if JUCE_WINDOWS
    #if JUCE_ARM
     return "win-arm64";
    #else
     return "win-x64";
    #endif
   #elif JUCE_MAC
    #if JUCE_ARM
     return "macos-arm64";
    #else
     return "macos-x64";
    #endif
   #elif JUCE_LINUX
    #if JUCE_ARM
     return "linux-arm64";
    #else
     return "linux-x64";
    #endif
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
        { OrtBundleId::linuxArm64Cpu,      "CPU only",           "linux-arm64-cpu",      "",         "linux-arm64", "onnxruntime-linux-arm64-cpu.zip",     "", 0, "libonnxruntime.so" },
        { OrtBundleId::winArm64Cpu,        "CPU only",           "win-arm64-cpu",        "",         "win-arm64",   "onnxruntime-win-arm64-cpu.zip",       "", 0, "onnxruntime.dll" },
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
