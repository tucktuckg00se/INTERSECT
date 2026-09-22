#include "FileTable.h"
#include "BrowserIcons.h"
#include "FileInfoCache.h"
#include "../IntersectLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace
{
constexpr int kHeaderH = 16;
constexpr int kRowH = 20;
constexpr int kLengthW = 50;
constexpr int kRateW = 42;
constexpr int kIconW = 14;
}

FileTable::FileTable (FileInfoCache& infoCache) : info (infoCache)
{
    addAndMakeVisible (header);

    list.setRowHeight (kRowH);
    list.setMultipleSelectionEnabled (true);
    list.setClickingTogglesRowSelection (false);
    list.setWantsKeyboardFocus (true);
    addAndMakeVisible (list);

    renameEditor.setMultiLine (false);
    renameEditor.setReturnKeyStartsNewLine (false);
    renameEditor.setScrollbarsShown (false);
    renameEditor.setIndents (4, 0);
    renameEditor.setJustification (juce::Justification::centredLeft);
    renameEditor.setFont (IntersectLookAndFeel::makeFont (9.5f));
    renameEditor.onReturnKey = [this] { finishRename (RenameEnd::commit); };
    renameEditor.onEscapeKey = [this] { finishRename (RenameEnd::cancel); };
    renameEditor.onFocusLost = [this]
    {
        // Focus-loss notifications arrive asynchronously; ignore one that is stale because the
        // field already has focus again.
        if (! renameEditor.hasKeyboardFocus (true))
            finishRename (RenameEnd::commitIfValid);
    };
    addChildComponent (renameEditor);
    list.getVerticalScrollBar().addListener (this);

    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setInterceptsMouseClicks (false, false);
    statusLabel.setFont (IntersectLookAndFeel::makeFont (10.0f));
    addChildComponent (statusLabel);

    refreshThemeColours();
}

FileTable::Columns FileTable::layoutColumns (juce::Rectangle<int> bounds)
{
    Columns c;
    bounds = bounds.reduced (6, 0);
    c.icon = bounds.removeFromLeft (kIconW);
    bounds.removeFromLeft (6);
    c.rate = bounds.removeFromRight (kRateW);
    c.length = bounds.removeFromRight (kLengthW);
    bounds.removeFromRight (6);
    c.name = bounds;
    return c;
}

void FileTable::setRows (std::vector<Browser::FileRow> newRows)
{
    finishRename (RenameEnd::commitIfValid);   // the row under the field is about to change
    rows = std::move (newRows);
    sortRows (false);
    if (sortColumn != SortColumn::name)
        requestInfoForAllRows();

    list.deselectAllRows();
    list.updateContent();
    list.setVerticalPosition (0.0);
    list.repaint();
}

std::vector<Browser::FileRow> FileTable::getSelectedRows() const
{
    std::vector<Browser::FileRow> selected;
    const auto ranges = list.getSelectedRows();
    for (int i = 0; i < ranges.size(); ++i)
        if (const int row = ranges[i]; row >= 0 && row < (int) rows.size())
            selected.push_back (rows[(size_t) row]);
    return selected;
}

const Browser::FileRow* FileTable::getFocusedRow() const
{
    const int row = list.getLastRowSelected();
    if (row < 0 || row >= (int) rows.size() || ! list.isRowSelected (row))
        return nullptr;
    return &rows[(size_t) row];
}

bool FileTable::selectFile (const juce::File& file)
{
    for (int row = 0; row < (int) rows.size(); ++row)
    {
        if (Browser::samePath (rows[(size_t) row].file, file))
        {
            list.selectRow (row);
            list.scrollToEnsureRowIsOnscreen (row);
            return true;
        }
    }
    return false;
}

void FileTable::selectFirstRow()
{
    if (! rows.empty())
        list.selectRow (0);
}

void FileTable::setStatus (const juce::String& text, bool asBottomStrip)
{
    statusAsStrip = asBottomStrip;
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setVisible (text.isNotEmpty());
    refreshThemeColours();
    resized();
}

void FileTable::infoUpdated()
{
    if (sortColumn != SortColumn::name)
        sortRows (true);
    list.repaint();
}

void FileTable::grabListFocus()
{
    list.grabKeyboardFocus();
}

void FileTable::beginRename (const juce::File& file, const juce::String& initialName)
{
    finishRename (RenameEnd::commitIfValid);
    if (! AppFiles::isPresetFile (file) || ! selectFile (file))
        return;

    const int row = list.getLastRowSelected();
    list.scrollToEnsureRowIsOnscreen (row);
    const auto rowBounds = list.getRowPosition (row, true).translated (list.getX(), list.getY());
    const auto cols = layoutColumns (rowBounds.reduced (2, 1));

    renamingFile = file;
    renaming = true;
    renameScrollPosition = list.getVerticalPosition();
    renameEditor.setBounds (cols.name.getUnion (cols.length).withTrimmedLeft (-4).reduced (0, 1));
    renameEditor.setText (initialName.isNotEmpty() ? initialName : file.getFileNameWithoutExtension(),
                          juce::dontSendNotification);
    renameEditor.setVisible (true);
    renameEditor.toFront (true);
    renameEditor.grabKeyboardFocus();
    renameEditor.selectAll();
}

// Called from the TextEditor's own callbacks: the name is checked first so a refused name never
// hides and re-opens the field; after that, hiding and reporting happen on the next message-loop
// turn so nothing is torn down while the callback is still running.
void FileTable::finishRename (RenameEnd end)
{
    if (! renaming)
        return;

    const auto file = renamingFile;
    const auto text = renameEditor.getText();
    bool accept = end != RenameEnd::cancel;

    if (accept && canRename != nullptr && ! canRename (file, text))
    {
        if (end == RenameEnd::commit)
        {
            renameEditor.grabKeyboardFocus();
            renameEditor.selectAll();
            return;   // keep editing
        }
        accept = false;
    }

    renaming = false;
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FileTable> (this), file, text, accept]
    {
        if (safe == nullptr)
            return;

        if (! safe->renaming)   // a new rename may already have opened the field again
        {
            safe->renameEditor.setVisible (false);
            safe->grabListFocus();
        }
        if (accept && safe->onRenameRequested != nullptr)
            safe->onRenameRequested (file, text);
    });
}

void FileTable::scrollBarMoved (juce::ScrollBar*, double)
{
    // The field would no longer sit over its row. Ignore the (async) notification for the scroll
    // that beginRename itself did to bring the row into view.
    if (renaming && std::abs (list.getVerticalPosition() - renameScrollPosition) > 1.0e-6)
        finishRename (RenameEnd::commitIfValid);
}

void FileTable::refreshThemeColours()
{
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    for (auto* bar : { &list.getVerticalScrollBar(), &list.getHorizontalScrollBar() })
    {
        bar->setColour (juce::ScrollBar::thumbColourId, getTheme().accent.withAlpha (0.7f));
        bar->setColour (juce::ScrollBar::trackColourId, getTheme().surface4.withAlpha (0.45f));
        bar->setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    }

    renameEditor.setColour (juce::TextEditor::backgroundColourId, getTheme().surface1);
    renameEditor.setColour (juce::TextEditor::outlineColourId, getTheme().accent.withAlpha (0.85f));
    renameEditor.setColour (juce::TextEditor::focusedOutlineColourId, getTheme().accent);
    renameEditor.setColour (juce::TextEditor::textColourId, getTheme().text2);
    renameEditor.setColour (juce::TextEditor::highlightColourId, getTheme().accent.withAlpha (0.35f));

    statusLabel.setColour (juce::Label::textColourId,
                           statusAsStrip ? getTheme().text2.withAlpha (0.9f) : getTheme().text0.withAlpha (0.7f));
    statusLabel.setColour (juce::Label::backgroundColourId,
                           statusAsStrip ? getTheme().surface1.withAlpha (0.95f) : juce::Colours::transparentBlack);
    header.repaint();
    list.repaint();
    repaint();
}

void FileTable::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().surface0);
}

void FileTable::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (kHeaderH));
    list.setBounds (area);

    auto statusArea = list.getBounds();
    statusLabel.setBounds (statusAsStrip ? statusArea.removeFromBottom (18) : statusArea);
}

bool FileTable::keyPressed (const juce::KeyPress& key)
{
    const auto* focused = getFocusedRow();

    if (key.isKeyCode (juce::KeyPress::rightKey) && focused != nullptr)
    {
        // Folders open; audio restarts its preview like a one-shot (never pauses).
        if (focused->kind == Browser::FileKind::directory && onRowActivated != nullptr)
            onRowActivated (*focused, false);
        else if (focused->kind == Browser::FileKind::audio && onRetrigger != nullptr)
            onRetrigger (*focused);
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::F2Key) && focused != nullptr && focused->kind == Browser::FileKind::preset)
    {
        beginRename (focused->file);
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::backspaceKey))
    {
        if (onGoUp != nullptr)
            onGoUp();
        return true;
    }

    return false;
}

int FileTable::getNumRows()
{
    return (int) rows.size();
}

void FileTable::paintListBoxItem (int rowIndex, juce::Graphics& g, int width, int height, bool selected)
{
    if (rowIndex < 0 || rowIndex >= (int) rows.size())
        return;

    const auto& row = rows[(size_t) rowIndex];
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1);
    if (selected)
    {
        g.setColour (getTheme().surface3.interpolatedWith (getTheme().accent, 0.16f));
        g.fillRoundedRectangle (bounds.toFloat(), 3.0f);
    }

    const auto cols = layoutColumns (bounds);

    auto icon = BrowserIcons::Icon::audio;
    auto iconColour = getTheme().waveform.withAlpha (0.9f);
    if (row.kind == Browser::FileKind::directory)
    {
        icon = BrowserIcons::Icon::folder;
        iconColour = getTheme().color1.brighter (0.3f);
    }
    else if (row.kind == Browser::FileKind::preset)
    {
        icon = BrowserIcons::Icon::preset;
        iconColour = getTheme().accent;
    }
    BrowserIcons::draw (g, icon, cols.icon.toFloat().reduced (0.0f, 1.0f), iconColour);

    g.setFont (IntersectLookAndFeel::makeFont (9.5f));
    g.setColour (selected ? getTheme().text2 : getTheme().text2.withAlpha (0.86f));
    const auto name = row.kind == Browser::FileKind::preset ? row.file.getFileNameWithoutExtension()
                                                            : row.file.getFileName();
    g.drawText (name, cols.name, juce::Justification::centredLeft, true);

    if (row.kind == Browser::FileKind::directory)
        return;

    const auto* fileInfo = info.find (row.file);
    if (fileInfo == nullptr)
    {
        info.request (row.file);
        return;
    }

    g.setFont (IntersectLookAndFeel::makeFont (8.8f));
    g.setColour (getTheme().text1);
    if (row.kind == Browser::FileKind::preset)
    {
        if (fileInfo->presetSampleCount >= 0)
            g.drawText (juce::String (fileInfo->presetSampleCount) + " smp",
                        cols.length.getUnion (cols.rate), juce::Justification::centredRight, false);
        return;
    }

    if (! fileInfo->readable)
    {
        g.setColour (getTheme().text0.withAlpha (0.6f));
        g.drawText ("-", cols.length, juce::Justification::centredRight, false);
        return;
    }

    g.drawText (Browser::formatLength (fileInfo->lengthSeconds), cols.length, juce::Justification::centredRight, false);
    g.drawText (Browser::formatRate (fileInfo->sampleRate), cols.rate, juce::Justification::centredRight, false);
}

void FileTable::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && row >= 0 && row < (int) rows.size() && onRowMenu != nullptr)
        onRowMenu (rows[(size_t) row], e.getScreenPosition());
}

void FileTable::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) rows.size() || onRowActivated == nullptr)
        return;

    // The first click of a Shift+double-click range-selects, which is not what the user meant:
    // Shift+double-click loads just the row that was double-clicked.
    if (e.mods.isShiftDown())
        list.selectRow (row);

    onRowActivated (rows[(size_t) row], e.mods.isShiftDown());
}

void FileTable::returnKeyPressed (int lastRowSelected)
{
    if (lastRowSelected >= 0 && lastRowSelected < (int) rows.size() && onRowActivated != nullptr)
        onRowActivated (rows[(size_t) lastRowSelected], juce::ModifierKeys::currentModifiers.isShiftDown());
}

void FileTable::deleteKeyPressed (int)
{
    // ListBox routes both Delete and Backspace here when a row is selected; only Backspace navigates.
    if (juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::backspaceKey) && onGoUp != nullptr)
        onGoUp();
}

void FileTable::selectedRowsChanged (int)
{
    if (onSelectionChanged != nullptr)
        onSelectionChanged();
}

juce::var FileTable::getDragSourceDescription (const juce::SparseSet<int>& rowsToDescribe)
{
    std::vector<juce::File> files;
    for (int i = 0; i < rowsToDescribe.size(); ++i)
    {
        const int row = rowsToDescribe[i];
        if (row < 0 || row >= (int) rows.size())
            continue;

        const auto& item = rows[(size_t) row];
        if ((item.kind == Browser::FileKind::audio || item.kind == Browser::FileKind::preset)
            && item.file.existsAsFile())
            files.push_back (item.file);
    }
    return Browser::makeDragDescription (files);
}

juce::String FileTable::getTooltipForRow (int row)
{
    if (! fullPathTooltips || row < 0 || row >= (int) rows.size())
        return {};
    return rows[(size_t) row].file.getFullPathName();
}

void FileTable::setSort (SortColumn column)
{
    if (column == sortColumn)
        sortAscending = ! sortAscending;
    else
    {
        sortColumn = column;
        sortAscending = true;
    }

    if (sortColumn != SortColumn::name)
        requestInfoForAllRows();

    sortRows (true);
    header.repaint();
    list.repaint();
}

void FileTable::requestInfoForAllRows()
{
    for (const auto& row : rows)
        if (row.kind != Browser::FileKind::directory)
            info.request (row.file);
}

void FileTable::sortRows (bool keepSelection)
{
    std::set<juce::String> selectedPaths;
    juce::String focusedPath;
    if (keepSelection)
    {
        for (const auto& row : getSelectedRows())
            selectedPaths.insert (row.file.getFullPathName());
        if (const auto* focused = getFocusedRow())
            focusedPath = focused->file.getFullPathName();
    }

    // Rows without info yet (or presets) sort after rows that have a value, then by name.
    auto sortKey = [this] (const Browser::FileRow& row) -> double
    {
        if (row.kind != Browser::FileKind::audio)
            return -1.0;
        const auto* fileInfo = info.find (row.file);
        if (fileInfo == nullptr || ! fileInfo->readable)
            return -1.0;
        return sortColumn == SortColumn::length ? fileInfo->lengthSeconds : fileInfo->sampleRate;
    };

    std::stable_sort (rows.begin(), rows.end(), [&] (const Browser::FileRow& a, const Browser::FileRow& b)
    {
        const bool aDir = a.kind == Browser::FileKind::directory;
        const bool bDir = b.kind == Browser::FileKind::directory;
        if (aDir != bDir)
            return aDir;

        if (sortColumn != SortColumn::name && ! aDir)
        {
            const double ka = sortKey (a), kb = sortKey (b);
            if ((ka >= 0.0) != (kb >= 0.0))
                return ka >= 0.0;
            if (ka >= 0.0 && std::abs (ka - kb) > 1.0e-9)
                return sortAscending ? ka < kb : ka > kb;
        }

        const int byName = a.file.getFileName().compareNatural (b.file.getFileName());
        return (sortColumn != SortColumn::name || sortAscending) ? byName < 0 : byName > 0;
    });

    if (! keepSelection)
        return;

    juce::SparseSet<int> newSelection;
    int focusedRow = -1;
    for (int row = 0; row < (int) rows.size(); ++row)
    {
        const auto path = rows[(size_t) row].file.getFullPathName();
        if (selectedPaths.count (path) > 0)
            newSelection.addRange ({ row, row + 1 });
        if (path == focusedPath)
            focusedRow = row;
    }

    // Re-select the focused row last so it stays the "last selected" row that drives the preview.
    if (focusedRow >= 0)
        newSelection.removeRange ({ focusedRow, focusedRow + 1 });
    list.setSelectedRows (newSelection, juce::dontSendNotification);
    if (focusedRow >= 0)
        list.selectRow (focusedRow, true, false);
}

void FileTable::Header::paint (juce::Graphics& g)
{
    const auto cols = layoutColumns (getLocalBounds().reduced (2, 0));
    g.setFont (IntersectLookAndFeel::makeFont (8.0f, true));

    auto drawColumn = [&] (const juce::String& label, juce::Rectangle<int> area, SortColumn column, bool rightAligned)
    {
        const bool active = owner.sortColumn == column;
        g.setColour (active ? getTheme().text2.withAlpha (0.85f) : getTheme().text0.withAlpha (0.85f));
        const int textW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label)) + 1;
        auto textArea = rightAligned ? area.removeFromRight (textW) : area.removeFromLeft (textW);
        g.drawText (label, textArea, juce::Justification::centred, false);
        if (active)
        {
            const auto arrowArea = rightAligned ? textArea.translated (-10, 0).withWidth (8)
                                                : textArea.translated (textW + 2, 0).withWidth (8);
            BrowserIcons::drawSortArrow (g, arrowArea.toFloat(), owner.sortAscending, getTheme().accent);
        }
    };

    drawColumn ("NAME", cols.icon.getUnion (cols.name), SortColumn::name, false);
    drawColumn ("LEN", cols.length, SortColumn::length, true);
    drawColumn ("RATE", cols.rate, SortColumn::rate, true);

    g.setColour (getTheme().surface4.withAlpha (0.8f));
    g.fillRect (getLocalBounds().removeFromBottom (1));
}

void FileTable::Header::mouseDown (const juce::MouseEvent& e)
{
    const auto cols = layoutColumns (getLocalBounds().reduced (2, 0));
    if (e.x >= cols.rate.getX())
        owner.setSort (SortColumn::rate);
    else if (e.x >= cols.length.getX())
        owner.setSort (SortColumn::length);
    else
        owner.setSort (SortColumn::name);
}
