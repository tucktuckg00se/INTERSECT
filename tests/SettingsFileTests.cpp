#include "SettingsFile.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

const char* kSample =
    "uiScale: 1.50\n"
    "theme: oc\n"
    "sampleBrowserBookmarks:\n"
    "  - /home/you/samples/breaks\n"
    "  - C:\\Users\\you\\Music\n"
    "browserLastFolder: C:\\Users\\you\\Music\n"
    "auditionAutoPlay: true\n";

void parseReadsScalarsListsAndPathsWithColons()
{
    const auto values = SettingsValues::parse (kSample);
    require (values.getString ("uiScale") == "1.50", "scalar");
    require (values.getString ("browserLastFolder") == "C:\\Users\\you\\Music", "value keeps its colon");
    const auto bookmarks = values.getList ("sampleBrowserBookmarks");
    require (bookmarks.size() == 2 && bookmarks[1] == "C:\\Users\\you\\Music", "list items");
    require (values.getString ("sampleBrowserBookmarks").isEmpty(), "a list is not a scalar");
    require (values.getString ("missing", "fallback") == "fallback", "fallback");
}

void serializeRoundTrips()
{
    const auto values = SettingsValues::parse (kSample);
    require (SettingsValues::parse (values.serialize()) == values, "parse(serialize(x)) == x");
    require (values.serialize() == juce::String (kSample), "same text for a file we wrote");
}

void listFollowedByKeyEndsTheList()
{
    const auto values = SettingsValues::parse ("a:\n  - one\nb: two\n");
    require (values.getList ("a") == juce::StringArray ("one"), "list a");
    require (values.getString ("b") == "two", "key after the list");
}

void mergeKeepsOtherEditorsChanges()
{
    SettingsValues base, mine, theirs;
    base.set ("theme", "dark");     base.set ("nrpnChannel", "0");     base.set ("browserLastFolder", "/a");
    mine = base;
    theirs = base;

    mine.set ("nrpnChannel", "5");               // changed here
    theirs.set ("theme", "light");               // changed by another instance
    theirs.set ("browserLastFolder", "/b");      // another instance browsed elsewhere
    theirs.set ("newKey", "x");                  // only the other side has it

    const auto merged = SettingsValues::merge (base, mine, theirs);
    require (merged.getString ("nrpnChannel") == "5", "our change is written");
    require (merged.getString ("theme") == "light", "unchanged key takes the file's value");
    require (merged.getString ("browserLastFolder") == "/b", "their last folder survives");
    require (merged.getString ("newKey") == "x", "their extra key is kept");
}

void mergeHandlesRemovalsAndLists()
{
    SettingsValues base;
    base.set ("presetFolder", "/presets");
    base.setList ("bookmarks", { "/one" });

    auto mine = base;
    mine.remove ("presetFolder");                         // cleared here
    auto theirs = base;
    theirs.setList ("bookmarks", { "/one", "/two" });     // bookmark added elsewhere

    auto merged = SettingsValues::merge (base, mine, theirs);
    require (! merged.contains ("presetFolder"), "our removal is written");
    require (merged.getList ("bookmarks").size() == 2, "their list wins when ours is unchanged");

    // Both sides changed the same list: this editor's save wins.
    mine.setList ("bookmarks", { "/one", "/three" });
    merged = SettingsValues::merge (base, mine, theirs);
    require (merged.getList ("bookmarks") == juce::StringArray ("/one", "/three"), "our list change wins");

    // Another instance removed a key we never touched: it stays removed.
    auto theirsRemoved = base;
    theirsRemoved.remove ("presetFolder");
    merged = SettingsValues::merge (base, base, theirsRemoved);
    require (! merged.contains ("presetFolder"), "their removal is kept");
}

void twoEditorsSharingOneFile()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("intersect-settings-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    require (dir.createDirectory().wasOk(), "temp dir");
    const auto path = dir.getChildFile ("settings.yaml");
    require (path.replaceWithText ("theme: dark\nsampleBrowserBookmarks:\n  - /one\nbrowserLastFolder: /start\n"), "seed");

    SettingsFile a (path), b (path);
    auto aValues = a.load();
    auto bValues = b.load();

    // A adds a bookmark and browses; B (still holding the old copy) changes the theme.
    aValues.setList ("sampleBrowserBookmarks", { "/one", "/two" });
    aValues.set ("browserLastFolder", "/a-folder");
    require (a.save (aValues), "A saves");
    bValues.set ("theme", "light");
    require (b.save (bValues), "B saves");

    auto onDisk = SettingsValues::parse (path.loadFileAsString());
    require (onDisk.getString ("theme") == "light", "B's theme");
    require (onDisk.getList ("sampleBrowserBookmarks").size() == 2, "A's bookmark survived B's save");
    require (onDisk.getString ("browserLastFolder") == "/a-folder", "A's last folder survived B's save");
    require (b.readValue ("browserLastFolder") == "/a-folder", "B can read the shared last folder");

    // B closes (saves its unchanged copy again): nothing of A's is lost.
    require (b.save (bValues), "B saves again");
    onDisk = SettingsValues::parse (path.loadFileAsString());
    require (onDisk.getString ("browserLastFolder") == "/a-folder", "closing B keeps A's folder");

    dir.deleteRecursively();
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

    run ("parse scalars, lists, paths with colons", parseReadsScalarsListsAndPathsWithColons);
    run ("serialize round trip", serializeRoundTrips);
    run ("a key after a list ends it", listFollowedByKeyEndsTheList);
    run ("merge keeps other editors' changes", mergeKeepsOtherEditorsChanges);
    run ("merge removals and lists", mergeHandlesRemovalsAndLists);
    run ("two editors sharing one file", twoEditorsSharingOneFile);

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
