#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BrowserTypes.h"
#include <functional>
#include <vector>

class FileInfoCache;

/**
    The browser's file list: a NAME / LEN / RATE table with sortable columns, multi-select,
    drag-out, and the keyboard model for moving through folders. Folders always sort first.
    LEN and RATE come from FileInfoCache as rows are painted.
*/
class FileTable : public juce::Component,
                  private juce::ListBoxModel,
                  private juce::ScrollBar::Listener
{
public:
    explicit FileTable (FileInfoCache& infoCache);

    /** Replaces the listing. Applies the current sort and clears the selection. */
    void setRows (std::vector<Browser::FileRow> newRows);
    const std::vector<Browser::FileRow>& getRows() const noexcept { return rows; }

    std::vector<Browser::FileRow> getSelectedRows() const;
    /** The row the user last selected, or nullptr. Drives the preview pane. */
    const Browser::FileRow* getFocusedRow() const;
    bool selectFile (const juce::File& file);
    void selectFirstRow();

    void setFullPathTooltips (bool shouldShow) noexcept { fullPathTooltips = shouldShow; }
    /** Overlay text over the list (e.g. search progress). Empty hides it. A strip sits at the bottom. */
    void setStatus (const juce::String& text, bool asBottomStrip);
    /** Call when FileInfoCache has new results: repaints, and re-sorts when sorting by LEN or RATE. */
    void infoUpdated();
    void grabListFocus();
    void refreshThemeColours();

    /** Shows an inline name field over the file's row (presets only), prefilled with initialName
        or the current name, all selected. Return / clicking away commits, Esc cancels. */
    void beginRename (const juce::File& file, const juce::String& initialName = {});
    bool isRenaming() const noexcept { return renaming; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void()> onSelectionChanged;
    std::function<void (const Browser::FileRow&, bool shiftDown)> onRowActivated;
    std::function<void (const Browser::FileRow&, juce::Point<int> screenPos)> onRowMenu;
    std::function<void()> onGoUp;
    /** Right arrow on an audio row: play its preview from the start. */
    std::function<void (const Browser::FileRow&)> onRetrigger;
    /** Checked before an inline rename closes. Return false to refuse the name (the callback
        explains why to the user); the field then stays open (Return) or cancels (clicking away). */
    std::function<bool (const juce::File&, const juce::String&)> canRename;
    /** Inline rename accepted with a new name. */
    std::function<void (const juce::File&, const juce::String&)> onRenameRequested;

private:
    enum class SortColumn
    {
        name,
        length,
        rate,
    };

    struct Columns
    {
        juce::Rectangle<int> icon, name, length, rate;
    };

    class Header : public juce::Component
    {
    public:
        explicit Header (FileTable& ownerIn) : owner (ownerIn) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;

    private:
        FileTable& owner;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override;
    void returnKeyPressed (int lastRowSelected) override;
    void deleteKeyPressed (int lastRowSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rowsToDescribe) override;
    juce::String getTooltipForRow (int row) override;

    static Columns layoutColumns (juce::Rectangle<int> rowBounds);
    void setSort (SortColumn column);
    void sortRows (bool keepSelection);
    void requestInfoForAllRows();
    enum class RenameEnd
    {
        commit,          // Return: a refused name keeps the field open
        commitIfValid,   // clicking away / scrolling / re-listing: a refused name cancels
        cancel,          // Esc
    };
    void finishRename (RenameEnd end);
    void scrollBarMoved (juce::ScrollBar* bar, double newRangeStart) override;

    FileInfoCache& info;
    Header header { *this };
    juce::ListBox list { "browser files", this };
    juce::Label statusLabel;

    std::vector<Browser::FileRow> rows;
    SortColumn sortColumn = SortColumn::name;
    bool sortAscending = true;
    bool fullPathTooltips = false;
    bool statusAsStrip = false;

    // Inline rename. The editor is a permanent child that is only shown and hidden, never
    // destroyed from its own callbacks (see the TextEditor lifecycle note in CLAUDE.md).
    juce::TextEditor renameEditor;
    juce::File renamingFile;
    bool renaming = false;
    double renameScrollPosition = 0.0;   // scroll notifications arrive async; only a real move commits

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileTable)
};
