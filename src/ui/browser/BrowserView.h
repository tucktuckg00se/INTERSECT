#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BrowserTypes.h"
#include "Breadcrumb.h"
#include "FileInfoCache.h"
#include "FileTable.h"
#include "IconButton.h"
#include "PlacesList.h"
#include "PreviewPane.h"
#include "../DirectorySearch.h"
#include <functional>
#include <vector>

/**
    The full-view file browser that replaces the editor area below the sample lane.

    Places sidebar | toolbar (navigation, breadcrumb, search, OPEN, SAVE) over the file table |
    preview pane. Owns navigation history, search, and the keyboard model; everything that
    changes the kit or plays audio is reported through callbacks so the editor stays in charge.
*/
class BrowserView : public juce::Component
{
public:
    BrowserView();
    ~BrowserView() override;

    // ---- Persisted state (the editor saves and restores these) ----
    void setBookmarks (const juce::StringArray& paths)      { places.setBookmarks (paths); }
    juce::StringArray getBookmarks() const                  { return places.getBookmarks(); }
    void setRecentFolders (const juce::StringArray& paths)  { places.setRecentFolders (paths); }
    juce::StringArray getRecentFolders() const              { return places.getRecentFolders(); }
    void setCustomPresetsFolder (const juce::File& folder)  { places.setCustomPresetsFolder (folder); }
    juce::File getCustomPresetsFolder() const               { return places.getCustomPresetsFolder(); }
    void setAutoPlay (bool shouldAutoPlay)                  { preview.setAutoPlay (shouldAutoPlay); }
    bool getAutoPlay() const noexcept                       { return preview.getAutoPlay(); }
    void setAuditionGainDb (float db)                       { preview.setGainDb (db); }
    float getAuditionGainDb() const noexcept                { return preview.getGainDb(); }

    /** Starts browsing at dir (falls back to home if it no longer exists) with a fresh history. */
    void setStartFolder (const juce::File& dir);
    juce::File getCurrentFolder() const noexcept { return currentDirectory; }

    /** Navigates to the file's folder and selects it without starting a preview. */
    void revealFile (const juce::File& file);
    /** Starts inline renaming of a preset in the current listing (see FileTable::beginRename). */
    void beginRename (const juce::File& presetFile);
    /** Where SAVE puts a new preset: the folder being browsed when it is inside the default or
        custom presets folder (so subfolders work), otherwise the default presets folder. */
    juce::File getPresetSaveFolder() const;
    void setAuditionStatus (const AuditionStatus& status);
    /** Gives the file list keyboard focus; call when the browser opens. */
    void focusFileList();
    void refreshThemeColours();

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void (const std::vector<juce::File>&)> onAddFiles;
    std::function<void (const std::vector<juce::File>&)> onLoadFiles;
    std::function<void (const juce::File&)> onPresetChosen;
    /** "Open Files..." from the open button: pick audio / presets with the system dialog. */
    std::function<void()> onOpenDialogRequested;
    /** Right-click Export... / Export with Samples... on a preset. */
    std::function<void (const juce::File& preset, bool embedSamples)> onExportRequested;
    std::function<void()> onCloseRequested;
    /** Bookmarks, recent folders, AUTO or volume changed and should be saved. */
    std::function<void()> onPersistentStateChanged;
    std::function<void (const juce::File&)> onAuditionRequested;
    std::function<void()> onAuditionStopRequested;
    std::function<void()> onAuditionPauseRequested;
    std::function<void()> onAuditionResumeRequested;
    std::function<void (float)> onAuditionGainChanged;

private:
    void navigateTo (const juce::File& dir);
    void showDirectory (const juce::File& dir);
    void goBack();
    void goForward();
    void goUp();
    void refresh();
    void listCurrentDirectory();
    void updateNavButtons();

    void onSelectionChanged();
    void scheduleAudition (const juce::File& file);
    /** Space / PLAY-PAUSE: pause what is playing, resume what is paused, otherwise play from the start. */
    void togglePlayback();
    void retrigger (const juce::File& file);
    void stopAuditionIfActive();

    void activateRow (const Browser::FileRow& row, bool shiftDown);
    std::vector<juce::File> audioFilesFor (const Browser::FileRow* activated) const;
    void addFiles (const std::vector<juce::File>& files);
    void loadFiles (const std::vector<juce::File>& files);
    void showRowMenu (const Browser::FileRow& row, juce::Point<int> screenPos);
    void showOpenMenu();
    void openFolderDialog();
    bool canRenamePreset (const juce::File& preset, const juce::String& newName);
    void renamePreset (const juce::File& preset, const juce::String& newName);
    void showTemporaryStatus (const juce::String& text);

    void onSearchTextChanged();
    void launchSearch();
    void applySearchResults (const DirectorySearch::Result& result);
    void clearSearch();
    void updateSearchStatus();

    FileInfoCache infoCache;
    PlacesList places;
    FileTable table { infoCache };
    PreviewPane preview { infoCache };
    Breadcrumb breadcrumb;

    juce::TextButton backButton { juce::String::charToString (0x2190) };    // left arrow
    juce::TextButton forwardButton { juce::String::charToString (0x2192) }; // right arrow
    juce::TextButton upButton { juce::String::charToString (0x2191) };      // up arrow
    juce::TextButton refreshButton { juce::String::charToString (0x21BB) }; // clockwise arrow
    IconButton openButton { "open", BrowserIcons::Icon::folderOpen };
    std::unique_ptr<juce::FileChooser> folderChooser;
    int statusMessageId = 0;
    juce::TextButton clearSearchButton { juce::String::charToString (0x00D7) }; // multiplication sign
    juce::TextEditor searchEditor;

    juce::File currentDirectory;
    juce::Array<juce::File> history;
    int historyIndex = -1;

    juce::File lastFocusedFile;
    int auditionRequestId = 0;
    AuditionStatus audition;

    juce::String searchQuery;
    bool searchMode = false;
    bool searchScanning = false;
    bool searchTruncated = false;
    int searchDebounceId = 0;

    // Declared last so its worker thread stops before anything it calls back into is destroyed.
    DirectorySearch searcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserView)
};
