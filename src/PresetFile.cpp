#include "PresetFile.h"
#include "AppFiles.h"
#include <juce_cryptography/juce_cryptography.h>

namespace PresetFile
{
namespace
{
constexpr int kMagic = 0x52505849;          // bytes "IXPR" when written little-endian
constexpr int kFormatVersion = 1;
constexpr int kMaxSamples = 256;
constexpr juce::int64 kMaxStateBytes = 16 * 1024 * 1024;
constexpr juce::int64 kMaxEmbeddedBytes = (juce::int64) 2 * 1024 * 1024 * 1024;
constexpr int kCopyChunkBytes = 1 << 20;

bool shouldStop (const ShouldAbortFn& shouldAbort)
{
    return shouldAbort != nullptr && shouldAbort();
}

// Copies in chunks so a long copy can be abandoned between chunks.
bool copyBytes (juce::InputStream& in, juce::OutputStream& out, juce::int64 numBytes,
                const ShouldAbortFn& shouldAbort)
{
    juce::HeapBlock<char> buffer ((size_t) kCopyChunkBytes);
    while (numBytes > 0)
    {
        if (shouldStop (shouldAbort))
            return false;

        const int wanted = (int) juce::jmin<juce::int64> (numBytes, kCopyChunkBytes);
        const int got = in.read (buffer, wanted);
        if (got <= 0 || ! out.write (buffer, (size_t) got))
            return false;

        numBytes -= got;
    }
    return true;
}

juce::File fileFromSavedPath (const juce::String& path)
{
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

// Embedded names come from untrusted files: strip path characters, refuse "." / "..", and only
// accept audio extensions so a preset can't drop arbitrary files into the cache.
juce::String safeAudioFileName (const juce::String& name)
{
    const auto legal = juce::File::createLegalFileName (name.trim());
    if (legal.isEmpty() || legal == "." || legal == ".." || ! legal.containsChar ('.'))
        return {};

    if (! AppFiles::isSupportedAudioExtension (legal.fromLastOccurrenceOf (".", true, false)))
        return {};

    return legal;
}

juce::File extractEmbedded (const juce::File& presetFile, const SampleEntry& entry,
                            const juce::String& safeName, const juce::File& cacheDir,
                            const ShouldAbortFn& shouldAbort)
{
    juce::FileInputStream in (presetFile);
    if (! in.openedOk() || ! in.setPosition (entry.embeddedOffset))
        return {};

    // Keyed by content so re-loading a preset (or two presets sharing a sample) reuses one copy.
    const auto hash = juce::MD5 (in, entry.embeddedSize).toHexString();
    const auto target = cacheDir.getChildFile (hash).getChildFile (safeName);
    if (target.existsAsFile() && target.getSize() == entry.embeddedSize)
        return target;

    if (shouldStop (shouldAbort)
        || ! target.getParentDirectory().createDirectory()
        || ! in.setPosition (entry.embeddedOffset))
        return {};

    juce::TemporaryFile temp (target);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk() || ! copyBytes (in, out, entry.embeddedSize, shouldAbort))
            return {};

        out.flush();
        if (out.getStatus().failed())
            return {};
    }

    return temp.overwriteTargetFileWithTemporary() ? target : juce::File();
}

juce::File resolveSample (const juce::File& presetFile, const SampleEntry& entry,
                          const juce::File& cacheDir, const ShouldAbortFn& shouldAbort)
{
    auto usable = [&entry] (const juce::File& candidate)
    {
        return AppFiles::isSupportedAudioFile (candidate)
            && candidate.existsAsFile()
            && (! entry.isEmbedded() || candidate.getSize() == entry.embeddedSize);
    };

    const auto presetDir = presetFile.getParentDirectory();
    const auto safeName = safeAudioFileName (entry.fileName);

    if (const auto original = fileFromSavedPath (entry.savedPath); usable (original))
        return original;

    if (entry.relativePath.isNotEmpty())
        if (const auto relative = presetDir.getChildFile (entry.relativePath); usable (relative))
            return relative;

    if (safeName.isNotEmpty())
        if (const auto sibling = presetDir.getChildFile (safeName); usable (sibling))
            return sibling;

    if (entry.isEmbedded() && safeName.isNotEmpty())
        return extractEmbedded (presetFile, entry, safeName, cacheDir, shouldAbort);

    return {};
}
}

juce::Result write (const juce::File& destination,
                    const juce::MemoryBlock& state,
                    const juce::StringArray& samplePaths,
                    bool embedSamples,
                    const ShouldAbortFn& shouldAbort)
{
    std::vector<SampleSource> samples;
    samples.reserve ((size_t) samplePaths.size());
    for (const auto& path : samplePaths)
        samples.push_back ({ path, fileFromSavedPath (path) });
    return write (destination, state, samples, embedSamples, shouldAbort);
}

juce::Result write (const juce::File& destination,
                    const juce::MemoryBlock& state,
                    const std::vector<SampleSource>& samples,
                    bool embedSamples,
                    const ShouldAbortFn& shouldAbort)
{
    const auto presetDir = destination.getParentDirectory();
    if (! presetDir.createDirectory())
        return juce::Result::fail ("Couldn't create folder " + presetDir.getFullPathName());

    // Check the embed budget before writing anything.
    juce::int64 embeddedTotal = 0;
    if (embedSamples)
        for (const auto& sample : samples)
            if (sample.source.existsAsFile())
                embeddedTotal += sample.source.getSize();

    if (embeddedTotal > kMaxEmbeddedBytes)
        return juce::Result::fail ("Samples are too large to embed in a preset (over 2 GB)");

    const auto writeFailed = juce::Result::fail ("Couldn't write preset " + destination.getFileName());

    juce::TemporaryFile temp (destination);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk())
            return writeFailed;

        out.writeInt (kMagic);
        out.writeInt (kFormatVersion);
        out.writeString (JucePlugin_VersionString);
        out.writeInt64 ((juce::int64) state.getSize());
        out.write (state.getData(), state.getSize());

        out.writeInt ((int) samples.size());
        for (const auto& sample : samples)
        {
            const auto& file = sample.source;
            out.writeString (sample.savedPath);
            out.writeString (file != juce::File()
                                 ? file.getRelativePathFrom (presetDir).replaceCharacter ('\\', '/')
                                 : juce::String());
            out.writeString (file != juce::File() ? file.getFileName()
                                                  : fileFromSavedPath (sample.savedPath).getFileName());

            const bool embed = embedSamples && file.existsAsFile();
            out.writeBool (embed);
            if (! embed)
                continue;

            juce::FileInputStream in (file);
            if (! in.openedOk())
                return juce::Result::fail ("Couldn't read " + file.getFileName() + " to embed it");

            const auto size = in.getTotalLength();
            out.writeInt64 (size);
            if (! copyBytes (in, out, size, shouldAbort))
                return shouldStop (shouldAbort) ? juce::Result::fail ("Preset save cancelled")
                                                : juce::Result::fail ("Couldn't embed " + file.getFileName());
        }

        out.flush();
        if (out.getStatus().failed())
            return juce::Result::fail (writeFailed.getErrorMessage() + ": " + out.getStatus().getErrorMessage());
    }

    if (! temp.overwriteTargetFileWithTemporary())
        return writeFailed;

    return juce::Result::ok();
}

juce::Result read (const juce::File& presetFile, Contents& out)
{
    out = {};

    juce::FileInputStream in (presetFile);
    if (! in.openedOk())
        return juce::Result::fail ("Couldn't open " + presetFile.getFileName());

    auto remaining = [&in] { return in.getTotalLength() - in.getPosition(); };
    const auto notAPreset = juce::Result::fail (presetFile.getFileName() + " is not an INTERSECT preset");
    const auto damaged = juce::Result::fail (presetFile.getFileName() + " is damaged or incomplete");

    if (remaining() < 8 || in.readInt() != kMagic)
        return notAPreset;

    const int version = in.readInt();
    if (version < 1)
        return notAPreset;
    if (version > kFormatVersion)
        return juce::Result::fail (presetFile.getFileName() + " needs a newer version of INTERSECT");

    (void) in.readString();   // saving plugin version, informational only

    if (remaining() < 8)
        return damaged;

    const auto stateSize = in.readInt64();
    if (stateSize <= 0 || stateSize > kMaxStateBytes || stateSize > remaining())
        return damaged;

    out.state.setSize ((size_t) stateSize);
    if (in.read (out.state.getData(), (int) stateSize) != (int) stateSize)
        return damaged;

    if (remaining() < 4)
        return damaged;

    const int numSamples = in.readInt();
    if (numSamples < 0 || numSamples > kMaxSamples)
        return damaged;

    out.samples.reserve ((size_t) numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        SampleEntry entry;
        entry.savedPath = in.readString();
        entry.relativePath = in.readString();
        entry.fileName = in.readString();

        if (remaining() < 1)
            return damaged;

        if (in.readBool())
        {
            if (remaining() < 8)
                return damaged;

            entry.embeddedSize = in.readInt64();
            if (entry.embeddedSize < 0 || entry.embeddedSize > remaining())
                return damaged;

            entry.embeddedOffset = in.getPosition();
            if (! in.setPosition (entry.embeddedOffset + entry.embeddedSize))
                return damaged;
        }

        out.samples.push_back (std::move (entry));
    }

    return juce::Result::ok();
}

std::vector<juce::File> resolveSamples (const juce::File& presetFile,
                                        const Contents& contents,
                                        const juce::File& cacheDir,
                                        const ShouldAbortFn& shouldAbort)
{
    std::vector<juce::File> resolved;
    resolved.reserve (contents.samples.size());
    for (const auto& entry : contents.samples)
        resolved.push_back (shouldStop (shouldAbort) ? juce::File()
                                                     : resolveSample (presetFile, entry, cacheDir, shouldAbort));
    return resolved;
}

juce::Result exportWithSamples (const juce::File& source,
                                const juce::File& destination,
                                const juce::File& cacheDir,
                                int& missingSamples,
                                const ShouldAbortFn& shouldAbort)
{
    missingSamples = 0;

    Contents contents;
    if (auto result = read (source, contents); result.failed())
        return result;

    const auto resolved = resolveSamples (source, contents, cacheDir, shouldAbort);
    if (shouldStop (shouldAbort))
        return juce::Result::fail ("Preset export cancelled");

    // Keep each entry's saved path as the key (the state blob refers to it); embed whatever local
    // copy was found for it.
    std::vector<SampleSource> samples;
    samples.reserve (contents.samples.size());
    for (size_t i = 0; i < contents.samples.size(); ++i)
    {
        const auto& found = i < resolved.size() ? resolved[i] : juce::File();
        if (found == juce::File())
            ++missingSamples;
        samples.push_back ({ contents.samples[i].savedPath, found });
    }

    return write (destination, contents.state, samples, true, shouldAbort);
}
}
