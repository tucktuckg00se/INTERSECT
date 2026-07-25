#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <algorithm>
#include <functional>
#include <vector>

/**
    Background recursive file/folder searcher for the sample browser.

    Scans a root directory and all of its subfolders on a worker thread, matching file and
    folder names against a case-insensitive partial query. Results are delivered on the message
    thread via onComplete. Only the newest search() request is honoured: older in-flight scans
    are abandoned as soon as a newer request arrives, so rapid typing never floods the UI.

    Kept header-only to match the AudioAnalysis.h / AdsrEnvelope.h precedent.
*/
class DirectorySearch : private juce::Thread,
                        private juce::AsyncUpdater
{
public:
    struct Match
    {
        juce::File file;
        bool directory = false;
        bool audio = false;
    };

    struct Result
    {
        juce::String query;               // query this result was produced for
        std::vector<Match> matches;
        bool truncated = false;           // hit kMaxResults / kMaxDirs before finishing
    };

    /** Predicate deciding whether a non-directory file counts as an audio result. Injected by
        the owner so the searcher stays decoupled from the file-type policy. */
    std::function<bool (const juce::File&)> isAudioFile;

    /** Invoked on the message thread when a scan for the newest request completes. */
    std::function<void (Result)> onComplete;

    DirectorySearch() : juce::Thread ("intersect-browser-search")
    {
        startThread();
    }

    ~DirectorySearch() override
    {
        signalThreadShouldExit();
        notify();                 // wake the wait(-1) in run() (signalThreadShouldExit does not)
        stopThread (2000);
        cancelPendingUpdate();
    }

    /** Queue a recursive search of root for query. An empty query cancels any pending scan. */
    void search (const juce::File& root, const juce::String& query)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingRoot = root;
            pendingQuery = query;
            ++requestGeneration;
            hasPending = true;
        }
        notify();
    }

    /** Cancel any in-flight/pending search without delivering results. */
    void cancel()
    {
        search ({}, {});
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (-1.0);

            for (;;)
            {
                if (threadShouldExit())
                    return;

                juce::File root;
                juce::String query;
                int generation = 0;
                {
                    const juce::ScopedLock sl (lock);
                    if (! hasPending)
                        break;
                    hasPending = false;
                    root = pendingRoot;
                    query = pendingQuery;
                    generation = requestGeneration;
                }

                if (query.isEmpty() || ! root.isDirectory())
                    continue;   // nothing to deliver for an empty/invalid request

                Result result;
                result.query = query;
                if (! scan (root, query, generation, result))
                    continue;   // superseded or asked to exit — pick up the newest request

                {
                    const juce::ScopedLock sl (lock);
                    pendingResult = std::move (result);
                    hasPendingResult = true;
                }
                triggerAsyncUpdate();
            }
        }
    }

    /** Returns false if the scan was cancelled (superseded / exiting), true if it finished. */
    bool scan (const juce::File& root, const juce::String& query, int generation, Result& result)
    {
        const auto needle = query.toLowerCase();

        std::vector<juce::File> stack;
        stack.push_back (root);

        int dirsVisited = 0;

        while (! stack.empty())
        {
            if (threadShouldExit() || generationChanged (generation))
                return false;

            const auto dir = stack.back();
            stack.pop_back();

            if (++dirsVisited > kMaxDirs)
            {
                result.truncated = true;
                break;
            }

            const auto children = dir.findChildFiles (juce::File::findFilesAndDirectories,
                                                      false,
                                                      "*",
                                                      juce::File::FollowSymlinks::no);

            for (const auto& child : children)
            {
                const bool isDir = child.isDirectory();
                const bool isAudio = (! isDir) && isAudioFile != nullptr && isAudioFile (child);

                // Descend into subfolders, but skip hidden/system trees (.git, .Trash, …).
                if (isDir && ! child.getFileName().startsWithChar ('.'))
                    stack.push_back (child);

                if (! isDir && ! isAudio)
                    continue;   // non-audio file — never a result and nothing to descend

                if (child.getFileName().toLowerCase().contains (needle))
                {
                    result.matches.push_back ({ child, isDir, isAudio });
                    if ((int) result.matches.size() >= kMaxResults)
                    {
                        result.truncated = true;
                        stack.clear();
                        break;
                    }
                }
            }
        }

        // Directories first, then file name case-insensitive (matches the name shown in each row).
        std::sort (result.matches.begin(), result.matches.end(), [] (const Match& a, const Match& b)
        {
            if (a.directory != b.directory)
                return a.directory;
            return a.file.getFileName().compareIgnoreCase (b.file.getFileName()) < 0;
        });

        return true;
    }

    bool generationChanged (int generation) const
    {
        const juce::ScopedLock sl (lock);
        return requestGeneration != generation;
    }

    void handleAsyncUpdate() override
    {
        Result result;
        {
            const juce::ScopedLock sl (lock);
            if (! hasPendingResult)
                return;
            result = std::move (pendingResult);
            pendingResult = {};
            hasPendingResult = false;
        }

        if (onComplete != nullptr)
            onComplete (std::move (result));
    }

    static constexpr int kMaxResults = 500;
    static constexpr int kMaxDirs = 20000;

    juce::CriticalSection lock;
    juce::File pendingRoot;
    juce::String pendingQuery;
    Result pendingResult;
    int requestGeneration = 0;
    bool hasPending = false;
    bool hasPendingResult = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DirectorySearch)
};
