#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

/**
    Browser search, split from the threading in DirectorySearch so it can be tested.

    Search works as a folder filter: the folder tree under a root is read once into a SearchIndex,
    a query marks what matches (applyQuery), and each folder you open shows only its matching
    items plus the subfolders that contain matches (folderView).
*/
namespace Browser
{
/** Lowercase search terms: single words, or "quoted phrases" kept whole. */
struct SearchQuery
{
    juce::StringArray terms;
    bool isEmpty() const noexcept { return terms.isEmpty(); }
};

/** Words split on whitespace; text in double quotes is one phrase. An unclosed quote runs to the
    end; empty quotes are ignored. */
SearchQuery parseQuery (const juce::String& text);

/** Every term appears in name, case-insensitive, in any order. */
bool matchesName (const juce::String& name, const SearchQuery& query);

/** Folders and listed files under a root, gathered once so a search never re-reads the disk. */
struct SearchIndex
{
    struct Entry
    {
        juce::String name;
        int parentDir = 0;         // folder it sits in (index into dirPaths)
        int childDir = -1;         // for a folder that was walked: its own index into dirPaths
        bool isDirectory = false;
    };

    struct DirRange
    {
        int first = 0;             // [first, end) into entries: this folder's children
        int end = 0;
    };

    juce::File root;
    juce::StringArray dirPaths;      // walked folders, full paths ending in a separator; 0 is root
    std::vector<int> dirParent;      // parent folder of each (always a lower index); -1 for root
    std::vector<int> dirEntry;       // the entry naming each folder; -1 for root
    std::vector<DirRange> dirChildren;
    std::vector<Entry> entries;      // every folder and listed file below root
    bool truncated = false;          // the walk stopped at a limit

    juce::File fileFor (const Entry& entry) const;
    /** Index of a walked folder, or -1 when it isn't in the index. */
    int findFolder (const juce::File& folder) const;
};

struct SearchLimits
{
    int maxFolders = 50000;
    int maxEntries = 500000;         // folders + listed files; about 60-80 MB at most
};

/** Walks root depth-first with listDirectory, indexing folders and the files isListedFile accepts.
    Skips dot-names (hidden folders, macOS "._" files). Symlinked folders are walked once per real
    location: a link that loops back or repeats an already-searched place is listed, not walked.
    shouldStop is polled per folder; returns false if it fired (index incomplete). */
bool buildIndex (SearchIndex& index, const juce::File& root,
                 const std::function<bool (const juce::File&)>& isListedFile,
                 const std::function<bool()>& shouldStop,
                 SearchLimits limits = {});

/** What a query matches in an index, computed once per query. */
struct SearchFilter
{
    std::vector<char> entryMatched;   // by entry: its name matches, or it sits in a matched folder
    std::vector<char> folderMatched;  // by folder: its name matches, or it's inside a matched one
    std::vector<int> matchCount;      // by folder: matched files anywhere inside it
};

/** A folder whose name matches counts for everything inside it, except the root itself: searching
    from inside "Kicks" still means matching by name. */
SearchFilter applyQuery (const SearchIndex& index, const SearchQuery& query);

struct FolderViewRow
{
    juce::File file;
    bool directory = false;
    int matches = 0;                  // folders: matched files inside
};

struct FolderView
{
    std::vector<FolderViewRow> rows;  // matched files, subfolders leading to a matched sound, and
                                      // matching subfolders the walk couldn't enter (no count)
    int totalMatches = 0;             // matched files anywhere inside the folder
    bool inIndex = false;             // false: the folder wasn't walked (outside root, or past a limit)
};

FolderView folderView (const SearchIndex& index, const SearchFilter& filter, const juce::File& folder);
} // namespace Browser
