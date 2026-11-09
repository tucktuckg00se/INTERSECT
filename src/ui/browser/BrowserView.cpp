#include "BrowserView.h"
#include "../IntersectLookAndFeel.h"
#include "../FullViewBar.h"
#include "DirectoryListing.h"
#include "../../AppFiles.h"
#include <algorithm>

namespace
{
constexpr int kPlacesW = 168;
constexpr int kPreviewW = 226;
constexpr int kToolbarRowH = 20;
constexpr int kAuditionDelayMs = 150;
constexpr int kSearchDelayMs = 100;   // refining filters the in-memory index, so this can be short

enum MenuIds
{
    kMenuAdd = 1,
    kMenuLoad,
    kMenuLoadPreset,
    kMenuOpenFolder,
    kMenuAddBookmark,
    kMenuReveal,
    kMenuRename,
    kMenuExport,
    kMenuExportWithSamples,
    kMenuOpenFiles,
    kMenuOpenFolderDialog,
    kMenuSetDefaultFolder,
};

// Kept ASCII: non-ASCII punctuation mis-renders on some toolchains.
const juce::String kSearchPlaceholder = "Search this folder and subfolders...";
}

BrowserView::BrowserView()
{
    setWantsKeyboardFocus (true);

    addAndMakeVisible (places);
    addAndMakeVisible (table);
    addAndMakeVisible (preview);
    addAndMakeVisible (breadcrumb);

    for (auto* button : std::initializer_list<juce::Button*> { &backButton, &forwardButton, &upButton, &refreshButton,
                                                               &openButton, &clearSearchButton, &relinkCancelButton })
    {
        button->getProperties().set (IntersectLookAndFeel::outlineOnlyButtonProperty, true);
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (*button);
    }
    clearSearchButton.setVisible (false);

    // In the bottom bar, like CLOSE in the stem and settings views.
    closeButton.setTooltip ("Close the browser (Esc)");
    closeButton.onClick = [this]
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
    };
    addAndMakeVisible (closeButton);

    backButton.setTooltip ("Back");
    forwardButton.setTooltip ("Forward");
    upButton.setTooltip ("Up one folder (Backspace)");
    refreshButton.setTooltip ("Refresh");
    openButton.setTooltip ("Open files or a folder");
    clearSearchButton.setTooltip ("Clear search (Esc)");

    backButton.onClick = [this] { goBack(); };
    forwardButton.onClick = [this] { goForward(); };
    upButton.onClick = [this] { goUp(); };
    refreshButton.onClick = [this] { refresh(); };
    openButton.onClick = [this] { showOpenMenu(); };
    clearSearchButton.onClick = [this] { clearSearch(); };
    relinkCancelButton.onClick = [this] { endRelink(); };
    relinkCancelButton.setTooltip ("Stop relinking (Esc)");
    addChildComponent (relinkCancelButton);

    searchEditor.setJustification (juce::Justification::centredLeft);
    searchEditor.setIndents (6, 0);
    searchEditor.setMultiLine (false);
    searchEditor.setReturnKeyStartsNewLine (false);
    searchEditor.setScrollbarsShown (false);
    searchEditor.setFont (IntersectLookAndFeel::makeFont (9.0f));
    searchEditor.onTextChange = [this] { onSearchTextChanged(); };
    searchEditor.onEscapeKey = [this]
    {
        if (searchEditor.getText().isNotEmpty())
            clearSearch();
        else
            focusFileList();
    };
    searchEditor.onReturnKey = [this]
    {
        table.selectFirstRow();
        focusFileList();
    };
    addAndMakeVisible (searchEditor);

    breadcrumb.onNavigate = [this] (const juce::File& dir) { navigateTo (dir); };

    places.onPlaceChosen = [this] (const juce::File& dir) { navigateTo (dir); };
    places.onBookmarksChanged = [this]
    {
        if (onPersistentStateChanged != nullptr)
            onPersistentStateChanged();
    };
    places.onRecentFoldersChanged = places.onBookmarksChanged;
    places.isDefaultFolder = [this] (const juce::File& dir) { return isDefaultFolder (dir); };
    places.onSetDefaultFolder = [this] (const juce::File& dir) { useAsDefaultFolder (dir); };

    table.onSelectionChanged = [this] { onSelectionChanged(); };
    table.onRowActivated = [this] (const Browser::FileRow& row, bool shiftDown) { activateRow (row, shiftDown); };
    table.onRowMenu = [this] (const Browser::FileRow& row, juce::Point<int> pos) { showRowMenu (row, pos); };
    table.onGoUp = [this] { goUp(); };
    table.onRetrigger = [this] (const Browser::FileRow& row) { retrigger (row.file); };
    table.canRename = [this] (const juce::File& file, const juce::String& name) { return canRenamePreset (file, name); };
    table.onRenameRequested = [this] (const juce::File& file, const juce::String& name) { renamePreset (file, name); };

    preview.onAdd = [this] { addFiles (audioFilesFor (table.getFocusedRow())); };
    preview.onLoad = [this] { loadFiles (audioFilesFor (table.getFocusedRow())); };
    preview.onLoadPreset = [this]
    {
        if (const auto* row = table.getFocusedRow())
            activateRow (*row, false);
    };
    preview.onOpenFolder = preview.onLoadPreset;
    preview.onPlayToggle = [this] { togglePlayback(); };
    preview.onAutoPlayChanged = [this] (bool autoPlay)
    {
        if (! autoPlay)
            stopAuditionIfActive();
        else if (const auto* row = table.getFocusedRow(); row != nullptr && row->kind == Browser::FileKind::audio)
            scheduleAudition (row->file);

        if (onPersistentStateChanged != nullptr)
            onPersistentStateChanged();
    };
    preview.onGainChanged = [this] (float db)
    {
        if (onAuditionGainChanged != nullptr)
            onAuditionGainChanged (db);
        if (onPersistentStateChanged != nullptr)
            onPersistentStateChanged();
    };

    infoCache.onInfoReady = [this]
    {
        table.infoUpdated();
        preview.infoUpdated();
    };

    searcher.isListedFile = [] (const juce::File& f)
    {
        return Browser::classifyByName (f) != Browser::FileKind::other;
    };
    searcher.onComplete = [this] (const DirectorySearch::Result& result) { applySearchResults (result); };

    refreshThemeColours();
    setStartFolder (juce::File::getSpecialLocation (juce::File::userHomeDirectory));
}

BrowserView::~BrowserView()
{
    infoCache.onInfoReady = nullptr;
    searcher.onComplete = nullptr;
}

//==============================================================================
// Navigation

void BrowserView::setStartFolder (const juce::File& dir)
{
    const auto start = dir.isDirectory() ? dir : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    history.clear();
    historyIndex = -1;
    navigateTo (start);
}

void BrowserView::applyDefaultFolder (const juce::File& lastUsedFolder)
{
    if (searchMode)
    {
        endSearch();   // FILES opens on a plain folder
        listCurrentDirectory();
    }

    auto target = lastUsedFolder;
    if (defaultFolderMode == Browser::DefaultFolderMode::chosenFolder && defaultFolder != juce::File())
    {
        if (defaultFolder.isDirectory())
            target = defaultFolder;
        else
            showTemporaryStatus ("Default folder not found: " + defaultFolder.getFullPathName());
    }

    if (target.isDirectory() && ! Browser::samePath (target, currentDirectory))
        navigateTo (target);
}

bool BrowserView::isDefaultFolder (const juce::File& dir) const
{
    return defaultFolderMode == Browser::DefaultFolderMode::chosenFolder && Browser::samePath (dir, defaultFolder);
}

void BrowserView::useAsDefaultFolder (const juce::File& dir)
{
    if (! dir.isDirectory())
        return;

    defaultFolder = dir;
    defaultFolderMode = Browser::DefaultFolderMode::chosenFolder;
    showTemporaryStatus ("FILES will open in " + dir.getFullPathName());
    if (onPersistentStateChanged != nullptr)
        onPersistentStateChanged();
}

void BrowserView::navigateTo (const juce::File& dir)
{
    if (! dir.isDirectory())
        return;

    while (history.size() - 1 > historyIndex)
        history.removeLast();
    history.add (dir);
    historyIndex = history.size() - 1;
    showDirectory (dir);
}

void BrowserView::showDirectory (const juce::File& dir)
{
    stopAuditionIfActive();
    const bool folderChanged = ! Browser::samePath (dir, currentDirectory);
    currentDirectory = dir;
    breadcrumb.setDirectory (dir);
    places.setCurrentDirectory (dir);
    updateNavButtons();

    // A search stays on while browsing: this folder shows what matches inside it.
    if (searchMode)
    {
        table.setRows ({});   // the previous folder's matches don't belong here
        launchSearch();
    }
    else
    {
        listCurrentDirectory();
    }

    if (folderChanged && onFolderChanged != nullptr)
        onFolderChanged();
}

void BrowserView::listCurrentDirectory()
{
    std::vector<Browser::FileRow> rows;
    if (currentDirectory.isDirectory())
    {
        // Typed from the directory listing itself: no per-file disk lookups (see DirectoryListing).
        const auto folder = currentDirectory.getFullPathName();
        const auto prefix = folder.endsWith (juce::File::getSeparatorString()) ? folder
                                                                                : folder + juce::File::getSeparatorString();
        for (const auto& entry : Browser::listDirectory (currentDirectory))
        {
            if (entry.name.startsWithChar ('.'))
                continue;   // hidden files and folders

            const auto child = juce::File::createFileWithoutCheckingPath (prefix + entry.name);
            const auto kind = entry.isDirectory ? Browser::FileKind::directory : Browser::classifyByName (child);
            if (kind != Browser::FileKind::other)
                rows.push_back ({ child, kind });
        }
    }

    infoCache.cancelPending();
    lastFocusedFile = juce::File();
    table.setFullPathTooltips (false);
    table.setRows (std::move (rows));
    updateSearchStatus();
    onSelectionChanged();
}

void BrowserView::goBack()
{
    if (historyIndex > 0)
        showDirectory (history[--historyIndex]);
}

void BrowserView::goForward()
{
    if (historyIndex + 1 < history.size())
        showDirectory (history[++historyIndex]);
}

void BrowserView::goUp()
{
    const auto previous = currentDirectory;
    const auto parent = currentDirectory.getParentDirectory();
    if (parent == currentDirectory || ! parent.isDirectory())
        return;

    navigateTo (parent);
    // Land on the folder we came from, like most file managers.
    lastFocusedFile = previous;
    table.selectFile (previous);
}

void BrowserView::refresh()
{
    infoCache.clear();
    if (searchMode)
        launchSearch (true);   // re-read the disk, not just the search index
    else
        listCurrentDirectory();
}

void BrowserView::updateNavButtons()
{
    backButton.setEnabled (historyIndex > 0);
    forwardButton.setEnabled (historyIndex + 1 < history.size());
    upButton.setEnabled (currentDirectory.getParentDirectory() != currentDirectory);
}

void BrowserView::revealFile (const juce::File& file)
{
    const auto dir = file.getParentDirectory();
    if (! dir.isDirectory())
        return;

    if (searchMode)
        endSearch();   // the file must be visible, whether it matches or not

    if (Browser::samePath (dir, currentDirectory))
        listCurrentDirectory();
    else
        navigateTo (dir);

    lastFocusedFile = file;   // selecting it below must not start a preview
    table.selectFile (file);
}

void BrowserView::beginRelink (int sampleId, const juce::String& fileName, const juce::File& originalFile)
{
    if (searchMode)
    {
        endSearch();
        listCurrentDirectory();
    }

    relinkSampleId = sampleId;
    relinkFileName = fileName;
    preview.setRelinkMode (true);
    relinkCancelButton.setVisible (true);
    resized();
    repaint();

    // Start where the file used to be, or as close to it as still exists.
    auto dir = originalFile.getParentDirectory();
    while (dir != juce::File() && ! dir.isDirectory() && dir.getParentDirectory() != dir)
        dir = dir.getParentDirectory();

    if (dir.isDirectory())
    {
        const auto sameName = dir.getChildFile (fileName);
        if (sameName.existsAsFile())
            revealFile (sameName);
        else if (! Browser::samePath (dir, currentDirectory))
            navigateTo (dir);
    }
    focusFileList();
}

void BrowserView::endRelink()
{
    if (relinkSampleId < 0)
        return;

    relinkSampleId = -1;
    relinkFileName.clear();
    preview.setRelinkMode (false);
    relinkCancelButton.setVisible (false);
    resized();
    repaint();
}

void BrowserView::relinkWith (const std::vector<juce::File>& files)
{
    // Prefer the focused row when it is one of several selected files.
    auto file = files.front();
    if (const auto* row = table.getFocusedRow(); row != nullptr && row->kind == Browser::FileKind::audio)
        file = row->file;

    ++auditionRequestId;
    places.noteRecentFolder (file.getParentDirectory());
    if (onRelinkFile != nullptr)
        onRelinkFile (relinkSampleId, file);
}

void BrowserView::focusFileList()
{
    table.grabListFocus();
}

//==============================================================================
// Selection, audition

void BrowserView::onSelectionChanged()
{
    const auto* focused = table.getFocusedRow();
    preview.setSelection (table.getSelectedRows(), focused);
    updateStatusBar();

    const auto focusedFile = focused != nullptr ? focused->file : juce::File();
    if (Browser::samePath (focusedFile, lastFocusedFile))
        return;   // e.g. a re-sort re-selected the same row

    lastFocusedFile = focusedFile;
    if (focused != nullptr && focused->kind == Browser::FileKind::audio && preview.getAutoPlay())
        scheduleAudition (focused->file);
    else
        stopAuditionIfActive();
}

void BrowserView::scheduleAudition (const juce::File& file)
{
    const int requestId = ++auditionRequestId;
    juce::Timer::callAfterDelay (kAuditionDelayMs, [safe = juce::Component::SafePointer<BrowserView> (this), requestId, file]
    {
        if (safe == nullptr || safe->auditionRequestId != requestId || ! safe->isShowing())
            return;
        if (Browser::samePath (safe->lastFocusedFile, file) && safe->onAuditionRequested != nullptr)
            safe->onAuditionRequested (file);
    });
}

void BrowserView::togglePlayback()
{
    const auto* focused = table.getFocusedRow();
    if (focused == nullptr || focused->kind != Browser::FileKind::audio)
        return;

    ++auditionRequestId;   // an explicit choice overrides a pending auto-play
    const bool sameFile = Browser::samePath (audition.file, focused->file);

    // The status only refreshes on the editor's timer, so update it here too; otherwise two quick
    // presses would both see the old state.
    if (sameFile && audition.state == AuditionStatus::State::playing)
    {
        audition.state = AuditionStatus::State::paused;
        if (onAuditionPauseRequested != nullptr)
            onAuditionPauseRequested();
    }
    else if (sameFile && audition.state == AuditionStatus::State::decoding)
    {
        audition.state = AuditionStatus::State::stopped;
        if (onAuditionStopRequested != nullptr)
            onAuditionStopRequested();
    }
    else if (sameFile && audition.state == AuditionStatus::State::paused)
    {
        audition.state = AuditionStatus::State::playing;
        if (onAuditionResumeRequested != nullptr)
            onAuditionResumeRequested();
    }
    else
    {
        retrigger (focused->file);
        return;
    }

    preview.setAuditionStatus (audition);
}

void BrowserView::retrigger (const juce::File& file)
{
    ++auditionRequestId;
    lastFocusedFile = file;
    audition.file = file;
    audition.state = AuditionStatus::State::playing;
    if (onAuditionRequested != nullptr)
        onAuditionRequested (file);
    preview.setAuditionStatus (audition);
}

void BrowserView::stopAuditionIfActive()
{
    ++auditionRequestId;   // cancels a pending auto-play
    const bool active = audition.state == AuditionStatus::State::playing
                     || audition.state == AuditionStatus::State::decoding;
    if (active && onAuditionStopRequested != nullptr)
        onAuditionStopRequested();
}

void BrowserView::setAuditionStatus (const AuditionStatus& status)
{
    audition = status;
    preview.setAuditionStatus (status);
}

//==============================================================================
// Actions

std::vector<juce::File> BrowserView::audioFilesFor (const Browser::FileRow* activated) const
{
    // Act on the whole selection when the activated row is part of it, otherwise on that row alone.
    std::vector<juce::File> files;
    const auto selected = table.getSelectedRows();
    const bool activatedIsSelected = activated != nullptr
        && std::any_of (selected.begin(), selected.end(), [activated] (const Browser::FileRow& row)
           {
               return Browser::samePath (row.file, activated->file);
           });

    if (activated != nullptr && ! activatedIsSelected)
    {
        if (activated->kind == Browser::FileKind::audio)
            files.push_back (activated->file);
        return files;
    }

    for (const auto& row : selected)
        if (row.kind == Browser::FileKind::audio && row.file.existsAsFile())
            files.push_back (row.file);
    return files;
}

void BrowserView::activateRow (const Browser::FileRow& row, bool shiftDown)
{
    switch (row.kind)
    {
        case Browser::FileKind::directory:
            navigateTo (row.file);
            break;

        case Browser::FileKind::preset:
            if (isRelinking())
                showTemporaryStatus ("Pick an audio file to relink " + relinkFileName);
            else if (row.file.existsAsFile() && onPresetChosen != nullptr)
            {
                places.noteRecentFolder (row.file.getParentDirectory());
                onPresetChosen (row.file);
            }
            break;

        case Browser::FileKind::audio:
        {
            const auto files = audioFilesFor (&row);
            if (shiftDown)
                loadFiles (files);
            else
                addFiles (files);
            break;
        }

        case Browser::FileKind::other:
            break;
    }
}

void BrowserView::addFiles (const std::vector<juce::File>& files)
{
    if (files.empty())
        return;

    if (isRelinking())
    {
        relinkWith (files);
        return;
    }

    ++auditionRequestId;
    places.noteRecentFolder (files.front().getParentDirectory());
    if (onAddFiles != nullptr)
        onAddFiles (files);
}

void BrowserView::loadFiles (const std::vector<juce::File>& files)
{
    if (files.empty())
        return;

    if (isRelinking())
    {
        relinkWith (files);
        return;
    }

    ++auditionRequestId;
    places.noteRecentFolder (files.front().getParentDirectory());
    if (onLoadFiles != nullptr)
        onLoadFiles (files);
}

void BrowserView::showRowMenu (const Browser::FileRow& row, juce::Point<int> screenPos)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    switch (row.kind)
    {
        case Browser::FileKind::audio:
            if (isRelinking())
            {
                menu.addItem (kMenuAdd, "Relink " + relinkFileName + " to This File");
                break;
            }
            menu.addItem (kMenuAdd, "Add to Kit");
            menu.addItem (kMenuLoad, "Load (Replace Kit)");
            break;
        case Browser::FileKind::preset:
            menu.addItem (kMenuLoadPreset, "Load Preset", ! isRelinking());
            menu.addItem (kMenuRename, "Rename");
            menu.addSeparator();
            menu.addItem (kMenuExport, "Export...");
            menu.addItem (kMenuExportWithSamples, "Export with Samples...");
            break;
        case Browser::FileKind::directory:
            menu.addItem (kMenuOpenFolder, "Open");
            if (! places.hasBookmark (row.file))
                menu.addItem (kMenuAddBookmark, "Add Bookmark");
            menu.addItem (kMenuSetDefaultFolder, "Set as Default Folder", ! isDefaultFolder (row.file), isDefaultFolder (row.file));
            break;
        case Browser::FileKind::other:
            return;
    }
    menu.addSeparator();
    menu.addItem (kMenuReveal, "Show in File Manager");

    menu.showMenuAsync (IntersectLookAndFeel::makeEditorMenuOptions (*this)
                            .withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)),
        [safe = juce::Component::SafePointer<BrowserView> (this), row] (int result)
        {
            if (safe == nullptr)
                return;

            switch (result)
            {
                case kMenuAdd:        safe->addFiles (safe->audioFilesFor (&row)); break;
                case kMenuLoad:       safe->loadFiles (safe->audioFilesFor (&row)); break;
                case kMenuLoadPreset:
                case kMenuOpenFolder: safe->activateRow (row, false); break;
                case kMenuAddBookmark: safe->places.addBookmark (row.file); break;
                case kMenuSetDefaultFolder: safe->useAsDefaultFolder (row.file); break;
                case kMenuReveal:     row.file.revealToUser(); break;
                case kMenuRename:     safe->beginRename (row.file); break;
                case kMenuExport:
                case kMenuExportWithSamples:
                    if (safe->onExportRequested != nullptr)
                        safe->onExportRequested (row.file, result == kMenuExportWithSamples);
                    break;
                default: break;
            }
        });
}

void BrowserView::showOpenMenu()
{
    // Two entries because the Windows and Linux system dialogs pick files or a folder, not both.
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addItem (kMenuOpenFiles, "Open Files...");
    menu.addItem (kMenuOpenFolderDialog, "Open Folder...");

    menu.showMenuAsync (IntersectLookAndFeel::makeEditorMenuOptions (*this).withTargetComponent (&openButton),
        [safe = juce::Component::SafePointer<BrowserView> (this)] (int result)
        {
            if (safe == nullptr)
                return;

            if (result == kMenuOpenFiles && safe->onOpenDialogRequested != nullptr)
                safe->onOpenDialogRequested();
            else if (result == kMenuOpenFolderDialog)
                safe->openFolderDialog();
        });
}

void BrowserView::openFolderDialog()
{
    folderChooser = std::make_unique<juce::FileChooser> ("Open Folder", currentDirectory);
    folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<BrowserView> (this)] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr)
                return;

            const auto dir = chooser.getResult();
            if (dir.isDirectory())
            {
                safe->navigateTo (dir);
                safe->focusFileList();
            }
        });
}

juce::File BrowserView::getPresetSaveFolder() const
{
    for (const auto& root : { AppFiles::getPresetsDir(), places.getCustomPresetsFolder() })
        if (root != juce::File() && (Browser::samePath (currentDirectory, root) || currentDirectory.isAChildOf (root)))
            return currentDirectory;

    return AppFiles::getPresetsDir();
}

void BrowserView::beginRename (const juce::File& presetFile)
{
    table.beginRename (presetFile);
}

// Empty or unchanged names are accepted (they simply leave the file as it is).
bool BrowserView::canRenamePreset (const juce::File& preset, const juce::String& newName)
{
    const auto name = juce::File::createLegalFileName (newName.trim());
    if (name.isEmpty() || name == preset.getFileNameWithoutExtension())
        return true;

    const auto target = preset.getSiblingFile (name + AppFiles::kPresetExtension);
    if (target.exists() && target != preset)   // "!=" lets a case-only rename through on case-insensitive disks
    {
        showTemporaryStatus ("A preset named \"" + name + "\" already exists");
        return false;
    }
    return true;
}

void BrowserView::renamePreset (const juce::File& preset, const juce::String& newName)
{
    const auto name = juce::File::createLegalFileName (newName.trim());
    if (name.isEmpty() || name == preset.getFileNameWithoutExtension() || ! preset.existsAsFile())
        return;

    const auto target = preset.getSiblingFile (name + AppFiles::kPresetExtension);
    if ((target.exists() && target != preset) || ! preset.moveFileTo (target))
    {
        showTemporaryStatus ("Couldn't rename " + preset.getFileNameWithoutExtension());
        return;
    }

    revealFile (target);
}

void BrowserView::showTemporaryStatus (const juce::String& text)
{
    const int id = ++statusMessageId;
    temporaryStatus = text;
    repaint (statusArea);
    juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<BrowserView> (this), id]
    {
        if (safe != nullptr && safe->statusMessageId == id)
        {
            safe->temporaryStatus.clear();
            safe->repaint (safe->statusArea);
        }
    });
}

void BrowserView::updateStatusBar()
{
    const int count = (int) table.getRows().size();
    const int selected = (int) table.getSelectedRows().size();
    auto withSelection = [selected] (juce::String text)
    {
        if (selected > 0)
            text << juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) << selected << " selected";
        return text;
    };

    // "10,752" reads better than "10752".
    auto formatCount = [] (int value)
    {
        auto digits = juce::String (value);
        for (int i = digits.length() - 3; i > 0; i -= 3)
            digits = digits.substring (0, i) + "," + digits.substring (i);
        return digits;
    };
    const auto items = formatCount (count) + (count == 1 ? " item" : " items");

    statusIsNote = false;
    if (searchMode && searchScanning)
        statusText = "Searching...";
    else if (searchMode)
    {
        statusText = count == 0 ? juce::String ("No matches")
                                : withSelection (searchTotalMatches > 0
                                                     ? items + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))
                                                           + formatCount (searchTotalMatches)
                                                           + (searchTotalMatches == 1 ? " match" : " matches")
                                                     : items);
        if (searchIncomplete)
        {
            statusText << " - this folder is too big to search completely, try a narrower one";
            statusIsNote = true;
        }
    }
    else
        statusText = count == 0 ? juce::String ("Empty folder") : withSelection (items);

    repaint (statusArea);
}

//==============================================================================
// Search

void BrowserView::onSearchTextChanged()
{
    searchQuery = searchEditor.getText().trim();
    clearSearchButton.setVisible (searchEditor.getText().isNotEmpty());
    resized();

    if (searchQuery.isEmpty())
    {
        if (searchMode)
        {
            endSearch();
            listCurrentDirectory();   // the folder you're in now, not where the search started
        }
        return;
    }

    searchMode = true;
    searchScanning = true;
    updateSearchStatus();

    const int id = ++searchDebounceId;
    juce::Timer::callAfterDelay (kSearchDelayMs, [safe = juce::Component::SafePointer<BrowserView> (this), id]
    {
        if (safe != nullptr && safe->searchDebounceId == id && safe->searchMode)
            safe->launchSearch();
    });
}

void BrowserView::launchSearch (bool rebuildIndex)
{
    if (! searchMode || searchQuery.isEmpty())
        return;

    searchScanning = true;
    updateSearchStatus();
    searcher.search (currentDirectory, searchQuery, rebuildIndex);
}

void BrowserView::applySearchResults (const DirectorySearch::Result& result)
{
    // Ignore stale deliveries: search closed, a newer query in the box, or you've moved on to
    // another folder (its own view is on the way).
    if (! searchMode || result.query != searchQuery || ! Browser::samePath (result.folder, currentDirectory))
        return;

    searchScanning = false;
    searchTotalMatches = result.totalMatches;
    searchIncomplete = result.incomplete;

    std::vector<Browser::FileRow> rows;
    rows.reserve (result.rows.size());
    for (const auto& row : result.rows)
        rows.push_back ({ row.file, row.directory ? Browser::FileKind::directory : Browser::classifyByName (row.file),
                          row.directory ? row.matches : -1 });

    stopAuditionIfActive();
    infoCache.cancelPending();
    lastFocusedFile = juce::File();
    table.setFullPathTooltips (false);   // every row is in this folder
    table.setRows (std::move (rows));
    updateSearchStatus();
    onSelectionChanged();
}

void BrowserView::endSearch()
{
    if (searchEditor.getText().isNotEmpty())
        searchEditor.setText ({}, juce::dontSendNotification);
    searchQuery.clear();
    searchMode = false;
    searchScanning = false;
    searchIncomplete = false;
    searchTotalMatches = 0;
    ++searchDebounceId;   // a queued launch must not restart it
    searcher.cancel();    // also frees the index
    clearSearchButton.setVisible (false);
    table.setStatus ({});
    resized();
}

void BrowserView::clearSearch()
{
    searchEditor.clear();
    onSearchTextChanged();
    focusFileList();
}

void BrowserView::updateSearchStatus()
{
    const bool hasRows = ! table.getRows().empty();

    // Progress and counts go to the bottom bar; the list only explains an empty result.
    if (searchMode && ! hasRows)
        table.setStatus (searchScanning ? juce::String ("Searching...")
                                        : "No matches for \"" + searchQuery + "\" in this folder");
    else
        table.setStatus ({});

    updateStatusBar();
}

//==============================================================================
// Keyboard, layout, theme

bool BrowserView::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
        if (searchEditor.getText().isNotEmpty())
            clearSearch();
        else if (isRelinking())
            endRelink();
        else if (onCloseRequested != nullptr)
            onCloseRequested();
        return true;
    }

    // Space plays / pauses the preview. It is only taken when there is something to play or
    // pause, so on folders and presets it still reaches the DAW (usually its transport).
    if (key.isKeyCode (juce::KeyPress::spaceKey) && ! key.getModifiers().isAnyModifierKeyDown())
    {
        const auto* focused = table.getFocusedRow();
        if (focused != nullptr && focused->kind == Browser::FileKind::audio)
        {
            togglePlayback();
            return true;
        }
        if (audition.state == AuditionStatus::State::playing || audition.state == AuditionStatus::State::decoding)
        {
            stopAuditionIfActive();
            return true;
        }
        return false;
    }

    if (key.getTextCharacter() == '/' || (key.getModifiers().isCommandDown() && key.getKeyCode() == 'F'))
    {
        searchEditor.grabKeyboardFocus();
        return true;
    }

    return false;
}

void BrowserView::refreshThemeColours()
{
    for (auto* button : std::initializer_list<juce::Button*> { &backButton, &forwardButton, &upButton, &refreshButton,
                                                               &openButton, &clearSearchButton, &relinkCancelButton })
    {
        button->setColour (juce::TextButton::buttonColourId, getTheme().surface4.withAlpha (0.95f));
        button->setColour (juce::TextButton::textColourOnId, getTheme().text2.withAlpha (0.88f));
        button->setColour (juce::TextButton::textColourOffId, getTheme().text2.withAlpha (0.88f));
    }

    searchEditor.setColour (juce::TextEditor::backgroundColourId, getTheme().surface1.withAlpha (0.92f));
    searchEditor.setColour (juce::TextEditor::outlineColourId, getTheme().surface4.withAlpha (0.92f));
    searchEditor.setColour (juce::TextEditor::focusedOutlineColourId, getTheme().accent.withAlpha (0.85f));
    searchEditor.setColour (juce::TextEditor::textColourId, getTheme().text2);
    searchEditor.setColour (juce::TextEditor::highlightColourId, getTheme().accent.withAlpha (0.35f));
    searchEditor.setTextToShowWhenEmpty (kSearchPlaceholder, getTheme().text0.withAlpha (0.7f));

    FullViewBar::styleButton (closeButton);
    places.refreshThemeColours();
    table.refreshThemeColours();
    preview.refreshThemeColours();
    breadcrumb.refreshThemeColours();
    searchEditor.repaint();
    repaint();
}

void BrowserView::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().surface0);

    const auto bar = getLocalBounds().removeFromBottom (FullViewBar::kHeight);
    g.setColour (getTheme().surface4.withAlpha (0.85f));
    g.fillRect (places.getRight(), 0, 1, bar.getY());
    g.fillRect (preview.getX() - 1, 0, 1, bar.getY());
    FullViewBar::paintTopLine (g, bar);

    // Status line: item count / selection / search, or a passing note in accent.
    const bool showingNote = temporaryStatus.isNotEmpty();
    g.setColour (showingNote || statusIsNote ? getTheme().accent : getTheme().text1);
    g.setFont (IntersectLookAndFeel::makeFont (9.0f, showingNote));
    g.drawText (showingNote ? temporaryStatus : statusText, statusArea, juce::Justification::centredLeft, true);

    if (isRelinking() && ! relinkBannerBounds.isEmpty())
    {
        g.setColour (getTheme().color5.withAlpha (0.22f));
        g.fillRoundedRectangle (relinkBannerBounds.toFloat(), 3.0f);
        g.setColour (getTheme().text2);
        g.setFont (IntersectLookAndFeel::makeFont (10.0f, true));
        const auto textArea = relinkBannerBounds.withTrimmedRight (relinkCancelButton.getWidth() + 8).reduced (8, 0);
        g.drawFittedText ("RELINK " + relinkFileName + "  -  pick the file to use in its place",
                          textArea, juce::Justification::centredLeft, 1, 0.8f);
    }
}

void BrowserView::resized()
{
    auto area = getLocalBounds();
    auto bar = FullViewBar::content (area.removeFromBottom (FullViewBar::kHeight));
    closeButton.setBounds (bar.removeFromRight (FullViewBar::kCloseWidth));
    bar.removeFromRight (12);
    statusArea = bar.withTrimmedLeft (4);

    places.setBounds (area.removeFromLeft (kPlacesW));
    area.removeFromLeft (1);
    preview.setBounds (area.removeFromRight (kPreviewW));
    area.removeFromRight (1);

    auto middle = area.reduced (8, 7);
    const int gap = 3;

    relinkBannerBounds = {};
    if (isRelinking())
    {
        relinkBannerBounds = middle.removeFromTop (kToolbarRowH + 2);
        relinkCancelButton.setBounds (relinkBannerBounds.withTrimmedLeft (relinkBannerBounds.getWidth() - 60)
                                                        .reduced (2, 2));
        middle.removeFromTop (5);
    }

    auto nav = middle.removeFromTop (kToolbarRowH);
    for (auto* button : { &backButton, &forwardButton, &upButton, &refreshButton })
    {
        button->setBounds (nav.removeFromLeft (22));
        nav.removeFromLeft (gap);
    }
    nav.removeFromLeft (gap);
    openButton.setBounds (nav.removeFromRight (24));
    nav.removeFromRight (gap);
    breadcrumb.setBounds (nav);

    middle.removeFromTop (5);
    auto searchRow = middle.removeFromTop (kToolbarRowH);
    if (clearSearchButton.isVisible())
    {
        clearSearchButton.setBounds (searchRow.removeFromRight (20));
        searchRow.removeFromRight (gap);
    }
    searchEditor.setBounds (searchRow);

    middle.removeFromTop (6);
    table.setBounds (middle);
}
