#include "StemSeparation.h"

namespace
{
const std::array<StemModelCatalogEntry, 1> kStemModelCatalog =
{{
    { StemModelId::bsRoformer2stem,
      "BS-RoFormer (Vocals/Instrumental)",
      "bs_roformer_ep317_sdr12.9755_quantized_uint8.onnx",
      "https://huggingface.co/xycld/BS-RoFormer-ONNX/resolve/main/bs_roformer_ep317_sdr12.9755_quantized_uint8.onnx?download=true",
      1,
      { StemRole::vocals, StemRole::unknown, StemRole::unknown, StemRole::unknown },
      true,
      StemRole::instrumental,
      44100.0, 352800, 2048, 441, 1025, 2 },
}};
}

juce::String stemModelIdToString (StemModelId modelId)
{
    switch (modelId)
    {
        case StemModelId::bsRoformer2stem: return "bsroformer";
    }

    return "unknown";
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

juce::File resolveStemModelFile (const juce::File& modelFolder, StemModelId modelId)
{
    if (const auto* entry = findStemModelCatalogEntry (modelId))
        return modelFolder.getChildFile (entry->fileName);

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
