#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "DirectorySearch.h"
#include <functional>
#include <vector>

class SampleBrowserPanel : public juce::Component
{
public:
    SampleBrowserPanel();
    ~SampleBrowserPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    void setBookmarks (const juce::StringArray& paths);
    juce::StringArray getBookmarks() const;
    void refreshThemeColours();

    std::function<void (const std::vector<juce::File>&)> onFilesChosen;
    std::function<void()> onBookmarksChanged;

private:
    enum class LocationKind
    {
        section,
        drive,
        bookmark,
    };

    struct LocationRow
    {
        LocationKind kind = LocationKind::section;
        juce::String label;
        juce::File file;
        bool unavailable = false;
    };

    struct FileRow
    {
        juce::File file;
        bool directory = false;
        bool audio = false;
    };

    class LocationListModel : public juce::ListBoxModel
    {
    public:
        explicit LocationListModel (SampleBrowserPanel& ownerIn) : owner (ownerIn) {}
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    private:
        SampleBrowserPanel& owner;
    };

    class FileListModel : public juce::ListBoxModel
    {
    public:
        explicit FileListModel (SampleBrowserPanel& ownerIn) : owner (ownerIn) {}
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rowsToDescribe) override;
        juce::String getTooltipForRow (int row) override;
    private:
        SampleBrowserPanel& owner;
    };

    class PathDisplay : public juce::Component
    {
    public:
        explicit PathDisplay (SampleBrowserPanel& ownerIn) : owner (ownerIn) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;

    private:
        SampleBrowserPanel& owner;
    };

    // Outline-only button that draws a vector magnifier icon (Inter lacks a magnifier glyph),
    // delegating its background/toggle styling to IntersectLookAndFeel.
    class SearchToggleButton : public juce::Button
    {
    public:
        SearchToggleButton();
        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    };

    int getNumLocationRows() const;
    int getNumFileRows() const;
    void paintLocationRow (int row, juce::Graphics& g, int width, int height, bool selected);
    void paintFileRow (int row, juce::Graphics& g, int width, int height, bool selected);
    void locationRowClicked (int row, const juce::MouseEvent& e);
    void fileRowClicked (int row, const juce::MouseEvent& e);
    juce::String fileRowTooltip (int row) const;

    void rebuildLocations();
    void refreshFiles();
    void populateCurrentDirectoryFiles();
    void setCurrentDirectory (const juce::File& dir);
    void goUp();
    void activateFileRow (int row);
    void activateSelectedFilesOrRow (int row);
    void showLocationMenu (int row, juce::Point<int> position);
    void showFileMenu (int row, juce::Point<int> position);
    void addBookmark (const juce::File& dir);
    void removeBookmark (const juce::File& dir);
    bool hasBookmark (const juce::File& dir) const;
    std::vector<juce::File> getSelectedAudioFiles() const;
    juce::String makeDragDescriptionForRows (const juce::SparseSet<int>& rows) const;
    void beginPathEditing();
    void endPathEditing (bool resetTextToCurrentDirectory);
    void commitPathText();
    void flashPathError();
    void updateListThemeColours();
    void updateSplitterCursor (juce::Point<int> position);
    int getDefaultLocationSectionHeight (const juce::Rectangle<int>& area) const;
    int clampLocationSectionHeight (int height, const juce::Rectangle<int>& area) const;
    bool isSupportedAudioFile (const juce::File& file) const;
    juce::String locationPrefixForKind (LocationKind kind) const;
    juce::String formatFileMeta (const FileRow& row) const;

    void layoutPathSearchRow (juce::Rectangle<int> row);
    void setSearchInputMode (bool shouldSearch);
    void onSearchTextChanged();
    void launchSearch();
    void applySearchResults (const DirectorySearch::Result& result);
    void updateSearchStatusLabel();

    juce::TextButton backButton { juce::String::charToString (0x2190) };   // ←
    juce::TextButton forwardButton { juce::String::charToString (0x2192) }; // →
    juce::TextButton upButton { juce::String::charToString (0x2191) };      // ↑
    juce::TextButton refreshButton { juce::String::charToString (0x21BB) }; // ↻
    SearchToggleButton searchToggleButton;
    juce::Label titleLabel;
    PathDisplay pathDisplay { *this };
    juce::TextEditor pathEditor;
    juce::TextEditor searchEditor;
    juce::TextButton clearSearchButton { juce::String::charToString (0x00D7) }; // ×
    juce::Label searchStatusLabel;
    LocationListModel locationModel { *this };
    FileListModel fileModel { *this };
    juce::ListBox locationList { "browser locations", &locationModel };
    juce::ListBox fileList { "browser files", &fileModel };

    std::vector<LocationRow> locations;
    std::vector<FileRow> files;
    juce::StringArray bookmarkPaths;
    juce::File currentDirectory;
    juce::Array<juce::File> history;
    int historyIndex = -1;
    int pathErrorTicks = 0;
    int locationSectionHeight = 0;
    int splitterDragStartY = 0;
    int splitterDragStartHeight = 0;
    bool splitterDragging = false;
    bool splitterHover = false;
    bool pathEditorActive = false;
    juce::Rectangle<int> locationSectionBounds;
    juce::Rectangle<int> fileSectionBounds;
    juce::Rectangle<int> splitterBounds;
    juce::Rectangle<int> pathSearchRow;   // full path/search row, for re-layout on mode change

    // Recursive-search state. searchInputMode = the bar is a search field (vs path);
    // searchMode = the file list is showing search results (vs current-dir contents).
    juce::String searchQuery;
    bool searchInputMode = false;
    bool searchMode = false;
    bool searchScanning = false;
    bool searchTruncated = false;
    int debounceGeneration = 0;

    // Declared last so its worker thread is stopped before any member it references is torn down.
    DirectorySearch searcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleBrowserPanel)
};
