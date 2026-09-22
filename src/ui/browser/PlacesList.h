#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

/**
    Browser sidebar: PRESETS (default + custom folder), LOCATIONS (standard user folders),
    BOOKMARKS, RECENT and DRIVES. Owns the bookmark, recent-folder and custom-presets lists;
    the owner persists them when the change callbacks fire.
*/
class PlacesList : public juce::Component,
                   private juce::ListBoxModel
{
public:
    PlacesList();

    void setBookmarks (const juce::StringArray& paths);
    juce::StringArray getBookmarks() const { return bookmarkPaths; }
    bool hasBookmark (const juce::File& dir) const;
    void addBookmark (const juce::File& dir);

    void setCustomPresetsFolder (const juce::File& folder);
    juce::File getCustomPresetsFolder() const { return customPresetsFolder; }

    void setRecentFolders (const juce::StringArray& paths);
    juce::StringArray getRecentFolders() const { return recentPaths; }
    /** Moves the folder to the top of RECENT (most recent first, capped). */
    void noteRecentFolder (const juce::File& dir);

    /** Highlights the place matching the folder being browsed. */
    void setCurrentDirectory (const juce::File& dir);
    void rebuild();
    void refreshThemeColours();

    void paint (juce::Graphics& g) override;
    void resized() override;

    std::function<void (const juce::File&)> onPlaceChosen;
    std::function<void()> onBookmarksChanged;
    std::function<void()> onRecentFoldersChanged;

    static constexpr int kMaxRecentFolders = 8;

private:
    enum class Kind
    {
        section,
        presets,
        location,
        bookmark,
        recent,
        drive,
    };

    struct Row
    {
        Kind kind = Kind::section;
        juce::String label;
        juce::File file;
        bool unavailable = false;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;

    void showMenu (const Row& row, juce::Point<int> screenPos);
    void removeBookmark (const juce::File& dir);
    void removeRecentFolder (const juce::File& dir);

    juce::ListBox list { "browser places", this };
    std::vector<Row> rows;
    juce::StringArray bookmarkPaths;
    juce::StringArray recentPaths;
    juce::File customPresetsFolder;
    juce::File currentDirectory;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlacesList)
};
