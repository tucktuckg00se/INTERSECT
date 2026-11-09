#include "ui/browser/DirectoryListing.h"
#include "ui/browser/SearchIndex.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#if JUCE_LINUX || JUCE_MAC || JUCE_BSD
 #include <unistd.h>
#endif

namespace
{
void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

bool isAudioOrPreset (const juce::File& file)
{
    for (const auto* ext : { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".rx2", ".intersectpreset" })
        if (file.hasFileExtension (ext))
            return true;
    return false;
}

/** A temp folder tree, deleted on scope exit. */
struct TempTree
{
    juce::File root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("intersect-search-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));

    TempTree() { require (root.createDirectory().wasOk(), "temp dir"); }
    ~TempTree() { root.deleteRecursively(); }

    juce::File file (const juce::String& relative)
    {
        auto f = root.getChildFile (relative);
        require (f.getParentDirectory().createDirectory().wasOk() && f.create().wasOk(), "create " + relative.toStdString());
        return f;
    }
};

juce::StringArray names (const Browser::FolderView& view)
{
    juce::StringArray out;
    for (const auto& row : view.rows)
        out.add (row.file.getFileName());
    return out;
}

int matchesFor (const Browser::FolderView& view, const juce::String& folderName)
{
    for (const auto& row : view.rows)
        if (row.directory && row.file.getFileName() == folderName)
            return row.matches;
    return -1;
}

void queriesParse()
{
    using Browser::parseQuery;
    require (parseQuery ("808 kick").terms == juce::StringArray ("808", "kick"), "words");
    require (parseQuery ("  Kick   808 ").terms == juce::StringArray ("kick", "808"), "spaces and case");
    require (parseQuery ("\"808 kick\" hard").terms == juce::StringArray ("808 kick", "hard"), "phrase + word");
    require (parseQuery ("hard \"808 kick").terms == juce::StringArray ("hard", "808 kick"), "unclosed quote runs to the end");
    require (parseQuery ("\"\" kick").terms == juce::StringArray ("kick"), "empty quotes ignored");
    require (parseQuery ("   ").isEmpty(), "blank");
}

void namesMatch()
{
    using Browser::matchesName;
    using Browser::parseQuery;
    require (matchesName ("Kick 808 Hard.wav", parseQuery ("808 kick")), "any order");
    require (matchesName ("KICK_808.wav", parseQuery ("808 kick")), "case-insensitive, no spaces needed");
    require (! matchesName ("Kick Hard.wav", parseQuery ("808 kick")), "every word must appear");
    require (! matchesName ("Kick 808 Hard.wav", parseQuery ("\"808 kick\"")), "phrase needs that order");
    require (matchesName ("Big 808 Kick.wav", parseQuery ("\"808 kick\"")), "phrase found");
    require (matchesName ("snare.wav", parseQuery ("nar")), "single partial word, as before");
    require (! matchesName ("snare.wav", parseQuery ("")), "empty query matches nothing");
}

void directoriesList()
{
    TempTree tree;
    tree.file ("kick.wav");
    tree.file ("notes.txt");
    tree.file (".hidden.wav");
    require (tree.root.getChildFile ("Drums").createDirectory().wasOk(), "subfolder");

    auto entries = Browser::listDirectory (tree.root);
    auto find = [&entries] (const juce::String& name) -> const Browser::DirEntry*
    {
        for (const auto& e : entries)
            if (e.name == name)
                return &e;
        return nullptr;
    };

    require (entries.size() == 4, "all four entries, no . or ..");
    require (find ("Drums") != nullptr && find ("Drums")->isDirectory, "folder typed");
    require (find ("kick.wav") != nullptr && ! find ("kick.wav")->isDirectory, "file typed");
    require (find (".hidden.wav") != nullptr, "the lister itself keeps dot-names (callers skip them)");

   #if JUCE_LINUX || JUCE_MAC || JUCE_BSD
    const auto link = tree.root.getChildFile ("DrumsLink");
    require (symlink (tree.root.getChildFile ("Drums").getFullPathName().toRawUTF8(), link.getFullPathName().toRawUTF8()) == 0,
             "symlink");
    entries = Browser::listDirectory (tree.root);
    require (find ("DrumsLink") != nullptr && find ("DrumsLink")->isDirectory && find ("DrumsLink")->isSymlink,
             "a symlink to a folder lists as a folder and is marked");
   #endif

    require (Browser::listDirectory (tree.root.getChildFile ("missing")).empty(), "unreadable folder is empty");
}

void folderViewsFilter()
{
    TempTree tree;
    tree.file ("Kick Big Room.wav");
    tree.file ("Drums/kick.wav");
    tree.file ("Drums/hat.wav");
    tree.file ("Drums/Snares/snare.wav");
    tree.file ("Kicks/BD 01.wav");               // only its folder says "kick"
    tree.file ("Kicks/Sub/BD 02.wav");
    tree.file ("Loops/loop.wav");
    tree.file ("Drums/readme.txt");
    tree.file (".git/kick-in-hidden.wav");
    tree.file ("Drums/._kick.wav");              // macOS junk next to the real file
    tree.file ("One Shots/Big 808 Kick.aif");
    tree.file ("One Shots/Kick 808 Hard.aif");

   #if JUCE_LINUX || JUCE_MAC || JUCE_BSD
    // A loop back to the root must not be walked (or the index would never finish).
    require (symlink (tree.root.getFullPathName().toRawUTF8(),
                      tree.root.getChildFile ("Drums/LoopBack").getFullPathName().toRawUTF8()) == 0, "loop symlink");
   #endif

    Browser::SearchIndex index;
    require (Browser::buildIndex (index, tree.root, isAudioOrPreset, nullptr), "build");
    require (! index.truncated, "small tree is complete");

    const auto kick = Browser::applyQuery (index, Browser::parseQuery ("kick"));

    // The root: its own matching file, and subfolders that contain matches, with counts.
    auto view = Browser::folderView (index, kick, tree.root);
    require (view.inIndex, "root is indexed");
    auto rootNames = names (view);
    require (rootNames.contains ("Kick Big Room.wav"), "matching file in this folder");
    require (rootNames.contains ("Drums") && matchesFor (view, "Drums") == 1, "Drums holds one match");
    require (rootNames.contains ("Kicks") && matchesFor (view, "Kicks") == 2, "a matching folder counts everything inside");
    require (rootNames.contains ("One Shots") && matchesFor (view, "One Shots") == 2, "One Shots holds two");
    require (! rootNames.contains ("Loops"), "folders without matches are hidden");
    require (! rootNames.contains ("kick.wav"), "deeper files aren't listed at the root");
    require (! rootNames.contains (".git"), "hidden folders not indexed");
    require (view.rows.front().directory, "folders first");
    require (view.totalMatches == 6, "every match below the root: 1 + 1 + 2 + 2");

    // Opening a folder shows its own matches; subfolders without any are hidden.
    view = Browser::folderView (index, kick, tree.root.getChildFile ("Drums"));
    require (names (view) == juce::StringArray ("kick.wav"), "Drums shows kick.wav only (not hat, Snares, ._kick, readme)");

    // Inside a folder whose name matched, everything counts.
    view = Browser::folderView (index, kick, tree.root.getChildFile ("Kicks"));
    require (names (view).contains ("BD 01.wav") && names (view).contains ("Sub") && matchesFor (view, "Sub") == 1,
             "Kicks shows all its files and its subfolder");

    // Word order and phrases.
    const auto words = Browser::applyQuery (index, Browser::parseQuery ("808 kick"));
    require (Browser::folderView (index, words, tree.root.getChildFile ("One Shots")).rows.size() == 2, "both words, any order");
    const auto phrase = Browser::applyQuery (index, Browser::parseQuery ("\"808 kick\""));
    require (names (Browser::folderView (index, phrase, tree.root.getChildFile ("One Shots")))
                 == juce::StringArray ("Big 808 Kick.aif"), "exact phrase");

    // Searching from inside "Kicks": the root's own name doesn't make everything match.
    Browser::SearchIndex kicksIndex;
    require (Browser::buildIndex (kicksIndex, tree.root.getChildFile ("Kicks"), isAudioOrPreset, nullptr), "build kicks");
    const auto inKicks = Browser::applyQuery (kicksIndex, Browser::parseQuery ("kick"));
    require (Browser::folderView (kicksIndex, inKicks, tree.root.getChildFile ("Kicks")).rows.empty(),
             "the search root's name isn't inherited");

    // A folder outside the index.
    require (! Browser::folderView (index, kick, tree.root.getParentDirectory()).inIndex, "outside the index");
}

void limitsStopTheWalk()
{
    TempTree tree;
    for (int i = 0; i < 30; ++i)
        tree.file ("F" + juce::String (i) + "/a.wav");

    Browser::SearchIndex index;
    require (Browser::buildIndex (index, tree.root, isAudioOrPreset, nullptr, { 1000, 20 }), "entry cap build");
    require (index.truncated && (int) index.entries.size() == 20, "entry cap");

    require (Browser::buildIndex (index, tree.root, isAudioOrPreset, nullptr, { 5, 1000 }), "folder cap build");
    require (index.truncated && index.dirPaths.size() == 5, "folder cap");

    int polls = 0;
    require (! Browser::buildIndex (index, tree.root, isAudioOrPreset, [&polls] { return ++polls > 3; }),
             "shouldStop aborts the walk");
}

void foldersMustLeadToASound()
{
    TempTree tree;
    TempTree outside;
    require (tree.root.getChildFile ("Drums/Kicks").createDirectory().wasOk(), "empty Kicks");
    tree.file ("Drums/hat.wav");
    tree.file ("Perc/Kick Notes/readme.txt");   // matches by name, holds no audio
    outside.file ("kick 01.wav");
    outside.file ("sub/kick 02.wav");

   #if JUCE_LINUX || JUCE_MAC || JUCE_BSD
    auto link = [] (const juce::File& target, const juce::File& at)
    {
        require (symlink (target.getFullPathName().toRawUTF8(), at.getFullPathName().toRawUTF8()) == 0, "symlink");
    };
    link (outside.root, tree.root.getChildFile ("Linked"));                            // another drive, say
    link (outside.root, tree.root.getChildFile ("Linked Again"));                      // same place twice
    link (tree.root.getChildFile ("Drums"), tree.root.getChildFile ("Kick Shortcut")); // back inside the root
   #endif

    Browser::SearchIndex index;
    require (Browser::buildIndex (index, tree.root, isAudioOrPreset, nullptr), "build");
    const auto kick = Browser::applyQuery (index, Browser::parseQuery ("kick"));

    const auto view = Browser::folderView (index, kick, tree.root);
    const auto rootNames = names (view);
    require (! rootNames.contains ("Drums"), "Drums only leads to an empty Kicks");
    require (! rootNames.contains ("Perc"), "a matching folder holding only a .txt leads to no sound");
    require (Browser::folderView (index, kick, tree.root.getChildFile ("Drums")).rows.empty(),
             "the empty Kicks is hidden from inside Drums too");

   #if JUCE_LINUX || JUCE_MAC || JUCE_BSD
    // Both links point at the same place: whichever is listed first is searched, the other is
    // listed but not walked (and hidden, since its name doesn't match).
    const int linked = matchesFor (view, "Linked");
    const int again = matchesFor (view, "Linked Again");
    require ((linked == 2 && again == -1) || (linked == -1 && again == 2), "the outside folder is searched once");

    const auto walkedLink = tree.root.getChildFile (linked == 2 ? "Linked" : "Linked Again");
    const auto inLink = Browser::folderView (index, kick, walkedLink);
    require (names (inLink).contains ("kick 01.wav") && matchesFor (inLink, "sub") == 1,
             "files are reachable through the link's own path");

    require (matchesFor (view, "Kick Shortcut") == 0, "a matching link into the root shows, without a count");
    require (index.findFolder (tree.root.getChildFile ("Kick Shortcut")) < 0,
             "it isn't walked again (opening it re-searches from there)");
   #endif
}

void foldersPastTheLimitStillShow()
{
    TempTree tree;
    tree.file ("A Kicks/x.wav");
    tree.file ("B Kicks/y.wav");
    tree.file ("C Kicks/z.wav");

    Browser::SearchIndex index;
    require (Browser::buildIndex (index, tree.root, isAudioOrPreset, nullptr, { 2, 1000 }), "build");   // root + 1 folder
    require (index.truncated, "folder limit hit");

    const auto view = Browser::folderView (index, Browser::applyQuery (index, Browser::parseQuery ("kick")), tree.root);
    require (view.rows.size() == 3, "all three matching folders show");

    int counted = 0;
    int unwalked = 0;
    for (const auto& row : view.rows)
    {
        if (row.matches == 1)
            ++counted;
        else if (row.matches == 0 && index.findFolder (row.file) < 0)
            ++unwalked;
    }
    require (counted == 1 && unwalked == 2, "the walked one has a count; the others show without one");
}

/** INTERSECT_BENCH_DIR=/path: time the index build and a few queries on a real library. */
void benchmark()
{
    const auto* dir = std::getenv ("INTERSECT_BENCH_DIR");
    if (dir == nullptr)
        return;

    // The bare walk, to tell disk time from indexing overhead.
    auto start = juce::Time::getMillisecondCounterHiRes();
    std::vector<juce::String> stack { juce::String (dir) };
    size_t walked = 0;
    while (! stack.empty())
    {
        const auto path = stack.back();
        stack.pop_back();
        for (const auto& entry : Browser::listDirectory (juce::File (path)))
        {
            ++walked;
            if (entry.isDirectory && ! entry.isSymlink && ! entry.name.startsWithChar ('.'))
                stack.push_back (path + "/" + entry.name);
        }
    }
    std::cout << "BENCH walk only: " << walked << " entries, "
              << juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - start) << " ms" << '\n';

    Browser::SearchIndex index;
    start = juce::Time::getMillisecondCounterHiRes();
    Browser::buildIndex (index, juce::File (dir), isAudioOrPreset, nullptr);
    const auto buildMs = juce::Time::getMillisecondCounterHiRes() - start;
    std::cout << "BENCH index " << dir << ": " << index.entries.size() << " entries, " << index.dirPaths.size()
              << " folders, " << juce::roundToInt (buildMs) << " ms" << (index.truncated ? " (truncated)" : "") << '\n';

    for (const auto* q : { "k", "kick", "808 kick", "\"808 kick\"", "zzzz" })
    {
        start = juce::Time::getMillisecondCounterHiRes();
        const auto filter = Browser::applyQuery (index, Browser::parseQuery (q));
        const auto queryMs = juce::Time::getMillisecondCounterHiRes() - start;
        start = juce::Time::getMillisecondCounterHiRes();
        const auto view = Browser::folderView (index, filter, juce::File (dir));
        const auto viewMs = juce::Time::getMillisecondCounterHiRes() - start;
        std::cout << "BENCH query " << q << ": " << view.totalMatches << " matches, apply "
                  << juce::String (queryMs, 1) << " ms, root view " << view.rows.size() << " rows in "
                  << juce::String (viewMs, 2) << " ms" << '\n';
    }
}
} // namespace

int main()
{
    int failed = 0;
    int passed = 0;
    const auto run = [&] (const char* name, auto test)
    {
        try
        {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

    run ("queries parse", queriesParse);
    run ("names match", namesMatch);
    run ("directories list with their type", directoriesList);
    run ("folder views filter", folderViewsFilter);
    run ("limits stop the walk", limitsStopTheWalk);
    run ("folders must lead to a sound", foldersMustLeadToASound);
    run ("folders past the limit still show", foldersPastTheLimitStillShow);
    benchmark();

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
