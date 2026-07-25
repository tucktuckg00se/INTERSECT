#include "SampleBrowserPanel.h"
#include "IntersectLookAndFeel.h"
#include <algorithm>

namespace
{
enum MenuIds
{
    kAddBookmark = 1,
    kRemoveBookmark,
};

const juce::String kBrowserDragPrefix = "INTERSECT_BROWSER_FILES\n";

// Kept ASCII on purpose: non-ASCII punctuation (ellipsis, em-dash) mis-renders on this toolchain,
// so status text uses plain "..." / "-" rather than fancy glyphs.
const juce::String kSearchPlaceholder = "Search files & folders...";
const juce::String kSearchingLabel    = "Searching...";
const juce::String kTruncatedLabel    = "Showing first matches - refine search";

juce::String normalisePath (const juce::File& file)
{
    return file.getFullPathName();
}

bool samePath (const juce::File& a, const juce::File& b)
{
    return normalisePath (a) == normalisePath (b);
}

bool isImplicitUserFolder (const juce::File& file)
{
    const juce::File userFolders[] =
    {
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
    };

    for (const auto& userFolder : userFolders)
        if (userFolder != juce::File() && samePath (file, userFolder))
            return true;

    return false;
}
}

void SampleBrowserPanel::PathDisplay::paint (juce::Graphics& g)
{
    const auto pathWarning = getTheme().color5;
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (owner.pathErrorTicks > 0 ? getTheme().surface1.interpolatedWith (pathWarning, 0.18f).withAlpha (0.94f)
                                          : getTheme().surface1.withAlpha (0.92f));
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (owner.pathErrorTicks > 0 ? pathWarning.withAlpha (0.8f) : getTheme().surface4.withAlpha (0.92f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    if (owner.pathEditorActive)
        return;

    g.setFont (IntersectLookAndFeel::makeFont (9.0f));
    g.setColour (getTheme().text2);
    g.drawText (owner.currentDirectory.getFullPathName(),
                getLocalBounds().reduced (5, 0),
                juce::Justification::centredLeft,
                true);
}

void SampleBrowserPanel::PathDisplay::mouseDown (const juce::MouseEvent&)
{
    owner.beginPathEditing();
}

SampleBrowserPanel::SearchToggleButton::SearchToggleButton()
    : juce::Button ("search")
{
    getProperties().set (IntersectLookAndFeel::outlineOnlyButtonProperty, true);
    setClickingTogglesState (false);
    setTooltip ("Search files & folders");
}

void SampleBrowserPanel::SearchToggleButton::paintButton (juce::Graphics& g,
                                                          bool shouldDrawButtonAsHighlighted,
                                                          bool shouldDrawButtonAsDown)
{
    getLookAndFeel().drawButtonBackground (g, *this,
                                           findColour (juce::TextButton::buttonColourId),
                                           shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    auto iconCol = findColour (getToggleState() ? juce::TextButton::textColourOnId
                                                : juce::TextButton::textColourOffId);
    if (iconCol.isTransparent())
        iconCol = getTheme().text2;
    g.setColour (iconCol);

    // Magnifier: a circular lens with a short handle off the lower-right.
    const auto area = getLocalBounds().toFloat().reduced (getWidth() * 0.30f, getHeight() * 0.30f);
    const float d = juce::jmin (area.getWidth(), area.getHeight());
    juce::Rectangle<float> lens (area.getX(), area.getY(), d, d);
    const float stroke = juce::jmax (1.1f, d * 0.13f);
    g.drawEllipse (lens, stroke);

    const auto centre = lens.getCentre();
    const float r = d * 0.5f;
    const juce::Point<float> handleStart (centre.x + r * 0.72f, centre.y + r * 0.72f);
    const juce::Point<float> handleEnd (area.getRight(), area.getBottom());
    g.drawLine ({ handleStart, handleEnd }, stroke);
}

SampleBrowserPanel::SampleBrowserPanel()
{
    setWantsKeyboardFocus (true);

    titleLabel.setText ("BROWSER", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (pathDisplay);

    pathEditor.setJustification (juce::Justification::centredLeft);
    pathEditor.setSelectAllWhenFocused (true);
    pathEditor.setIndents (5, 0);
    pathEditor.setInputRestrictions (0);
    pathEditor.setMultiLine (false);
    pathEditor.setReturnKeyStartsNewLine (false);
    pathEditor.setScrollbarsShown (false);
    pathEditor.setFont (IntersectLookAndFeel::makeFont (9.0f));
    pathEditor.onReturnKey = [this] { commitPathText(); };
    pathEditor.onEscapeKey = [this] { endPathEditing (true); };
    pathEditor.onFocusLost = [this]
    {
        if (pathEditorActive)
            endPathEditing (true);
    };
    addChildComponent (pathEditor);

    for (auto* button : { &backButton, &forwardButton, &upButton, &refreshButton })
    {
        button->getProperties().set (IntersectLookAndFeel::outlineOnlyButtonProperty, true);
        button->setColour (juce::TextButton::buttonColourId, getTheme().surface4);
        button->setColour (juce::TextButton::textColourOnId, getTheme().text2);
        button->setColour (juce::TextButton::textColourOffId, getTheme().text2);
        addAndMakeVisible (*button);
    }

    backButton.setTooltip ("Back");
    forwardButton.setTooltip ("Forward");
    upButton.setTooltip ("Up folder");
    refreshButton.setTooltip ("Refresh");

    addAndMakeVisible (searchToggleButton);
    searchToggleButton.onClick = [this] { setSearchInputMode (! searchInputMode); };

    searchEditor.setJustification (juce::Justification::centredLeft);
    searchEditor.setIndents (5, 0);
    searchEditor.setInputRestrictions (0);
    searchEditor.setMultiLine (false);
    searchEditor.setReturnKeyStartsNewLine (false);
    searchEditor.setScrollbarsShown (false);
    searchEditor.setFont (IntersectLookAndFeel::makeFont (9.0f));
    searchEditor.setTextToShowWhenEmpty (kSearchPlaceholder, getTheme().text0.withAlpha (0.55f));
    searchEditor.onTextChange = [this] { onSearchTextChanged(); };
    searchEditor.onEscapeKey = [this] { setSearchInputMode (false); };
    searchEditor.onReturnKey = [this]
    {
        if (! files.empty())
            activateSelectedFilesOrRow (juce::jmax (0, fileList.getSelectedRow()));
    };
    addChildComponent (searchEditor);

    clearSearchButton.getProperties().set (IntersectLookAndFeel::outlineOnlyButtonProperty, true);
    clearSearchButton.setTooltip ("Clear search");
    clearSearchButton.onClick = [this] { searchEditor.clear(); onSearchTextChanged(); };
    addChildComponent (clearSearchButton);

    searchStatusLabel.setJustificationType (juce::Justification::centred);
    searchStatusLabel.setInterceptsMouseClicks (false, false);
    searchStatusLabel.setFont (IntersectLookAndFeel::makeFont (10.0f));
    addChildComponent (searchStatusLabel);

    searcher.isAudioFile = [this] (const juce::File& f) { return isSupportedAudioFile (f); };
    searcher.onComplete = [this] (const DirectorySearch::Result& r) { applySearchResults (r); };

    backButton.onClick = [this]
    {
        if (historyIndex > 0)
        {
            --historyIndex;
            currentDirectory = history[historyIndex];
            refreshFiles();
            rebuildLocations();
        }
    };

    forwardButton.onClick = [this]
    {
        if (historyIndex + 1 < history.size())
        {
            ++historyIndex;
            currentDirectory = history[historyIndex];
            refreshFiles();
            rebuildLocations();
        }
    };

    upButton.onClick = [this] { goUp(); };
    refreshButton.onClick = [this]
    {
        if (searchMode)
            launchSearch();
        else
            refreshFiles();
    };
    for (auto* list : { &locationList, &fileList })
    {
        list->setRowHeight (22);
        list->setMultipleSelectionEnabled (false);
        list->setClickingTogglesRowSelection (false);
        list->setWantsKeyboardFocus (true);
        addAndMakeVisible (*list);
    }

    fileList.setMultipleSelectionEnabled (true);

    updateListThemeColours();

    auto initial = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    setCurrentDirectory (initial);
}

SampleBrowserPanel::~SampleBrowserPanel() = default;

void SampleBrowserPanel::paint (juce::Graphics& g)
{
    updateListThemeColours();

    g.fillAll (getTheme().surface0);
    g.setColour (getTheme().surface4.withAlpha (0.85f));
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    g.setColour (getTheme().surface0.withAlpha (0.98f));
    g.fillRect (locationSectionBounds.expanded (0, 2));
    g.fillRect (fileSectionBounds.expanded (0, 2));

    const auto splitterColour = splitterDragging
        ? getTheme().accent.withAlpha (0.9f)
        : (splitterHover ? getTheme().surface5.withAlpha (0.95f)
                         : getTheme().surface4.withAlpha (0.95f));
    g.setColour (splitterColour);
    g.fillRect (splitterBounds.withHeight (1).withY (splitterBounds.getCentreY()));
    if (splitterHover || splitterDragging)
        g.fillRect (splitterBounds.reduced (24, 2).withHeight (3).withY (splitterBounds.getCentreY() - 1));
}

void SampleBrowserPanel::resized()
{
    auto area = getLocalBounds().reduced (8, 7);
    auto header = area.removeFromTop (48);
    auto titleRow = header.removeFromTop (18);

    const int navW = 22;
    const int gap = 3;
    auto navArea = titleRow.removeFromRight (navW * 4 + gap * 3);
    backButton.setBounds (navArea.removeFromLeft (navW));
    navArea.removeFromLeft (gap);
    forwardButton.setBounds (navArea.removeFromLeft (navW));
    navArea.removeFromLeft (gap);
    upButton.setBounds (navArea.removeFromLeft (navW));
    navArea.removeFromLeft (gap);
    refreshButton.setBounds (navArea.removeFromLeft (navW));

    titleRow.removeFromRight (8);
    titleLabel.setBounds (titleRow);

    header.removeFromTop (4);
    layoutPathSearchRow (header.removeFromTop (22));

    area.removeFromTop (5);
    if (locationSectionHeight <= 0)
        locationSectionHeight = getDefaultLocationSectionHeight (area);

    locationSectionHeight = clampLocationSectionHeight (locationSectionHeight, area);
    locationSectionBounds = area.removeFromTop (locationSectionHeight);
    splitterBounds = area.removeFromTop (6);
    fileSectionBounds = area;

    locationList.setBounds (locationSectionBounds);
    fileList.setBounds (fileSectionBounds);
    updateSearchStatusLabel();
}

// Lays out the shared path/search row: magnifier toggle on the left, then the path display
// (path mode) or the search editor + clear button (search mode) sharing the remaining width.
void SampleBrowserPanel::layoutPathSearchRow (juce::Rectangle<int> row)
{
    pathSearchRow = row;
    const int toggleW = 20;
    const int gap = 3;
    searchToggleButton.setBounds (row.removeFromLeft (toggleW));
    row.removeFromLeft (gap);

    pathDisplay.setBounds (row);
    pathEditor.setBounds (row);

    auto searchRow = row;
    if (searchInputMode)
    {
        const int clearW = 20;
        clearSearchButton.setBounds (searchRow.removeFromRight (clearW));
        searchRow.removeFromRight (gap);
    }
    searchEditor.setBounds (searchRow);
}

void SampleBrowserPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! splitterBounds.expanded (0, 3).contains (e.getPosition()))
        return;

    splitterDragging = true;
    splitterDragStartY = e.y;
    splitterDragStartHeight = locationSectionHeight;
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    repaint();
}

void SampleBrowserPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (! splitterDragging)
        return;

    auto area = getLocalBounds().reduced (8, 7);
    area.removeFromTop (48 + 5);
    locationSectionHeight = clampLocationSectionHeight (splitterDragStartHeight + e.y - splitterDragStartY, area);
    resized();
    repaint();
}

void SampleBrowserPanel::mouseMove (const juce::MouseEvent& e)
{
    updateSplitterCursor (e.getPosition());
}

void SampleBrowserPanel::mouseExit (const juce::MouseEvent&)
{
    if (splitterHover && ! splitterDragging)
    {
        splitterHover = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SampleBrowserPanel::mouseUp (const juce::MouseEvent&)
{
    if (! splitterDragging)
        return;

    splitterDragging = false;
    setMouseCursor (splitterHover ? juce::MouseCursor::UpDownResizeCursor
                                  : juce::MouseCursor::NormalCursor);
    repaint();
}

bool SampleBrowserPanel::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::returnKey)
    {
        activateSelectedFilesOrRow (fileList.getSelectedRow());
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        goUp();
        return true;
    }

    return false;
}

void SampleBrowserPanel::setBookmarks (const juce::StringArray& paths)
{
    bookmarkPaths.clear();
    for (auto path : paths)
    {
        path = path.trim();
        if (path.isNotEmpty() && ! bookmarkPaths.contains (path))
            bookmarkPaths.add (path);
    }
    rebuildLocations();
}

juce::StringArray SampleBrowserPanel::getBookmarks() const
{
    return bookmarkPaths;
}

int SampleBrowserPanel::LocationListModel::getNumRows()
{
    return owner.getNumLocationRows();
}

void SampleBrowserPanel::LocationListModel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    owner.paintLocationRow (row, g, width, height, selected);
}

void SampleBrowserPanel::LocationListModel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    owner.locationRowClicked (row, e);
}

int SampleBrowserPanel::FileListModel::getNumRows()
{
    return owner.getNumFileRows();
}

void SampleBrowserPanel::FileListModel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    owner.paintFileRow (row, g, width, height, selected);
}

void SampleBrowserPanel::FileListModel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    owner.fileRowClicked (row, e);
}

void SampleBrowserPanel::FileListModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    owner.activateSelectedFilesOrRow (row);
}

juce::var SampleBrowserPanel::FileListModel::getDragSourceDescription (const juce::SparseSet<int>& rowsToDescribe)
{
    return owner.makeDragDescriptionForRows (rowsToDescribe);
}

int SampleBrowserPanel::getNumLocationRows() const
{
    return (int) locations.size();
}

int SampleBrowserPanel::getNumFileRows() const
{
    return (int) files.size();
}

void SampleBrowserPanel::paintLocationRow (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto rowBounds = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1);

    if (row < 0 || row >= (int) locations.size())
        return;

    const auto& item = locations[(size_t) row];
    if (item.kind == LocationKind::section)
    {
        g.setColour (getTheme().text0.withAlpha (0.78f));
        g.setFont (IntersectLookAndFeel::makeFont (8.3f, true));
        g.drawText (item.label.toUpperCase(), rowBounds.reduced (4, 0), juce::Justification::centredLeft, true);
        return;
    }

    const bool active = item.file != juce::File() && samePath (item.file, currentDirectory);
    if (selected || active)
    {
        g.setColour (active ? getTheme().surface3.interpolatedWith (getTheme().color1, 0.22f)
                            : getTheme().surface3);
        g.fillRoundedRectangle (rowBounds.toFloat(), 3.0f);
    }

    g.setFont (IntersectLookAndFeel::makeFont (9.5f, true));
    g.setColour (item.unavailable ? getTheme().text0.withAlpha (0.45f) : getTheme().text2);
    auto textBounds = rowBounds.reduced (6, 0);
    g.drawText (locationPrefixForKind (item.kind), textBounds.removeFromLeft (22), juce::Justification::centredLeft, true);
    g.setFont (IntersectLookAndFeel::makeFont (9.5f));
    g.drawText (item.label, textBounds, juce::Justification::centredLeft, true);
}

void SampleBrowserPanel::paintFileRow (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto rowBounds = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1);

    if (row < 0 || row >= (int) files.size())
        return;

    const auto& item = files[(size_t) row];
    if (selected)
    {
        g.setColour (getTheme().surface3.interpolatedWith (getTheme().accent, 0.13f));
        g.fillRoundedRectangle (rowBounds.toFloat(), 3.0f);
    }

    auto textBounds = rowBounds.reduced (6, 0);
    g.setFont (IntersectLookAndFeel::makeFont (9.5f, true));
    g.setColour (item.directory ? getTheme().color1.brighter (0.25f) : getTheme().waveform.withAlpha (0.92f));
    g.drawText (item.directory ? "D" : "W", textBounds.removeFromLeft (18), juce::Justification::centredLeft, true);

    auto metaBounds = textBounds.removeFromRight (46);
    g.setFont (IntersectLookAndFeel::makeFont (9.5f));
    g.setColour (item.audio || item.directory ? getTheme().text2 : getTheme().text0.withAlpha (0.55f));
    g.drawText (item.displayName, textBounds, juce::Justification::centredLeft, true);
    g.setColour (getTheme().text0.withAlpha (0.8f));
    g.drawText (formatFileMeta (item), metaBounds, juce::Justification::centredRight, true);
}

void SampleBrowserPanel::locationRowClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showLocationMenu (row, e.getScreenPosition());
        return;
    }

    if (row >= 0 && row < (int) locations.size())
    {
        const auto& item = locations[(size_t) row];
        if (item.kind != LocationKind::section && ! item.unavailable && item.file.isDirectory())
            setCurrentDirectory (item.file);
    }
}

void SampleBrowserPanel::fileRowClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showFileMenu (row, e.getScreenPosition());
}

void SampleBrowserPanel::rebuildLocations()
{
    locations.clear();

    auto addSection = [this] (const juce::String& label)
    {
        locations.push_back ({ LocationKind::section, label, {}, false });
    };

    auto addLocation = [this] (LocationKind kind, const juce::String& label, const juce::File& file)
    {
        if (file == juce::File())
            return;

        for (const auto& existing : locations)
            if (existing.kind != LocationKind::section && samePath (existing.file, file))
                return;

        locations.push_back ({ kind, label, file, ! file.isDirectory() });
    };

    addSection ("Bookmarks");
    for (const auto& path : bookmarkPaths)
    {
        const juce::File bookmark (path);
        auto label = bookmark.getFileName();
        if (label.isEmpty())
            label = bookmark.getFullPathName();
        locations.push_back ({ LocationKind::bookmark, label, bookmark, ! bookmark.isDirectory() });
    }

    addSection ("Drives");
    juce::StringArray rootNames, rootPaths;
    juce::FileBrowserComponent::getDefaultRoots (rootNames, rootPaths);
    for (int i = 0; i < rootPaths.size(); ++i)
    {
        const auto path = rootPaths[i].trim();
        if (path.isEmpty())
            continue;

        const juce::File root (path);
        if (isImplicitUserFolder (root))
            continue;

        auto label = rootNames[i].trim();
        if (label.isEmpty())
            label = root.getFullPathName();
        addLocation (LocationKind::drive, label, root);
    }

   #if JUCE_LINUX
    const auto userName = juce::SystemStats::getLogonName();
    for (const auto& mountRoot : { juce::File ("/media").getChildFile (userName), juce::File ("/mnt") })
    {
        if (! mountRoot.isDirectory())
            continue;

        for (const auto& volume : mountRoot.findChildFiles (juce::File::findDirectories,
                                                            false,
                                                            "*",
                                                            juce::File::FollowSymlinks::no))
            addLocation (LocationKind::drive, volume.getFileName(), volume);
    }
   #endif

    locationList.updateContent();
    locationList.repaint();
}

// Normal-browsing reset: leaves search mode (any directory navigation calls this) and shows
// the current directory. Every navigation path funnels through here, so search always exits.
void SampleBrowserPanel::refreshFiles()
{
    searchMode = false;
    searchScanning = false;
    searchTruncated = false;

    if (searchInputMode)
    {
        searchInputMode = false;
        searcher.cancel();
        searchEditor.setText ({}, juce::dontSendNotification);
        searchQuery.clear();
        searchEditor.setVisible (false);
        clearSearchButton.setVisible (false);
        searchToggleButton.setToggleState (false, juce::dontSendNotification);
        pathDisplay.setVisible (true);
        if (! pathSearchRow.isEmpty())
            layoutPathSearchRow (pathSearchRow);
        searchToggleButton.repaint();
    }

    populateCurrentDirectoryFiles();
    updateSearchStatusLabel();
}

// Builds the file list from the current directory (folders + audio files). Does not touch
// search mode, so it also backs the "empty query" case where the search field stays open.
void SampleBrowserPanel::populateCurrentDirectoryFiles()
{
    files.clear();

    if (currentDirectory.isDirectory())
    {
        auto children = currentDirectory.findChildFiles (juce::File::findFilesAndDirectories,
                                                         false,
                                                         "*",
                                                         juce::File::FollowSymlinks::no);
        std::sort (children.begin(), children.end(), [] (const juce::File& a, const juce::File& b)
        {
            if (a.isDirectory() != b.isDirectory())
                return a.isDirectory();
            return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
        });

        for (const auto& child : children)
        {
            const bool isDir = child.isDirectory();
            const bool isAudio = isSupportedAudioFile (child);
            if (isDir || isAudio)
                files.push_back ({ child, isDir, isAudio, child.getFileName() });
        }
    }

    pathEditor.setText (currentDirectory.getFullPathName(), juce::dontSendNotification);
    backButton.setEnabled (historyIndex > 0);
    forwardButton.setEnabled (historyIndex + 1 < history.size());
    upButton.setEnabled (currentDirectory.getParentDirectory() != currentDirectory);

    fileList.updateContent();
    fileList.repaint();
    pathDisplay.repaint();
}

void SampleBrowserPanel::setCurrentDirectory (const juce::File& dir)
{
    if (! dir.isDirectory())
        return;

    currentDirectory = dir;
    while (history.size() - 1 > historyIndex)
        history.removeLast();
    history.add (dir);
    historyIndex = history.size() - 1;

    refreshFiles();
    rebuildLocations();
}

void SampleBrowserPanel::goUp()
{
    auto parent = currentDirectory.getParentDirectory();
    if (parent != juce::File() && parent != currentDirectory && parent.isDirectory())
        setCurrentDirectory (parent);
}

void SampleBrowserPanel::activateFileRow (int row)
{
    if (row < 0 || row >= (int) files.size())
        return;

    const auto& item = files[(size_t) row];
    if (item.directory)
    {
        setCurrentDirectory (item.file);
        return;
    }

    if (item.audio && item.file.existsAsFile() && onFilesChosen != nullptr)
        onFilesChosen ({ item.file });
}

void SampleBrowserPanel::activateSelectedFilesOrRow (int row)
{
    auto selectedAudio = getSelectedAudioFiles();
    if (! selectedAudio.empty() && onFilesChosen != nullptr)
    {
        onFilesChosen (selectedAudio);
        return;
    }

    activateFileRow (row);
}

void SampleBrowserPanel::showLocationMenu (int row, juce::Point<int> position)
{
    if (row < 0 || row >= (int) locations.size())
        return;

    const auto item = locations[(size_t) row];
    if (item.kind == LocationKind::section)
        return;

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    if (item.kind == LocationKind::bookmark)
        menu.addItem (kRemoveBookmark, "Remove Bookmark");
    else if (item.file.isDirectory() && ! hasBookmark (item.file))
        menu.addItem (kAddBookmark, "Add Bookmark");

    menu.showMenuAsync (IntersectLookAndFeel::makeEditorMenuOptions (*this)
                            .withTargetScreenArea ({ position, { 1, 1 } }),
        [this, item] (int result)
        {
            if (result == kAddBookmark)
                addBookmark (item.file);
            else if (result == kRemoveBookmark)
                removeBookmark (item.file);
        });
}

void SampleBrowserPanel::showFileMenu (int row, juce::Point<int> position)
{
    if (row < 0 || row >= (int) files.size())
        return;

    const auto item = files[(size_t) row];
    if (! item.directory)
        return;

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    if (! hasBookmark (item.file))
        menu.addItem (kAddBookmark, "Add Bookmark");

    menu.showMenuAsync (IntersectLookAndFeel::makeEditorMenuOptions (*this)
                            .withTargetScreenArea ({ position, { 1, 1 } }),
        [this, item] (int result)
        {
            if (result == kAddBookmark)
                addBookmark (item.file);
        });
}

void SampleBrowserPanel::addBookmark (const juce::File& dir)
{
    if (! dir.isDirectory() || hasBookmark (dir))
        return;

    bookmarkPaths.add (normalisePath (dir));
    rebuildLocations();
    refreshFiles();
    if (onBookmarksChanged != nullptr)
        onBookmarksChanged();
}

void SampleBrowserPanel::removeBookmark (const juce::File& dir)
{
    for (int i = bookmarkPaths.size(); --i >= 0;)
        if (normalisePath (juce::File (bookmarkPaths[i])) == normalisePath (dir))
            bookmarkPaths.remove (i);

    rebuildLocations();
    refreshFiles();
    if (onBookmarksChanged != nullptr)
        onBookmarksChanged();
}

bool SampleBrowserPanel::hasBookmark (const juce::File& dir) const
{
    for (const auto& path : bookmarkPaths)
        if (normalisePath (juce::File (path)) == normalisePath (dir))
            return true;
    return false;
}

std::vector<juce::File> SampleBrowserPanel::getSelectedAudioFiles() const
{
    std::vector<juce::File> selected;
    const auto rows = fileList.getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
    {
        const int row = rows[i];
        if (row >= 0 && row < (int) files.size())
        {
            const auto& item = files[(size_t) row];
            if (item.audio && item.file.existsAsFile())
                selected.push_back (item.file);
        }
    }
    return selected;
}

juce::String SampleBrowserPanel::makeDragDescriptionForRows (const juce::SparseSet<int>& rows) const
{
    juce::StringArray paths;
    for (int i = 0; i < rows.size(); ++i)
    {
        const int row = rows[i];
        if (row >= 0 && row < (int) files.size())
        {
            const auto& item = files[(size_t) row];
            if (item.audio && item.file.existsAsFile())
                paths.add (item.file.getFullPathName());
        }
    }

    if (paths.isEmpty())
        return {};

    return kBrowserDragPrefix + paths.joinIntoString ("\n");
}

void SampleBrowserPanel::commitPathText()
{
    const juce::File dir (pathEditor.getText().trim().unquoted());
    if (dir.isDirectory())
    {
        setCurrentDirectory (dir);
        endPathEditing (false);
        return;
    }

    flashPathError();
}

void SampleBrowserPanel::flashPathError()
{
    pathErrorTicks = 2;
    updateListThemeColours();
    repaint();
    juce::Timer::callAfterDelay (700, [safe = juce::Component::SafePointer<SampleBrowserPanel> (this)]
    {
        if (safe != nullptr)
        {
            safe->pathErrorTicks = 0;
            safe->pathEditor.setText (safe->currentDirectory.getFullPathName(), juce::dontSendNotification);
            safe->endPathEditing (false);
            safe->updateListThemeColours();
            safe->repaint();
        }
    });
}

void SampleBrowserPanel::setSearchInputMode (bool shouldSearch)
{
    if (shouldSearch)
    {
        if (searchInputMode)
        {
            searchEditor.grabKeyboardFocus();
            return;
        }

        if (pathEditorActive)
            endPathEditing (true);

        searchInputMode = true;
        pathDisplay.setVisible (false);
        pathEditor.setVisible (false);
        searchEditor.setVisible (true);
        searchToggleButton.setToggleState (true, juce::dontSendNotification);
        if (! pathSearchRow.isEmpty())
            layoutPathSearchRow (pathSearchRow);
        searchEditor.toFront (false);
        searchEditor.grabKeyboardFocus();
        searchToggleButton.repaint();
        onSearchTextChanged();   // reflect any existing text (usually empty on first open)
    }
    else
    {
        // refreshFiles() performs the full exit-to-path-mode reset.
        refreshFiles();
    }
}

void SampleBrowserPanel::onSearchTextChanged()
{
    searchQuery = searchEditor.getText().trim();
    clearSearchButton.setVisible (searchInputMode && searchEditor.getText().isNotEmpty());

    if (searchQuery.isEmpty())
    {
        searchMode = false;
        searchScanning = false;
        searchTruncated = false;
        searcher.cancel();
        populateCurrentDirectoryFiles();   // stay in search field, show current directory
        updateSearchStatusLabel();
        return;
    }

    searchMode = true;
    searchScanning = true;
    updateSearchStatusLabel();

    const int gen = ++debounceGeneration;
    juce::Timer::callAfterDelay (180, [safe = juce::Component::SafePointer<SampleBrowserPanel> (this), gen]
    {
        if (safe != nullptr && safe->debounceGeneration == gen && safe->searchMode)
            safe->launchSearch();
    });
}

void SampleBrowserPanel::launchSearch()
{
    if (! searchMode || searchQuery.isEmpty())
        return;

    searchScanning = true;
    updateSearchStatusLabel();
    searcher.search (currentDirectory, searchQuery);
}

void SampleBrowserPanel::applySearchResults (const DirectorySearch::Result& result)
{
    // Ignore stale deliveries: mode changed, or a newer query is already in the box.
    if (! searchMode || result.query != searchQuery)
        return;

    searchScanning = false;
    searchTruncated = result.truncated;

    files.clear();
    files.reserve (result.matches.size());
    for (const auto& m : result.matches)
        files.push_back ({ m.file, m.directory, m.audio, m.relativePath });

    fileList.deselectAllRows();
    fileList.updateContent();
    fileList.repaint();
    updateSearchStatusLabel();
}

void SampleBrowserPanel::updateSearchStatusLabel()
{
    if (! searchMode)
    {
        searchStatusLabel.setVisible (false);
        return;
    }

    juce::String text;
    bool bottomStrip = false;

    if (files.empty())
    {
        text = searchScanning ? kSearchingLabel
                              : "No matches for \"" + searchQuery + "\"";
    }
    else if (searchTruncated)
    {
        text = kTruncatedLabel;
        bottomStrip = true;
    }
    else
    {
        searchStatusLabel.setVisible (false);
        return;
    }

    searchStatusLabel.setText (text, juce::dontSendNotification);
    searchStatusLabel.setColour (juce::Label::textColourId,
                                 getTheme().text0.withAlpha (bottomStrip ? 0.9f : 0.62f));
    searchStatusLabel.setColour (juce::Label::backgroundColourId,
                                 bottomStrip ? getTheme().surface1.withAlpha (0.92f)
                                             : juce::Colours::transparentBlack);

    if (bottomStrip)
    {
        auto strip = fileSectionBounds;
        searchStatusLabel.setBounds (strip.removeFromBottom (18));
    }
    else
    {
        searchStatusLabel.setBounds (fileSectionBounds);
    }

    searchStatusLabel.setVisible (true);
    searchStatusLabel.toFront (false);
}

void SampleBrowserPanel::refreshThemeColours()
{
    updateListThemeColours();
    titleLabel.repaint();
    pathDisplay.repaint();
    pathEditor.repaint();
    searchEditor.repaint();
    searchToggleButton.repaint();
    clearSearchButton.repaint();
    searchStatusLabel.repaint();
    for (auto* button : { &backButton, &forwardButton, &upButton, &refreshButton })
        button->repaint();
    locationList.repaint();
    fileList.repaint();
    repaint();
}

void SampleBrowserPanel::beginPathEditing()
{
    if (pathEditorActive)
        return;

    pathEditorActive = true;
    pathEditor.setText (currentDirectory.getFullPathName(), juce::dontSendNotification);
    updateListThemeColours();
    pathEditor.setVisible (true);
    pathEditor.toFront (false);
    pathEditor.grabKeyboardFocus();
    pathEditor.selectAll();
    pathDisplay.repaint();
}

void SampleBrowserPanel::endPathEditing (bool resetTextToCurrentDirectory)
{
    if (resetTextToCurrentDirectory)
        pathEditor.setText (currentDirectory.getFullPathName(), juce::dontSendNotification);

    pathEditorActive = false;
    pathEditor.setVisible (false);
    pathDisplay.repaint();
}

void SampleBrowserPanel::updateListThemeColours()
{
    titleLabel.setFont (IntersectLookAndFeel::makeFont (10.5f, true));
    titleLabel.setColour (juce::Label::textColourId, getTheme().text2.withAlpha (0.86f));

    const auto pathWarning = getTheme().color5;
    pathEditor.setColour (juce::TextEditor::backgroundColourId,
                           pathErrorTicks > 0 ? getTheme().surface1.interpolatedWith (pathWarning, 0.18f).withAlpha (0.94f)
                                              : getTheme().surface1.withAlpha (0.92f));
    pathEditor.setColour (juce::TextEditor::outlineColourId,
                           pathErrorTicks > 0 ? pathWarning.withAlpha (0.8f) : getTheme().surface4.withAlpha (0.92f));
    pathEditor.setColour (juce::TextEditor::focusedOutlineColourId,
                           pathErrorTicks > 0 ? pathWarning.withAlpha (0.9f) : getTheme().accent.withAlpha (0.85f));
    pathEditor.setColour (juce::TextEditor::textColourId, getTheme().text2);
    pathEditor.setColour (juce::TextEditor::highlightColourId, getTheme().accent.withAlpha (0.35f));

    searchEditor.setColour (juce::TextEditor::backgroundColourId, getTheme().surface1.withAlpha (0.92f));
    searchEditor.setColour (juce::TextEditor::outlineColourId, getTheme().surface4.withAlpha (0.92f));
    searchEditor.setColour (juce::TextEditor::focusedOutlineColourId, getTheme().accent.withAlpha (0.85f));
    searchEditor.setColour (juce::TextEditor::textColourId, getTheme().text2);
    searchEditor.setColour (juce::TextEditor::highlightColourId, getTheme().accent.withAlpha (0.35f));
    searchEditor.setTextToShowWhenEmpty (kSearchPlaceholder, getTheme().text0.withAlpha (0.55f));

    for (auto* button : { &backButton, &forwardButton, &upButton, &refreshButton })
    {
        button->setColour (juce::TextButton::buttonColourId,
                           (button->isMouseOverOrDragging() ? getTheme().surface5 : getTheme().surface4).withAlpha (0.95f));
        button->setColour (juce::TextButton::textColourOnId, getTheme().text2.withAlpha (0.88f));
        button->setColour (juce::TextButton::textColourOffId, getTheme().text2.withAlpha (0.88f));
    }

    for (juce::Button* button : { static_cast<juce::Button*> (&searchToggleButton),
                                  static_cast<juce::Button*> (&clearSearchButton) })
    {
        button->setColour (juce::TextButton::buttonColourId, getTheme().surface4.withAlpha (0.95f));
        button->setColour (juce::TextButton::textColourOnId, getTheme().accent.withAlpha (0.95f));
        button->setColour (juce::TextButton::textColourOffId, getTheme().text2.withAlpha (0.88f));
    }

    locationList.setColour (juce::ListBox::backgroundColourId, getTheme().surface0);
    fileList.setColour (juce::ListBox::backgroundColourId, getTheme().surface0);

    const auto thumb = getTheme().accent.withAlpha (0.78f);
    const auto track = getTheme().surface4.withAlpha (0.45f);
    for (auto* list : { &locationList, &fileList })
    {
        for (auto* bar : { &list->getVerticalScrollBar(), &list->getHorizontalScrollBar() })
        {
            bar->setColour (juce::ScrollBar::thumbColourId, thumb);
            bar->setColour (juce::ScrollBar::trackColourId, track);
            bar->setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
        }
    }
}

void SampleBrowserPanel::updateSplitterCursor (juce::Point<int> position)
{
    const bool shouldHover = splitterBounds.expanded (0, 3).contains (position);
    if (splitterHover == shouldHover)
        return;

    splitterHover = shouldHover;
    setMouseCursor (splitterHover ? juce::MouseCursor::UpDownResizeCursor
                                  : juce::MouseCursor::NormalCursor);
    repaint();
}

int SampleBrowserPanel::getDefaultLocationSectionHeight (const juce::Rectangle<int>& area) const
{
    return juce::jmin (area.getHeight() / 2, 176);
}

int SampleBrowserPanel::clampLocationSectionHeight (int height, const juce::Rectangle<int>& area) const
{
    const int minLocationH = 70;
    const int minFilesH = 88;
    const int splitterH = 6;
    const int maxLocationH = juce::jmax (minLocationH, area.getHeight() - splitterH - minFilesH);
    return juce::jlimit (minLocationH, maxLocationH, height);
}

bool SampleBrowserPanel::isSupportedAudioFile (const juce::File& file) const
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".ogg" || ext == ".aiff"
        || ext == ".aif" || ext == ".flac" || ext == ".mp3";
}

juce::String SampleBrowserPanel::locationPrefixForKind (LocationKind kind) const
{
    switch (kind)
    {
        case LocationKind::drive: return "D";
        case LocationKind::bookmark: return "*";
        case LocationKind::section: break;
    }
    return {};
}

juce::String SampleBrowserPanel::formatFileMeta (const FileRow& row) const
{
    if (row.directory)
        return "DIR";

    const auto bytes = row.file.getSize();
    if (bytes >= 1024 * 1024)
        return juce::String (bytes / (1024 * 1024)) + "M";
    if (bytes >= 1024)
        return juce::String (bytes / 1024) + "K";
    return juce::String (bytes) + "B";
}
