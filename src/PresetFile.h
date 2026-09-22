#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

/** Reads and writes .intersectpreset files: the plugin's saved-state blob plus a table recording
    where each session sample lived and, optionally, the sample's original file bytes.

    Pure file I/O with no knowledge of the processor, so it can run on any background thread.
    Presets can come from anyone, so every size and name read from disk is validated. */
namespace PresetFile
{
struct SampleEntry
{
    juce::String savedPath;          // absolute path when the preset was saved
    juce::String relativePath;       // the same file relative to the preset's folder, '/'-separated
    juce::String fileName;           // original file name
    juce::int64 embeddedOffset = -1; // byte offset of the embedded audio inside the preset, -1 if none
    juce::int64 embeddedSize = 0;

    bool isEmbedded() const noexcept { return embeddedOffset >= 0; }
};

struct Contents
{
    juce::MemoryBlock state;         // exact getStateInformation() output
    std::vector<SampleEntry> samples;
};

/** Polled during long copies; return true to abandon the operation. */
using ShouldAbortFn = std::function<bool()>;

/** One sample-table entry to write. savedPath is the key the state blob refers to; source is the
    file whose bytes are embedded (the same file for a fresh save, possibly a resolved or cached
    copy when re-exporting an existing preset). */
struct SampleSource
{
    juce::String savedPath;
    juce::File source;
};

/** Writes a preset atomically (temp file + replace), so a failed save never damages an existing
    file. With embedSamples, each source file that exists is copied into the preset. */
juce::Result write (const juce::File& destination,
                    const juce::MemoryBlock& state,
                    const std::vector<SampleSource>& samples,
                    bool embedSamples,
                    const ShouldAbortFn& shouldAbort = {});

/** Convenience for a fresh save: each path is both the key and the source. */
juce::Result write (const juce::File& destination,
                    const juce::MemoryBlock& state,
                    const juce::StringArray& samplePaths,
                    bool embedSamples,
                    const ShouldAbortFn& shouldAbort = {});

/** Writes a copy of an existing preset with every sample it can find embedded, using the same
    lookup as loading. missingSamples reports entries that could not be found (left unembedded). */
juce::Result exportWithSamples (const juce::File& source,
                                const juce::File& destination,
                                const juce::File& cacheDir,
                                int& missingSamples,
                                const ShouldAbortFn& shouldAbort = {});

/** Parses the header and sample table. Embedded audio is not loaded, only located. */
juce::Result read (const juce::File& presetFile, Contents& out);

/** Finds a local file for each sample entry, in order: the saved absolute path, the path relative
    to the preset, a file of the same name next to the preset, then the embedded copy (unpacked into
    cacheDir). When the preset embeds a sample, a candidate only counts if its size matches.
    Returns one File per entry; an empty File means the sample could not be found. */
std::vector<juce::File> resolveSamples (const juce::File& presetFile,
                                        const Contents& contents,
                                        const juce::File& cacheDir,
                                        const ShouldAbortFn& shouldAbort = {});
}
