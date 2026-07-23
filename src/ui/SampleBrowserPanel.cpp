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
    refreshButton.onClick = [this] { refreshFiles(); };
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
    auto pathBounds = header.removeFromTop (22);
    pathDisplay.setBounds (pathBounds);
    pathEditor.setBounds (pathBounds);

    area.removeFromTop (5);
    if (locationSectionHeight <= 0)
        locationSectionHeight = getDefaultLocationSectionHeight (area);

    locationSectionHeight = clampLocationSectionHeight (locationSectionHeight, area);
    locationSectionBounds = area.removeFromTop (locationSectionHeight);
    splitterBounds = area.removeFromTop (6);
    fileSectionBounds = area;

    locationList.setBounds (locationSectionBounds);
    fileList.setBounds (fileSectionBounds);
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
    g.drawText (item.file.getFileName(), textBounds, juce::Justification::centredLeft, true);
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

void SampleBrowserPanel::refreshFiles()
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
                files.push_back ({ child, isDir, isAudio });
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

void SampleBrowserPanel::refreshThemeColours()
{
    updateListThemeColours();
    titleLabel.repaint();
    pathDisplay.repaint();
    pathEditor.repaint();
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

    for (auto* button : { &backButton, &forwardButton, &upButton, &refreshButton })
    {
        button->setColour (juce::TextButton::buttonColourId,
                           (button->isMouseOverOrDragging() ? getTheme().surface5 : getTheme().surface4).withAlpha (0.95f));
        button->setColour (juce::TextButton::textColourOnId, getTheme().text2.withAlpha (0.88f));
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
