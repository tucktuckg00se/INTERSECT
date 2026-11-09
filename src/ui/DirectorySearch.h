#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "browser/SearchIndex.h"
#include <functional>
#include <vector>

/**
    Background search for the sample browser, working as a folder filter.

    The first request reads the folder tree under the folder being viewed once (Browser::buildIndex,
    directory listings only) and keeps it in memory. After that, each request answers "what in this
    folder matches?": the folder's matching items plus the subfolders that contain matches, with
    counts. Opening subfolders or typing more only filters memory. A folder the index doesn't hold
    (going above where the search started, jumping elsewhere, or one the walk didn't enter) reads
    that folder's tree instead.

    Results arrive on the message thread via onComplete; only the newest request is answered.
    cancel() (search cleared) drops the index, so the next search sees files added since.
*/
class DirectorySearch : private juce::Thread,
                        private juce::AsyncUpdater
{
public:
    using Row = Browser::FolderViewRow;

    struct Result
    {
        juce::String query;               // query this result was produced for
        juce::File folder;                // folder whose view this is
        std::vector<Row> rows;            // matching items and subfolders with matches inside
        int totalMatches = 0;             // matched files anywhere inside the folder
        bool incomplete = false;          // the tree was too big to read completely
    };

    /** Predicate deciding whether a non-directory file can appear in the results (audio, presets).
        Injected by the owner so the searcher stays decoupled from the file-type policy. Set before
        the first search. */
    std::function<bool (const juce::File&)> isListedFile;

    /** Invoked on the message thread when the newest request has been answered. */
    std::function<void (Result)> onComplete;

    DirectorySearch();
    ~DirectorySearch() override;

    /** Queue "what in folder matches query?". rebuildIndex re-reads the disk even when folder is
        already indexed (the browser's refresh). An empty query is ignored. */
    void search (const juce::File& folder, const juce::String& query, bool rebuildIndex = false);

    /** Cancel any pending request without delivering results, and free the index. */
    void cancel();

private:
    struct Request
    {
        juce::File folder;
        juce::String query;
        bool rebuild = false;
        bool dropIndex = false;
    };

    void run() override;
    void handleAsyncUpdate() override;
    /** True when folder is the indexed root or inside it. */
    static bool isInside (const juce::File& folder, const juce::File& root);
    /** While indexing root: stop if the newest request needs a different tree (or none). */
    bool indexingSuperseded (const juce::File& root) const;

    // Worker thread only.
    Browser::SearchIndex index;
    bool indexReady = false;
    juce::String filterQuery;            // query `filter` was computed for
    Browser::SearchFilter filter;

    juce::CriticalSection lock;
    Request pending;
    bool hasPending = false;
    Result pendingResult;
    bool hasPendingResult = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DirectorySearch)
};
