#pragma once
#include <juce_core/juce_core.h>

// File-type rules and on-disk locations shared by the processor, the editor and the browser.
namespace AppFiles
{
constexpr const char* kPresetExtension = ".intersectpreset";

inline bool isSupportedAudioExtension (const juce::String& extension)
{
    const auto ext = extension.toLowerCase();
    return ext == ".wav" || ext == ".ogg" || ext == ".aiff"
        || ext == ".aif" || ext == ".flac" || ext == ".mp3";
}

inline bool isSupportedAudioFile (const juce::File& file)
{
    return isSupportedAudioExtension (file.getFileExtension());
}

inline bool isPresetFile (const juce::File& file)
{
    return file.hasFileExtension (kPresetExtension);
}

/** Per-user INTERSECT folder: settings.yaml, themes/, presets/, downloaded runtimes and models. */
inline juce::File getSettingsDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("INTERSECT");
}

inline juce::File getPresetsDir()
{
    return getSettingsDir().getChildFile ("presets");
}

/** Where audio embedded in presets is unpacked on load, one content-hash folder per file. */
inline juce::File getPresetSampleCacheDir()
{
    return getSettingsDir().getChildFile ("preset-samples");
}
}
