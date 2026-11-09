#include "DirectorySearch.h"

DirectorySearch::DirectorySearch() : juce::Thread ("intersect-browser-search")
{
    startThread();
}

DirectorySearch::~DirectorySearch()
{
    signalThreadShouldExit();
    notify();                 // wake the wait(-1) in run() (signalThreadShouldExit does not)
    stopThread (2000);
    cancelPendingUpdate();
}

void DirectorySearch::search (const juce::File& folder, const juce::String& query, bool rebuildIndex)
{
    {
        const juce::ScopedLock sl (lock);
        // A rebuild or drop still waiting stays requested when a newer request replaces it.
        const bool keepRebuild = hasPending && pending.rebuild;
        const bool keepDrop = hasPending && pending.dropIndex;
        pending = { folder, query, rebuildIndex || keepRebuild, keepDrop };
        hasPending = true;
    }
    notify();
}

void DirectorySearch::cancel()
{
    {
        const juce::ScopedLock sl (lock);
        pending = { {}, {}, false, true };
        hasPending = true;
    }
    notify();
}

bool DirectorySearch::isInside (const juce::File& folder, const juce::File& root)
{
    return folder == root || folder.isAChildOf (root);
}

bool DirectorySearch::indexingSuperseded (const juce::File& root) const
{
    if (threadShouldExit())
        return true;

    // Opening subfolders or typing during the walk is answered from this index once it's built;
    // only a folder outside it, a refresh or a cancel makes the walk pointless.
    const juce::ScopedLock sl (lock);
    return hasPending && (pending.dropIndex || pending.rebuild || ! isInside (pending.folder, root));
}

void DirectorySearch::run()
{
    while (! threadShouldExit())
    {
        wait (-1.0);

        for (;;)
        {
            if (threadShouldExit())
                return;

            Request request;
            {
                const juce::ScopedLock sl (lock);
                if (! hasPending)
                    break;
                request = pending;
                pending = {};
                hasPending = false;
            }

            if (request.dropIndex)
            {
                index = {};   // frees the memory; the next search re-reads the disk
                filter = {};
                filterQuery.clear();
                indexReady = false;
            }

            if (request.query.isEmpty() || ! request.folder.isDirectory())
                continue;   // nothing to deliver for an empty/invalid request

            // The index must hold the folder being viewed. Going above where the search started,
            // jumping elsewhere, or opening a folder the walk didn't enter (past the size limit, a
            // link to somewhere already searched) reads that folder's tree instead.
            if (request.rebuild || ! indexReady || index.findFolder (request.folder) < 0)
            {
                filter = {};
                filterQuery.clear();
                indexReady = Browser::buildIndex (index, request.folder, isListedFile,
                                                  [this, root = request.folder] { return indexingSuperseded (root); });
                if (! indexReady)
                {
                    index = {};
                    continue;   // superseded or exiting: pick up the newest request
                }

                // Browsing or typing went on during the walk: answer the newest request instead.
                const juce::ScopedLock sl (lock);
                if (hasPending && ! pending.dropIndex && ! pending.rebuild && pending.query.isNotEmpty()
                    && index.findFolder (pending.folder) >= 0)
                {
                    request = pending;
                    pending = {};
                    hasPending = false;
                }
            }

            if (request.query != filterQuery)
            {
                filter = Browser::applyQuery (index, Browser::parseQuery (request.query));
                filterQuery = request.query;
            }

            auto view = Browser::folderView (index, filter, request.folder);

            Result result;
            result.query = request.query;
            result.folder = request.folder;
            result.rows = std::move (view.rows);
            result.totalMatches = view.totalMatches;
            result.incomplete = index.truncated || ! view.inIndex;

            {
                const juce::ScopedLock sl (lock);
                pendingResult = std::move (result);
                hasPendingResult = true;
            }
            triggerAsyncUpdate();
        }
    }
}

void DirectorySearch::handleAsyncUpdate()
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
