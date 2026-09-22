#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "../../AppFiles.h"
#include "../../PresetFile.h"
#include <functional>
#include <map>
#include <set>
#include <vector>

/**
    Background reader for the metadata shown in the browser: audio length, rate, channels and
    bit depth (from the file header only), and the sample count of presets.

    Requests come from the message thread, usually for rows as they are painted, and the most
    recent request is served first so the visible rows fill in quickly while scrolling.
    Results are cached for the life of the browser; clear() forgets them (e.g. on refresh).

    Kept header-only to match DirectorySearch.h.
*/
class FileInfoCache : private juce::Thread,
                      private juce::AsyncUpdater
{
public:
    struct Info
    {
        bool readable = false;
        double lengthSeconds = 0.0;
        double sampleRate = 0.0;
        int numChannels = 0;
        int bitsPerSample = 0;
        bool floatingPoint = false;
        juce::int64 sizeBytes = 0;
        int presetSampleCount = -1;         // presets only
        bool presetEmbedsSamples = false;   // presets only
        juce::StringArray presetSampleNames;
    };

    /** Invoked on the message thread when one or more results arrive. */
    std::function<void()> onInfoReady;

    FileInfoCache() : juce::Thread ("intersect-browser-info")
    {
        startThread (juce::Thread::Priority::low);
    }

    ~FileInfoCache() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (2000);
        cancelPendingUpdate();
    }

    /** Message thread. Returns nullptr until the file's info has been read. */
    const Info* find (const juce::File& file) const
    {
        const auto it = cache.find (file.getFullPathName());
        return it != cache.end() ? &it->second : nullptr;
    }

    /** Message thread. Queues the file unless it is cached or already queued. */
    void request (const juce::File& file)
    {
        const auto key = file.getFullPathName();
        if (cache.count (key) > 0 || requested.count (key) > 0)
            return;

        requested.insert (key);
        {
            const juce::ScopedLock sl (lock);
            pending.push_back (file);
        }
        notify();
    }

    /** Message thread. Drops queued requests, e.g. when the listing changes. */
    void cancelPending()
    {
        const juce::ScopedLock sl (lock);
        pending.clear();
        requested.clear();
    }

    /** Message thread. Forgets everything so files are re-read. */
    void clear()
    {
        cancelPending();
        cache.clear();
    }

private:
    void run() override
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        while (! threadShouldExit())
        {
            juce::File next;
            {
                const juce::ScopedLock sl (lock);
                if (! pending.empty())
                {
                    next = pending.back();
                    pending.pop_back();
                }
            }

            if (next == juce::File())
            {
                wait (-1);
                continue;
            }

            auto info = readInfo (formats, next);
            {
                const juce::ScopedLock sl (lock);
                results.emplace_back (next.getFullPathName(), std::move (info));
            }
            triggerAsyncUpdate();
        }
    }

    static Info readInfo (juce::AudioFormatManager& formats, const juce::File& file)
    {
        Info info;
        info.sizeBytes = file.getSize();

        if (AppFiles::isPresetFile (file))
        {
            PresetFile::Contents contents;
            if (PresetFile::read (file, contents).wasOk())
            {
                info.readable = true;
                info.presetSampleCount = (int) contents.samples.size();
                for (const auto& sample : contents.samples)
                {
                    info.presetEmbedsSamples = info.presetEmbedsSamples || sample.isEmbedded();
                    info.presetSampleNames.add (sample.fileName.isNotEmpty() ? sample.fileName : sample.savedPath);
                }
            }
            return info;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            return info;

        info.readable = true;
        info.sampleRate = reader->sampleRate;
        info.lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;
        info.numChannels = (int) reader->numChannels;
        info.bitsPerSample = (int) reader->bitsPerSample;
        info.floatingPoint = reader->usesFloatingPointData;
        return info;
    }

    void handleAsyncUpdate() override
    {
        std::vector<std::pair<juce::String, Info>> ready;
        {
            const juce::ScopedLock sl (lock);
            ready.swap (results);
        }

        if (cache.size() + ready.size() > kMaxEntries)
            cache.clear();

        for (auto& entry : ready)
        {
            requested.erase (entry.first);
            cache[entry.first] = std::move (entry.second);
        }

        if (onInfoReady != nullptr)
            onInfoReady();
    }

    static constexpr size_t kMaxEntries = 20000;

    // Message thread only.
    std::map<juce::String, Info> cache;
    std::set<juce::String> requested;

    // Shared with the worker, guarded by lock.
    juce::CriticalSection lock;
    std::vector<juce::File> pending;
    std::vector<std::pair<juce::String, Info>> results;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileInfoCache)
};
