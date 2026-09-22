#pragma once
#include <juce_core/juce_core.h>
#include "../../AppFiles.h"
#include <cmath>
#include <vector>

// Types and helpers shared by the browser components and the drop targets that accept browser drags.
namespace Browser
{
enum class FileKind
{
    other,
    directory,
    audio,
    preset,
};

struct FileRow
{
    juce::File file;
    FileKind kind = FileKind::other;
};

/** Classifies a non-directory by extension only (no disk access). */
inline FileKind classifyByName (const juce::File& file)
{
    if (AppFiles::isSupportedAudioFile (file))
        return FileKind::audio;
    if (AppFiles::isPresetFile (file))
        return FileKind::preset;
    return FileKind::other;
}

inline FileKind classify (const juce::File& file)
{
    return file.isDirectory() ? FileKind::directory : classifyByName (file);
}

inline bool samePath (const juce::File& a, const juce::File& b)
{
    return a.getFullPathName() == b.getFullPathName();
}

/** "0.8s", "12.4s", "1:33", "1:02:03". */
inline juce::String formatLength (double seconds)
{
    if (seconds < 0.0)
        return {};
    if (seconds < 60.0)
        return juce::String (seconds, seconds < 10.0 ? 2 : 1) + "s";

    const auto total = (int) std::lround (seconds);
    const int h = total / 3600, m = (total / 60) % 60, s = total % 60;
    const auto ss = juce::String (s).paddedLeft ('0', 2);
    return h > 0 ? juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) + ":" + ss
                 : juce::String (m) + ":" + ss;
}

/** "44.1k", "48k", "96k". */
inline juce::String formatRate (double hz)
{
    if (hz <= 0.0)
        return {};
    const double k = hz / 1000.0;
    return std::abs (k - std::round (k)) < 0.05 ? juce::String ((int) std::lround (k)) + "k"
                                                : juce::String (k, 1) + "k";
}

/** "812 B", "64 KB", "2.1 MB", "1.3 GB". */
inline juce::String formatSize (juce::int64 bytes)
{
    if (bytes < 1024)
        return juce::String (bytes) + " B";
    if (bytes < 1024 * 1024)
        return juce::String (bytes / 1024) + " KB";
    if (bytes < (juce::int64) 1024 * 1024 * 1024)
        return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
    return juce::String ((double) bytes / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
}

/** Payload prefix for rows dragged from the browser onto other INTERSECT components. */
constexpr const char* kDragPrefix = "INTERSECT_BROWSER_FILES\n";

inline juce::String makeDragDescription (const std::vector<juce::File>& files)
{
    juce::StringArray paths;
    for (const auto& file : files)
        paths.add (file.getFullPathName());

    return paths.isEmpty() ? juce::String() : juce::String (kDragPrefix) + paths.joinIntoString ("\n");
}

inline bool isBrowserDrag (const juce::String& description)
{
    return description.startsWith (kDragPrefix);
}

/** Existing absolute files named by a browser drag payload; empty for any other payload. */
inline std::vector<juce::File> parseDragDescription (const juce::String& description)
{
    std::vector<juce::File> files;
    if (! isBrowserDrag (description))
        return files;

    for (const auto& line : juce::StringArray::fromLines (description.fromFirstOccurrenceOf ("\n", false, false)))
    {
        const auto path = line.trim();
        if (juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
            files.emplace_back (path);
    }
    return files;
}
}
