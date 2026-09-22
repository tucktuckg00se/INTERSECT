#include "PlacesList.h"
#include "BrowserIcons.h"
#include "BrowserTypes.h"
#include "../IntersectLookAndFeel.h"
#include "../../AppFiles.h"
#include <algorithm>

namespace
{
enum MenuIds
{
    kAddBookmark = 1,
    kRemoveBookmark,
    kRemoveRecent,
};

juce::String folderLabel (const juce::File& dir)
{
    const auto name = dir.getFileName();
    return name.isNotEmpty() ? name : dir.getFullPathName();
}

// Standard user folders are listed under LOCATIONS, so they are skipped under DRIVES.
std::vector<std::pair<juce::String, juce::File>> standardLocations()
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    std::vector<std::pair<juce::String, juce::File>> locations {
        { "Home", home },
        { "Desktop", juce::File::getSpecialLocation (juce::File::userDesktopDirectory) },
        { "Documents", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory) },
        { "Music", juce::File::getSpecialLocation (juce::File::userMusicDirectory) },
        { "Downloads", home.getChildFile ("Downloads") },
    };

    std::vector<std::pair<juce::String, juce::File>> existing;
    for (const auto& location : locations)
    {
        const bool duplicate = std::any_of (existing.begin(), existing.end(), [&location] (const auto& e)
        {
            return Browser::samePath (e.second, location.second);
        });
        if (location.second != juce::File() && location.second.isDirectory() && ! duplicate)
            existing.push_back (location);
    }
    return existing;
}
}

PlacesList::PlacesList()
{
    list.setRowHeight (20);
    list.setMultipleSelectionEnabled (false);
    list.setClickingTogglesRowSelection (false);
    list.setWantsKeyboardFocus (false);
    addAndMakeVisible (list);
    refreshThemeColours();
    rebuild();
}

void PlacesList::setBookmarks (const juce::StringArray& paths)
{
    bookmarkPaths.clear();
    for (auto path : paths)
    {
        path = path.trim();
        if (path.isNotEmpty())
            bookmarkPaths.addIfNotAlreadyThere (path);
    }
    rebuild();
}

bool PlacesList::hasBookmark (const juce::File& dir) const
{
    return bookmarkPaths.contains (dir.getFullPathName());
}

void PlacesList::addBookmark (const juce::File& dir)
{
    if (! dir.isDirectory() || hasBookmark (dir))
        return;

    bookmarkPaths.add (dir.getFullPathName());
    rebuild();
    if (onBookmarksChanged != nullptr)
        onBookmarksChanged();
}

void PlacesList::removeBookmark (const juce::File& dir)
{
    bookmarkPaths.removeString (dir.getFullPathName());
    rebuild();
    if (onBookmarksChanged != nullptr)
        onBookmarksChanged();
}

void PlacesList::setCustomPresetsFolder (const juce::File& folder)
{
    customPresetsFolder = folder;
    rebuild();
}

void PlacesList::setRecentFolders (const juce::StringArray& paths)
{
    recentPaths.clear();
    for (auto path : paths)
    {
        path = path.trim();
        if (path.isNotEmpty() && recentPaths.size() < kMaxRecentFolders)
            recentPaths.addIfNotAlreadyThere (path);
    }
    rebuild();
}

void PlacesList::noteRecentFolder (const juce::File& dir)
{
    if (! dir.isDirectory())
        return;

    const auto path = dir.getFullPathName();
    if (recentPaths.indexOf (path) == 0)
        return;

    recentPaths.removeString (path);
    recentPaths.insert (0, path);
    while (recentPaths.size() > kMaxRecentFolders)
        recentPaths.remove (recentPaths.size() - 1);

    rebuild();
    if (onRecentFoldersChanged != nullptr)
        onRecentFoldersChanged();
}

void PlacesList::removeRecentFolder (const juce::File& dir)
{
    recentPaths.removeString (dir.getFullPathName());
    rebuild();
    if (onRecentFoldersChanged != nullptr)
        onRecentFoldersChanged();
}

void PlacesList::setCurrentDirectory (const juce::File& dir)
{
    currentDirectory = dir;
    list.repaint();
}

void PlacesList::rebuild()
{
    rows.clear();

    auto addSection = [this] (const juce::String& label)
    {
        rows.push_back ({ Kind::section, label, {}, false });
    };

    auto addPlace = [this] (Kind kind, const juce::String& label, const juce::File& dir)
    {
        rows.push_back ({ kind, label, dir, ! dir.isDirectory() });
    };

    addSection ("Presets");
    // The default presets folder is created on first visit, so it never shows as unavailable.
    rows.push_back ({ Kind::presets, "Default", AppFiles::getPresetsDir(), false });
    if (customPresetsFolder != juce::File() && ! Browser::samePath (customPresetsFolder, AppFiles::getPresetsDir()))
        addPlace (Kind::presets, folderLabel (customPresetsFolder), customPresetsFolder);

    const auto locations = standardLocations();
    if (! locations.empty())
    {
        addSection ("Locations");
        for (const auto& location : locations)
            addPlace (Kind::location, location.first, location.second);
    }

    if (! bookmarkPaths.isEmpty())
    {
        addSection ("Bookmarks");
        for (const auto& path : bookmarkPaths)
            if (juce::File::isAbsolutePath (path))
                addPlace (Kind::bookmark, folderLabel (juce::File (path)), juce::File (path));
    }

    if (! recentPaths.isEmpty())
    {
        addSection ("Recent");
        for (const auto& path : recentPaths)
            if (juce::File::isAbsolutePath (path))
                addPlace (Kind::recent, folderLabel (juce::File (path)), juce::File (path));
    }

    addSection ("Drives");
    auto addDrive = [this, &locations] (const juce::String& label, const juce::File& root)
    {
        const bool isLocation = std::any_of (locations.begin(), locations.end(), [&root] (const auto& l)
        {
            return Browser::samePath (l.second, root);
        });
        const bool isListed = std::any_of (rows.begin(), rows.end(), [&root] (const Row& r)
        {
            return r.kind == Kind::drive && Browser::samePath (r.file, root);
        });
        if (root != juce::File() && ! isLocation && ! isListed)
            rows.push_back ({ Kind::drive, label, root, ! root.isDirectory() });
    };

    juce::StringArray rootNames, rootPaths;
    juce::FileBrowserComponent::getDefaultRoots (rootNames, rootPaths);
    for (int i = 0; i < rootPaths.size(); ++i)
    {
        const auto path = rootPaths[i].trim();
        if (! juce::File::isAbsolutePath (path))
            continue;

        const juce::File root (path);
        auto label = rootNames[i].trim();
        addDrive (label.isNotEmpty() ? label : root.getFullPathName(), root);
    }

   #if JUCE_LINUX
    const auto userName = juce::SystemStats::getLogonName();
    for (const auto& mountRoot : { juce::File ("/media").getChildFile (userName), juce::File ("/mnt") })
    {
        if (! mountRoot.isDirectory())
            continue;

        for (const auto& volume : mountRoot.findChildFiles (juce::File::findDirectories, false, "*",
                                                            juce::File::FollowSymlinks::no))
            addDrive (volume.getFileName(), volume);
    }
   #endif

    list.updateContent();
    list.repaint();
}

int PlacesList::getNumRows()
{
    return (int) rows.size();
}

void PlacesList::paintListBoxItem (int rowIndex, juce::Graphics& g, int width, int height, bool)
{
    if (rowIndex < 0 || rowIndex >= (int) rows.size())
        return;

    const auto& row = rows[(size_t) rowIndex];
    auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (4, 1);

    if (row.kind == Kind::section)
    {
        g.setColour (getTheme().text0.withAlpha (0.85f));
        g.setFont (IntersectLookAndFeel::makeFont (8.0f, true));
        g.drawText (row.label.toUpperCase(), bounds.reduced (4, 0).withTrimmedTop (4),
                    juce::Justification::centredLeft, true);
        return;
    }

    const bool active = Browser::samePath (row.file, currentDirectory);
    if (active)
    {
        g.setColour (getTheme().surface3.interpolatedWith (getTheme().accent, 0.14f));
        g.fillRoundedRectangle (bounds.toFloat(), 3.0f);
    }

    const auto textColour = row.unavailable ? getTheme().text0.withAlpha (0.5f)
                          : active          ? getTheme().text2
                                            : getTheme().text2.withAlpha (0.82f);

    BrowserIcons::Icon icon = BrowserIcons::Icon::folder;
    auto iconColour = getTheme().text1;
    switch (row.kind)
    {
        case Kind::presets:  icon = BrowserIcons::Icon::preset;   iconColour = getTheme().accent; break;
        case Kind::location: icon = row.label == "Home" ? BrowserIcons::Icon::home : BrowserIcons::Icon::folder;
                             iconColour = getTheme().color1.brighter (0.3f); break;
        case Kind::bookmark: icon = BrowserIcons::Icon::bookmark; iconColour = getTheme().color2.brighter (0.2f); break;
        case Kind::recent:   icon = BrowserIcons::Icon::recent;   iconColour = getTheme().text1; break;
        case Kind::drive:    icon = BrowserIcons::Icon::drive;    iconColour = getTheme().text1; break;
        case Kind::section:  break;
    }

    auto content = bounds.reduced (6, 0);
    BrowserIcons::draw (g, icon, content.removeFromLeft (12).toFloat(),
                        row.unavailable ? iconColour.withAlpha (0.4f) : iconColour);
    content.removeFromLeft (7);
    g.setColour (textColour);
    g.setFont (IntersectLookAndFeel::makeFont (9.5f));
    g.drawText (row.label, content, juce::Justification::centredLeft, true);
}

void PlacesList::listBoxItemClicked (int rowIndex, const juce::MouseEvent& e)
{
    if (rowIndex < 0 || rowIndex >= (int) rows.size())
        return;

    const auto row = rows[(size_t) rowIndex];
    if (row.kind == Kind::section)
        return;

    if (e.mods.isPopupMenu())
    {
        showMenu (row, e.getScreenPosition());
        return;
    }

    if (row.kind == Kind::presets && Browser::samePath (row.file, AppFiles::getPresetsDir()))
        (void) row.file.createDirectory();

    if (row.file.isDirectory() && onPlaceChosen != nullptr)
        onPlaceChosen (row.file);
}

void PlacesList::showMenu (const Row& row, juce::Point<int> screenPos)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    if (row.kind == Kind::bookmark)
        menu.addItem (kRemoveBookmark, "Remove Bookmark");
    else if (row.file.isDirectory() && ! hasBookmark (row.file))
        menu.addItem (kAddBookmark, "Add Bookmark");

    if (row.kind == Kind::recent)
        menu.addItem (kRemoveRecent, "Remove from Recent");

    if (menu.getNumItems() == 0)
        return;

    menu.showMenuAsync (IntersectLookAndFeel::makeEditorMenuOptions (*this)
                            .withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)),
        [safe = juce::Component::SafePointer<PlacesList> (this), file = row.file] (int result)
        {
            if (safe == nullptr)
                return;

            if (result == kAddBookmark)
                safe->addBookmark (file);
            else if (result == kRemoveBookmark)
                safe->removeBookmark (file);
            else if (result == kRemoveRecent)
                safe->removeRecentFolder (file);
        });
}

void PlacesList::refreshThemeColours()
{
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    for (auto* bar : { &list.getVerticalScrollBar(), &list.getHorizontalScrollBar() })
    {
        bar->setColour (juce::ScrollBar::thumbColourId, getTheme().accent.withAlpha (0.6f));
        bar->setColour (juce::ScrollBar::trackColourId, getTheme().surface4.withAlpha (0.45f));
        bar->setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    }
    list.repaint();
    repaint();
}

void PlacesList::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().surface0);
}

void PlacesList::resized()
{
    list.setBounds (getLocalBounds().reduced (2, 4));
}
