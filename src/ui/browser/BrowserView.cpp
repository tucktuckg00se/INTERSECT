#include "BrowserView.h"
#include "../IntersectLookAndFeel.h"
#include "../../AppFiles.h"
#include <algorithm>

namespace
{
constexpr int kPlacesW = 168;
constexpr int kPreviewW = 226;
constexpr int kToolbarRowH = 20;
constexpr int kAuditionDelayMs = 150;
constexpr int kSearchDelayMs = 180;

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
                                                               &openButton, &clearSearchButton })
    {
        button->getProperties().set (IntersectLookAndFeel::outlineOnlyButtonProperty, true);
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (*button);
    }
    clearSearchButton.setVisible (false);

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
    currentDirectory = dir;
    breadcrumb.setDirectory (dir);
    places.setCurrentDirectory (dir);

    if (searchEditor.getText().isNotEmpty())
        searchEditor.setText ({}, juce::dontSendNotification);
    searchQuery.clear();
    searchMode = false;
    searchScanning = false;
    searchTruncated = false;
    searcher.cancel();
    clearSearchButton.setVisible (false);
    resized();

    listCurrentDirectory();
    updateNavButtons();
}

void BrowserView::listCurrentDirectory()
{
    std::vector<Browser::FileRow> rows;
    if (currentDirectory.isDirectory())
    {
        for (const auto& child : currentDirectory.findChildFiles (juce::File::findFilesAndDirectories, false, "*",
                                                                  juce::File::FollowSymlinks::no))
        {
            if (child.getFileName().startsWithChar ('.'))
                continue;   // hidden files and folders

            const auto kind = Browser::classify (child);
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
        launchSearch();
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

    if (Browser::samePath (dir, currentDirectory) && ! searchMode)
        listCurrentDirectory();
    else
        navigateTo (dir);

    lastFocusedFile = file;   // selecting it below must not start a preview
    table.selectFile (file);
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
            if (row.file.existsAsFile() && onPresetChosen != nullptr)
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

    ++auditionRequestId;
    places.noteRecentFolder (files.front().getParentDirectory());
    if (onAddFiles != nullptr)
        onAddFiles (files);
}

void BrowserView::loadFiles (const std::vector<juce::File>& files)
{
    if (files.empty())
        return;

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
            menu.addItem (kMenuAdd, "Add to Kit");
            menu.addItem (kMenuLoad, "Load (Replace Kit)");
            break;
        case Browser::FileKind::preset:
            menu.addItem (kMenuLoadPreset, "Load Preset");
            menu.addItem (kMenuRename, "Rename");
            menu.addSeparator();
            menu.addItem (kMenuExport, "Export...");
            menu.addItem (kMenuExportWithSamples, "Export with Samples...");
            break;
        case Browser::FileKind::directory:
            menu.addItem (kMenuOpenFolder, "Open");
            if (! places.hasBookmark (row.file))
                menu.addItem (kMenuAddBookmark, "Add Bookmark");
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
    table.setStatus (text, true);
    juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<BrowserView> (this), id]
    {
        if (safe != nullptr && safe->statusMessageId == id)
            safe->updateSearchStatus();
    });
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
        searcher.cancel();
        if (searchMode)
        {
            searchMode = false;
            searchScanning = false;
            searchTruncated = false;
            listCurrentDirectory();
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

void BrowserView::launchSearch()
{
    if (! searchMode || searchQuery.isEmpty())
        return;

    searchScanning = true;
    updateSearchStatus();
    searcher.search (currentDirectory, searchQuery);
}

void BrowserView::applySearchResults (const DirectorySearch::Result& result)
{
    // Ignore stale deliveries: search closed, or a newer query is already in the box.
    if (! searchMode || result.query != searchQuery)
        return;

    searchScanning = false;
    searchTruncated = result.truncated;

    std::vector<Browser::FileRow> rows;
    rows.reserve (result.matches.size());
    for (const auto& match : result.matches)
        rows.push_back ({ match.file, match.directory ? Browser::FileKind::directory
                                                      : Browser::classifyByName (match.file) });

    stopAuditionIfActive();
    infoCache.cancelPending();
    lastFocusedFile = juce::File();
    table.setFullPathTooltips (true);
    table.setRows (std::move (rows));
    updateSearchStatus();
    onSelectionChanged();
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

    if (! searchMode)
        table.setStatus ({}, false);
    else if (searchScanning)
        table.setStatus ("Searching...", hasRows);   // a strip while old results are still showing
    else if (! hasRows)
        table.setStatus ("No matches for \"" + searchQuery + "\"", false);
    else if (searchTruncated)
        table.setStatus ("Showing the first " + juce::String ((int) table.getRows().size())
                             + " matches - refine your search", true);
    else
        table.setStatus ({}, false);
}

//==============================================================================
// Keyboard, layout, theme

bool BrowserView::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
        if (searchEditor.getText().isNotEmpty())
            clearSearch();
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
                                                               &openButton, &clearSearchButton })
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

    g.setColour (getTheme().surface4.withAlpha (0.85f));
    g.fillRect (places.getRight(), 0, 1, getHeight());
    g.fillRect (preview.getX() - 1, 0, 1, getHeight());
}

void BrowserView::resized()
{
    auto area = getLocalBounds();
    places.setBounds (area.removeFromLeft (kPlacesW));
    area.removeFromLeft (1);
    preview.setBounds (area.removeFromRight (kPreviewW));
    area.removeFromRight (1);

    auto middle = area.reduced (8, 7);
    const int gap = 3;

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
