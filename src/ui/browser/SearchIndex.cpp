#include "SearchIndex.h"
#include "DirectoryListing.h"
#include <algorithm>

#if JUCE_LINUX || JUCE_MAC || JUCE_BSD
 #include <climits>
 #include <cstdlib>
 #define INTERSECT_RESOLVE_LINKS 1
#endif

namespace Browser
{
namespace
{
juce::String withSeparator (const juce::String& path)
{
    const auto separator = juce::File::getSeparatorString();
    return path.endsWith (separator) ? path : path + separator;
}

/** Where a path really points, with every symlink resolved; empty if it can't be resolved
    (and always on Windows, where listDirectory doesn't report links). */
juce::String realPathOf (const juce::String& path)
{
   #if INTERSECT_RESOLVE_LINKS
    char resolved[PATH_MAX];
    if (realpath (path.toRawUTF8(), resolved) != nullptr)
        return juce::String::fromUTF8 (resolved);
   #else
    juce::ignoreUnused (path);
   #endif
    return {};
}

/** a is b or somewhere inside it (real paths). */
bool isSameOrInside (const juce::String& a, const juce::String& b)
{
    return a == b || a.startsWith (withSeparator (b));
}

/** Decides which symlinked folders the walk enters: each real location is searched once, so a
    link back to the root, a second link to the same place or a link to "/" can't loop or
    duplicate. Only links pay for the path resolution; ordinary folders never do. */
class LinkGuard
{
public:
    explicit LinkGuard (const juce::File& root) : rootReal (realPathOf (root.getFullPathName())) {}

    bool shouldFollow (const juce::String& linkPath)
    {
        const auto target = realPathOf (linkPath);
        if (target.isEmpty() || rootReal.isEmpty())
            return false;

        auto overlaps = [&target] (const juce::String& searched)
        {
            return isSameOrInside (target, searched) || isSameOrInside (searched, target);
        };
        if (overlaps (rootReal))
            return false;
        for (const auto& followed : followedReal)
            if (overlaps (followed))
                return false;

        followedReal.add (target);
        return true;
    }

private:
    juce::String rootReal;
    juce::StringArray followedReal;
};
} // namespace

SearchQuery parseQuery (const juce::String& text)
{
    SearchQuery query;
    juce::String current;
    bool inQuotes = false;

    auto finishTerm = [&query, &current]
    {
        const auto term = current.trim().toLowerCase();
        if (term.isNotEmpty())
            query.terms.addIfNotAlreadyThere (term);
        current.clear();
    };

    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (c == '"')
        {
            finishTerm();   // a quote ends the word or phrase before it
            inQuotes = ! inQuotes;
        }
        else if (! inQuotes && juce::CharacterFunctions::isWhitespace (c))
        {
            finishTerm();
        }
        else
        {
            current += juce::String::charToString (c);
        }
    }
    finishTerm();   // an unclosed quote runs to the end
    return query;
}

bool matchesName (const juce::String& name, const SearchQuery& query)
{
    if (query.isEmpty())
        return false;

    for (const auto& term : query.terms)
        if (! name.containsIgnoreCase (term))
            return false;
    return true;
}

juce::File SearchIndex::fileFor (const Entry& entry) const
{
    return juce::File::createFileWithoutCheckingPath (dirPaths[entry.parentDir] + entry.name);
}

int SearchIndex::findFolder (const juce::File& folder) const
{
    return dirPaths.indexOf (withSeparator (folder.getFullPathName()));
}

bool buildIndex (SearchIndex& index, const juce::File& root,
                 const std::function<bool (const juce::File&)>& isListedFile,
                 const std::function<bool()>& shouldStop,
                 SearchLimits limits)
{
    index = {};
    index.root = root;

    auto addFolder = [&index] (const juce::String& path, int parent, int entry)
    {
        index.dirPaths.add (withSeparator (path));
        index.dirParent.push_back (parent);
        index.dirEntry.push_back (entry);
        index.dirChildren.push_back ({});
        return index.dirPaths.size() - 1;
    };

    std::vector<int> stack { addFolder (root.getFullPathName(), -1, -1) };
    LinkGuard links (root);

    while (! stack.empty())
    {
        if (shouldStop != nullptr && shouldStop())
            return false;

        const int dir = stack.back();
        stack.pop_back();
        const auto dirPath = index.dirPaths[dir];
        const int first = (int) index.entries.size();

        for (auto& child : listDirectory (juce::File::createFileWithoutCheckingPath (dirPath)))
        {
            if (child.name.startsWithChar ('.'))
                continue;   // hidden folders (.git, .Trash) and macOS "._" files on non-Apple drives

            if (! child.isDirectory
                && (isListedFile == nullptr
                    || ! isListedFile (juce::File::createFileWithoutCheckingPath (dirPath + child.name))))
                continue;   // not audio or a preset: never shown

            if ((int) index.entries.size() >= limits.maxEntries)
            {
                index.truncated = true;
                index.dirChildren[(size_t) dir] = { first, (int) index.entries.size() };
                return true;
            }

            index.entries.push_back ({ child.name, dir, -1, child.isDirectory });

            // Symlinked folders are searched too (a "Kicks" link to another drive), each real
            // location once; a link that would loop or repeat is listed but not walked.
            if (child.isDirectory && (! child.isSymlink || links.shouldFollow (dirPath + child.name)))
            {
                if (index.dirPaths.size() >= limits.maxFolders)
                {
                    index.truncated = true;   // still listed, just not searched inside
                    continue;
                }
                const int childDir = addFolder (dirPath + child.name, dir, (int) index.entries.size() - 1);
                index.entries.back().childDir = childDir;
                stack.push_back (childDir);
            }
        }

        // A folder's children are appended together, so a range is enough to find them.
        index.dirChildren[(size_t) dir] = { first, (int) index.entries.size() };
    }

    return true;
}

SearchFilter applyQuery (const SearchIndex& index, const SearchQuery& query)
{
    SearchFilter filter;
    const auto numEntries = index.entries.size();
    const auto numFolders = (size_t) index.dirPaths.size();
    filter.entryMatched.assign (numEntries, 0);
    filter.folderMatched.assign (numFolders, 0);
    filter.matchCount.assign (numFolders, 0);
    if (query.isEmpty())
        return filter;

    for (size_t i = 0; i < numEntries; ++i)
        filter.entryMatched[i] = matchesName (index.entries[i].name, query) ? 1 : 0;

    // Parents come before children, so one forward pass passes a folder's match down the tree.
    // The root (0) is never matched by its own name.
    for (size_t dir = 1; dir < numFolders; ++dir)
        filter.folderMatched[dir] = (filter.entryMatched[(size_t) index.dirEntry[dir]] != 0
                                     || filter.folderMatched[(size_t) index.dirParent[dir]] != 0) ? 1 : 0;

    for (size_t i = 0; i < numEntries; ++i)
    {
        const auto& entry = index.entries[i];
        if (filter.folderMatched[(size_t) entry.parentDir] != 0)
            filter.entryMatched[i] = 1;   // inside a matching folder: everything counts
        if (! entry.isDirectory && filter.entryMatched[i] != 0)
            ++filter.matchCount[(size_t) entry.parentDir];
    }

    // Children have higher indices: a reverse pass adds every folder's count to its parent.
    for (size_t dir = numFolders; dir-- > 1;)
        filter.matchCount[(size_t) index.dirParent[dir]] += filter.matchCount[dir];

    return filter;
}

FolderView folderView (const SearchIndex& index, const SearchFilter& filter, const juce::File& folder)
{
    FolderView view;
    if (filter.entryMatched.size() != index.entries.size())
        return view;   // filter from another index

    const int dir = index.findFolder (folder);
    if (dir < 0)
        return view;

    view.inIndex = true;
    view.totalMatches = filter.matchCount[(size_t) dir];

    const auto range = index.dirChildren[(size_t) dir];
    for (int i = range.first; i < range.end; ++i)
    {
        const auto& entry = index.entries[(size_t) i];
        const bool matched = filter.entryMatched[(size_t) i] != 0;
        if (entry.isDirectory)
        {
            if (entry.childDir >= 0)
            {
                // Searched inside: shown only if it leads to at least one matching sound, so an
                // empty (or artwork-only) "Kicks" folder never appears, from any level.
                const int count = filter.matchCount[(size_t) entry.childDir];
                if (count > 0)
                    view.rows.push_back ({ index.fileFor (entry), true, count });
            }
            else if (matched)
            {
                // Not searched inside (past the size limit, or a link to somewhere already
                // searched): it might hold sounds, so a matching one stays, without a count.
                view.rows.push_back ({ index.fileFor (entry), true, 0 });
            }
        }
        else if (matched)
        {
            view.rows.push_back ({ index.fileFor (entry), false, 0 });
        }
    }

    std::sort (view.rows.begin(), view.rows.end(), [] (const FolderViewRow& a, const FolderViewRow& b)
    {
        if (a.directory != b.directory)
            return a.directory;
        return a.file.getFileName().compareIgnoreCase (b.file.getFileName()) < 0;
    });

    return view;
}
} // namespace Browser
