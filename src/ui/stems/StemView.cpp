#include "StemView.h"
#include "../IntersectLookAndFeel.h"
#include "../../PluginProcessor.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr int kModelsW = 210;
constexpr int kSectionLabelH = 18;
constexpr int kModelRowH = 34;
constexpr int kDetailH = 142;
constexpr int kHeaderRowH = 20;
constexpr int kBottomBarH = 34;
constexpr int kChipH = 20;
constexpr int kChipGap = 4;
constexpr int kGroupHeaderH = 18;

juce::String separatorDot()
{
    return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
}

float measureTextWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getWidth();
}

juce::String formatBytes (juce::int64 bytes)
{
    if (bytes <= 0)
        return {};
    if (bytes >= 1000 * 1000 * 1000)
        return juce::String ((double) bytes / 1.0e9, 1) + " GB";
    return juce::String (juce::roundToInt ((double) bytes / 1.0e6)) + " MB";
}

// "~11 min per 3-min song": a song-length figure reads better than a real-time factor.
juce::String formatCpuTime (float realtimeFactor)
{
    const float minutes = 3.0f * realtimeFactor;
    if (minutes < 1.0f)
        return "under 1 min per 3-min song";
    return "~" + juce::String ((int) std::ceil (minutes)) + " min per 3-min song";
}

juce::String formatGpuAdvice (StemGpuAdvice advice)
{
    switch (advice)
    {
        case StemGpuAdvice::notNeeded:   return "not needed";
        case StemGpuAdvice::optional:    return "optional, faster";
        case StemGpuAdvice::recommended: return "recommended";
        case StemGpuAdvice::unknown:     break;
    }
    return {};
}

int getSystemRamGb()
{
    static const int ramGb = juce::roundToInt (juce::SystemStats::getMemorySizeInMegabytes() / 1024.0);
    return ramGb;
}

juce::String deviceDisplayValue (StemComputeDevice device)
{
    if (device != StemComputeDevice::gpu)
        return stemComputeDeviceToString (device);

    const auto providerName = getAvailableGpuProviderName();
    return providerName.isNotEmpty() ? "GPU (" + providerName + ")" : "GPU";
}

bool isStemJobActive (StemJobState state)
{
    return state == StemJobState::preparing || state == StemJobState::separating
        || state == StemJobState::writing || state == StemJobState::importing;
}

void styleButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, getTheme().surface4.withAlpha (0.95f));
    button.setColour (juce::TextButton::textColourOnId, getTheme().text2.withAlpha (0.88f));
    button.setColour (juce::TextButton::textColourOffId, getTheme().text2.withAlpha (0.88f));
}

void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
{
    g.setColour (getTheme().text0.withAlpha (0.85f));
    g.setFont (IntersectLookAndFeel::makeFont (8.0f, true));
    g.drawText (text.toUpperCase(), area, juce::Justification::centredLeft, true);
}
} // namespace

// ── Model list ──────────────────────────────────────────────────────────

class StemView::ModelList : public juce::Component
{
public:
    explicit ModelList (StemView& o) : owner (o) {}

    void updateSize (int width)
    {
        setSize (width, juce::jmax (1, (int) owner.catalog.size() * kModelRowH));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto downloadProgress = owner.processor.getUiSliceSnapshot().stemDownloadProgress;

        for (int i = 0; i < (int) owner.catalog.size(); ++i)
        {
            const auto& entry = owner.catalog[(size_t) i];
            auto row = juce::Rectangle<int> (0, i * kModelRowH, getWidth(), kModelRowH).reduced (4, 1);
            const bool selected = i == owner.selectedModelIndex;

            if (selected)
            {
                g.setColour (getTheme().surface3.interpolatedWith (getTheme().accent, 0.14f));
                g.fillRoundedRectangle (row.toFloat(), 3.0f);
            }

            auto content = row.reduced (8, 4);
            const bool installed = owner.isInstalled (entry.id);

            juce::String subtitle;
            subtitle << entry.getNumOutputs() << (entry.getNumOutputs() == 1 ? " stem" : " stems");
            if (owner.isDownloading (entry.id))
                subtitle << separatorDot() << "Downloading " << juce::roundToInt (downloadProgress * 100.0f) << "%";
            else if (installed)
                subtitle << separatorDot() << "Installed";
            else if (const auto size = formatBytes (entry.downloadBytes); size.isNotEmpty())
                subtitle << separatorDot() << size;
            else
                subtitle << separatorDot() << "Not installed";

            g.setColour (selected ? getTheme().text2 : getTheme().text2.withAlpha (installed ? 0.82f : 0.6f));
            g.setFont (IntersectLookAndFeel::makeFont (9.5f, selected));
            g.drawText (entry.menuLabel, content.removeFromTop (13), juce::Justification::centredLeft, true);

            g.setColour (installed ? getTheme().accent.withAlpha (0.8f) : getTheme().text0);
            g.setFont (IntersectLookAndFeel::makeFont (8.0f));
            g.drawText (subtitle, content, juce::Justification::centredLeft, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int index = e.getPosition().y / kModelRowH;
        if (index >= 0 && index < (int) owner.catalog.size())
            owner.selectModel (index);
    }

private:
    StemView& owner;
};

// ── Grouped stem checklist ──────────────────────────────────────────────

class StemView::StemChecklist : public juce::Component
{
public:
    explicit StemChecklist (StemView& o) : owner (o) {}

    void setFilter (const juce::String& text)
    {
        filter = text.trim();
        rebuild();
    }

    void rebuild()
    {
        groups.clear();
        if (const auto* entry = owner.getSelectedModel())
        {
            for (int i = 0; i < entry->getNumOutputs(); ++i)
            {
                const auto& output = entry->outputs[(size_t) i];
                if (! matchesFilter (output))
                    continue;

                const auto groupName = output.group.isNotEmpty() ? output.group : juce::String ("Stems");
                auto group = std::find_if (groups.begin(), groups.end(),
                                           [&groupName] (const Group& g) { return g.name == groupName; });
                if (group == groups.end())
                {
                    groups.push_back ({ groupName, {}, {}, {}, {} });
                    group = std::prev (groups.end());
                }
                group->items.push_back ({ i, {} });
            }

            // The manifest's group order first, then the rest in output order.
            const auto rank = [entry] (const Group& g)
            {
                const int index = entry->groupOrder.indexOf (g.name);
                return index >= 0 ? index : entry->groupOrder.size();
            };
            std::stable_sort (groups.begin(), groups.end(),
                              [&rank] (const Group& a, const Group& b) { return rank (a) < rank (b); });
        }

        layout (getWidth());
    }

    /** Visible output indices (those matching the search). */
    std::vector<int> getVisibleOutputs() const
    {
        std::vector<int> visible;
        for (const auto& group : groups)
            for (const auto& item : group.items)
                visible.push_back (item.outputIndex);
        return visible;
    }

    void layout (int width)
    {
        const auto font = IntersectLookAndFeel::makeFont (9.0f);
        int y = 2;
        for (auto& group : groups)
        {
            group.header = { 0, y, width, kGroupHeaderH };
            const int nameW = juce::roundToInt (measureTextWidth (IntersectLookAndFeel::makeFont (8.0f, true),
                                                                  group.name.toUpperCase())) + 12;
            group.allHit = { nameW, y, 26, kGroupHeaderH };
            group.noneHit = { nameW + 28, y, 34, kGroupHeaderH };
            y += kGroupHeaderH;

            int x = 0;
            for (auto& item : group.items)
            {
                const auto& output = owner.getSelectedModel()->outputs[(size_t) item.outputIndex];
                const int chipW = juce::jlimit (64, 170, juce::roundToInt (measureTextWidth (font, output.label)) + 28);
                if (x > 0 && x + chipW > width)
                {
                    x = 0;
                    y += kChipH + kChipGap;
                }
                item.bounds = { x, y, chipW, kChipH };
                x += chipW + kChipGap;
            }
            y += kChipH + 10;
        }

        setSize (width, juce::jmax (1, y));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto* entry = owner.getSelectedModel();
        if (entry == nullptr)
            return;

        if (groups.empty())
        {
            g.setColour (getTheme().text0);
            g.setFont (IntersectLookAndFeel::makeFont (9.5f));
            g.drawText ("No stems match \"" + filter + "\"", getLocalBounds().withHeight (30),
                        juce::Justification::centredLeft, true);
            return;
        }

        const auto& chosen = owner.selectionForSelectedModel();
        for (const auto& group : groups)
        {
            drawSectionLabel (g, group.name, group.header);
            g.setFont (IntersectLookAndFeel::makeFont (8.0f));
            g.setColour (getTheme().text1);
            g.drawText ("all", group.allHit, juce::Justification::centred);
            g.drawText ("none", group.noneHit, juce::Justification::centred);

            for (const auto& item : group.items)
            {
                const auto& output = entry->outputs[(size_t) item.outputIndex];
                const bool on = chosen.count (output.name) > 0;
                auto chip = item.bounds.toFloat();

                g.setColour (on ? getTheme().accent.withAlpha (0.8f) : getTheme().surface4);
                g.fillRoundedRectangle (chip, 3.0f);

                auto box = item.bounds.withWidth (kChipH).reduced (5).toFloat();
                g.setColour (on ? getTheme().surface1 : getTheme().text1);
                g.drawRoundedRectangle (box, 1.5f, 1.0f);
                if (on)
                    g.fillRoundedRectangle (box.reduced (2.0f), 1.0f);

                g.setFont (IntersectLookAndFeel::makeFont (9.0f, on));
                g.setColour (on ? getTheme().surface1 : getTheme().text2);
                g.drawText (output.label, item.bounds.withTrimmedLeft (kChipH).withTrimmedRight (4),
                            juce::Justification::centredLeft, true);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* entry = owner.getSelectedModel();
        if (entry == nullptr)
            return;

        auto& chosen = owner.selectionForSelectedModel();
        const auto pos = e.getPosition();
        for (const auto& group : groups)
        {
            const bool all = group.allHit.contains (pos);
            if (all || group.noneHit.contains (pos))
            {
                for (const auto& item : group.items)
                {
                    const auto& name = entry->outputs[(size_t) item.outputIndex].name;
                    if (all)
                        chosen.insert (name);
                    else
                        chosen.erase (name);
                }
                owner.selectionChanged();
                return;
            }

            for (const auto& item : group.items)
            {
                if (! item.bounds.contains (pos))
                    continue;

                const auto& name = entry->outputs[(size_t) item.outputIndex].name;
                if (chosen.count (name) > 0)
                    chosen.erase (name);
                else
                    chosen.insert (name);
                owner.selectionChanged();
                return;
            }
        }
    }

private:
    struct Item
    {
        int outputIndex = 0;
        juce::Rectangle<int> bounds;
    };

    struct Group
    {
        juce::String name;
        juce::Rectangle<int> header, allHit, noneHit;
        std::vector<Item> items;
    };

    bool matchesFilter (const StemOutputDesc& output) const
    {
        return filter.isEmpty()
            || output.label.containsIgnoreCase (filter)
            || output.name.containsIgnoreCase (filter)
            || output.group.containsIgnoreCase (filter);
    }

    StemView& owner;
    juce::String filter;
    std::vector<Group> groups;
};

// ── StemView ────────────────────────────────────────────────────────────

StemView::StemView (IntersectProcessor& p)
    : processor (p)
{
    setWantsKeyboardFocus (true);

    modelList = std::make_unique<ModelList> (*this);
    checklist = std::make_unique<StemChecklist> (*this);

    modelViewport.setViewedComponent (modelList.get(), false);
    modelViewport.setScrollBarsShown (true, false);
    modelViewport.setScrollBarThickness (6);
    addAndMakeVisible (modelViewport);

    stemViewport.setViewedComponent (checklist.get(), false);
    stemViewport.setScrollBarsShown (true, false);
    stemViewport.setScrollBarThickness (6);
    addAndMakeVisible (stemViewport);

    searchEditor.setJustification (juce::Justification::centredLeft);
    searchEditor.setIndents (6, 0);
    searchEditor.setMultiLine (false);
    searchEditor.setReturnKeyStartsNewLine (false);
    searchEditor.setScrollbarsShown (false);
    searchEditor.setFont (IntersectLookAndFeel::makeFont (9.0f));
    searchEditor.onTextChange = [this] { checklist->setFilter (searchEditor.getText()); };
    searchEditor.onEscapeKey = [this]
    {
        // Only clears text; the editor itself stays alive (see CLAUDE.md TextEditor lifecycle).
        if (searchEditor.getText().isNotEmpty())
            searchEditor.clear();
        else
            grabKeyboardFocus();
        checklist->setFilter ({});
    };
    addAndMakeVisible (searchEditor);

    for (auto* button : { &allButton, &noneButton, &downloadButton, &browseButton, &startButton, &closeButton })
    {
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (*button);
    }

    allButton.setTooltip ("Select every stem shown");
    noneButton.setTooltip ("Clear the stems shown");
    browseButton.setTooltip ("Choose an output folder");
    closeButton.setTooltip ("Close (Esc)");

    auto setVisibleSelection = [this] (bool select)
    {
        const auto* entry = getSelectedModel();
        if (entry == nullptr)
            return;

        auto& chosen = selectionForSelectedModel();
        for (int index : checklist->getVisibleOutputs())
        {
            const auto& name = entry->outputs[(size_t) index].name;
            if (select)
                chosen.insert (name);
            else
                chosen.erase (name);
        }
        selectionChanged();
    };
    allButton.onClick = [setVisibleSelection] { setVisibleSelection (true); };
    noneButton.onClick = [setVisibleSelection] { setVisibleSelection (false); };

    downloadButton.onClick = [this]
    {
        const auto* entry = getSelectedModel();
        if (entry == nullptr)
            return;

        if (isDownloading (entry->id))
            processor.cancelStemModelDownload();
        else
            processor.startStemModelDownload ({ entry->id });
        updateControls();
    };

    browseButton.onClick = [this] { chooseOutputFolder(); };
    startButton.onClick = [this] { startSeparation(); };
    closeButton.onClick = [this]
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
    };

    deviceCell.label = "DEVICE";
    modeCell.label = "MODE";
    outputCell.label = "OUTPUT";
    outputCell.value = "Beside sample";

    refreshThemeColours();
}

StemView::~StemView() = default;

void StemView::openForSample (int sampleId)
{
    targetSampleId = sampleId;
    ortSupported = INTERSECT_HAS_ONNX_RUNTIME;
    ortInstalled = ortSupported && processor.getActiveOrtBundleDirectoryName().isNotEmpty();

    device = getAvailableGpuProviderName().isNotEmpty() ? processor.getStemComputeDevice()
                                                        : StemComputeDevice::cpu;
    updateDeviceCell();
    modeCell.value = stemExportModeToString (exportMode);

    reloadCatalog();
    updateControls();
    repaint();
}

void StemView::reloadCatalog()
{
    const auto previousId = getSelectedModel() != nullptr ? getSelectedModel()->id : lastModelId;

    const auto modelFolder = processor.getResolvedStemModelFolder();
    catalog = getEffectiveStemModelCatalog (modelFolder);
    installedModels = scanInstalledStemModels (modelFolder);

    // Keep the previous choice; otherwise prefer an installed model.
    int index = -1;
    for (int i = 0; i < (int) catalog.size() && index < 0; ++i)
        if (catalog[(size_t) i].id == previousId)
            index = i;
    for (int i = 0; i < (int) catalog.size() && index < 0; ++i)
        if (isInstalled (catalog[(size_t) i].id))
            index = i;
    if (index < 0 && ! catalog.empty())
        index = 0;

    selectedModelIndex = -1;   // force selectModel to rebuild
    modelList->updateSize (modelViewport.getMaximumVisibleWidth());
    selectModel (index);
}

void StemView::selectModel (int index)
{
    if (index == selectedModelIndex)
        return;

    selectedModelIndex = index;
    if (const auto* entry = getSelectedModel())
        lastModelId = entry->id;

    checklist->rebuild();
    stemViewport.setViewPosition (0, 0);
    updateControls();
    modelList->repaint();
    repaint();
}

const StemModelCatalogEntry* StemView::getSelectedModel() const
{
    return selectedModelIndex >= 0 && selectedModelIndex < (int) catalog.size()
               ? &catalog[(size_t) selectedModelIndex]
               : nullptr;
}

bool StemView::isInstalled (const StemModelId& modelId) const
{
    return std::find (installedModels.begin(), installedModels.end(), modelId) != installedModels.end();
}

bool StemView::isDownloading (const StemModelId& modelId) const
{
    return processor.getUiSliceSnapshot().stemDownloadState == StemModelDownloadState::downloading
        && processor.getStemModelDownloadJob().getCurrentModelId() == modelId;
}

std::set<juce::String>& StemView::selectionForSelectedModel()
{
    static std::set<juce::String> none;
    if (const auto* entry = getSelectedModel())
        return selections[entry->id];
    none.clear();
    return none;
}

void StemView::selectionChanged()
{
    checklist->repaint();
    updateControls();
    repaint (headerArea);
}

juce::String StemView::getLastModelId() const
{
    if (const auto* entry = getSelectedModel())
        return entry->id;
    return lastModelId;
}

juce::StringArray StemView::getRememberedSelections() const
{
    juce::StringArray entries;
    for (const auto& [modelId, names] : selections)
    {
        if (names.empty())
            continue;

        juce::StringArray list;
        for (const auto& name : names)
            list.add (name);
        entries.add (modelId + "=" + list.joinIntoString (","));
    }
    return entries;
}

void StemView::setRememberedSelections (const juce::StringArray& entries)
{
    selections.clear();
    for (const auto& entry : entries)
    {
        const auto modelId = normaliseStemModelId (entry.upToFirstOccurrenceOf ("=", false, false));
        if (modelId.isEmpty())
            continue;

        auto& names = selections[modelId];
        for (const auto& name : juce::StringArray::fromTokens (entry.fromFirstOccurrenceOf ("=", false, false), ",", {}))
            if (name.trim().isNotEmpty())
                names.insert (name.trim());
    }
}

juce::String StemView::getTargetSampleName() const
{
    const auto& ui = processor.getUiSliceSnapshot();
    for (int i = 0; i < ui.numSessionSamples; ++i)
        if (ui.sessionSamples[(size_t) i].sampleId == targetSampleId)
            return ui.sessionSamples[(size_t) i].fileName.toString();
    return {};
}

juce::String StemView::getStartBlockReason() const
{
    if (! ortSupported)
        return "Stem separation is not available on this platform";
    if (! ortInstalled)
        return "Download an ONNX Runtime bundle in SET > Stem Separation > ONNX Runtime";

    const auto* entry = getSelectedModel();
    if (entry == nullptr)
        return "No stem models available";
    if (! isInstalled (entry->id))
        return "Download this model to use it";
    if (isStemJobActive (processor.getUiSliceSnapshot().stemJobState))
        return "A separation is already running";

    const auto it = selections.find (entry->id);
    const bool anySelected = it != selections.end()
                          && std::any_of (entry->outputs.begin(), entry->outputs.end(),
                                          [&it] (const StemOutputDesc& o) { return it->second.count (o.name) > 0; });
    if (! anySelected)
        return "Select at least one stem";

    return {};
}

void StemView::updateDeviceCell()
{
    deviceCell.enabled = getAvailableGpuProviderName().isNotEmpty();
    deviceCell.value = deviceDisplayValue (device);
}

void StemView::updateControls()
{
    const auto* entry = getSelectedModel();
    const bool installed = entry != nullptr && isInstalled (entry->id);
    const bool downloadingThis = entry != nullptr && isDownloading (entry->id);
    const bool anyDownload = processor.getUiSliceSnapshot().stemDownloadState == StemModelDownloadState::downloading;

    downloadButton.setVisible (entry != nullptr && ! installed && entry->downloadUrl.isNotEmpty());
    downloadButton.setButtonText (downloadingThis ? "CANCEL" : "DOWNLOAD");
    downloadButton.setEnabled (downloadingThis || ! anyDownload);

    const bool checklistUsable = ortSupported && ortInstalled && entry != nullptr;
    stemViewport.setVisible (checklistUsable);
    for (auto* c : std::initializer_list<juce::Component*> { &searchEditor, &allButton, &noneButton })
        c->setEnabled (checklistUsable);

    startButton.setEnabled (getStartBlockReason().isEmpty());
    startButton.setTooltip (getStartBlockReason());
    repaint();
}

void StemView::startSeparation()
{
    const auto* entry = getSelectedModel();
    if (entry == nullptr || getStartBlockReason().isNotEmpty())
        return;

    const auto& chosen = selections[entry->id];
    StemSelection selection;
    for (int i = 0; i < entry->getNumOutputs(); ++i)
        if (chosen.count (entry->outputs[(size_t) i].name) > 0)
            selection.push_back (i);

    processor.setStemComputeDevice (device);
    if (customOutputFolder.isDirectory())
        processor.startStemSeparation (targetSampleId, entry->id, selection, exportMode, customOutputFolder);
    else
        processor.startStemSeparation (targetSampleId, entry->id, selection, exportMode);

    if (onCloseRequested != nullptr)
        onCloseRequested();
}

void StemView::chooseOutputFolder()
{
    auto initial = customOutputFolder.isDirectory() ? customOutputFolder : juce::File();
    folderChooser = std::make_unique<juce::FileChooser> ("Choose Stem Export Folder", initial, "*");
    folderChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis = juce::Component::SafePointer<StemView> (this)] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr)
                return;

            const auto folder = fc.getResult();
            if (folder.isDirectory())
            {
                safeThis->customOutputFolder = folder;
                safeThis->outputCell.value = folder.getFileName();
                safeThis->repaint();
            }
        });
}

void StemView::refreshThemeColours()
{
    for (auto* button : { &allButton, &noneButton, &downloadButton, &browseButton, &startButton, &closeButton })
        styleButton (*button);

    searchEditor.setColour (juce::TextEditor::backgroundColourId, getTheme().surface1.withAlpha (0.92f));
    searchEditor.setColour (juce::TextEditor::outlineColourId, getTheme().surface4.withAlpha (0.92f));
    searchEditor.setColour (juce::TextEditor::focusedOutlineColourId, getTheme().accent.withAlpha (0.85f));
    searchEditor.setColour (juce::TextEditor::textColourId, getTheme().text2);
    searchEditor.setColour (juce::TextEditor::highlightColourId, getTheme().accent.withAlpha (0.35f));
    searchEditor.setTextToShowWhenEmpty ("Search stems", getTheme().text0.withAlpha (0.7f));

    for (auto* viewport : { &modelViewport, &stemViewport })
        viewport->getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, getTheme().accent.withAlpha (0.6f));

    searchEditor.repaint();
    repaint();
}

void StemView::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().surface0);

    g.setColour (getTheme().surface4.withAlpha (0.85f));
    g.fillRect (kModelsW, 0, 1, getHeight() - kBottomBarH);
    g.fillRect (0, getHeight() - kBottomBarH, getWidth(), 1);

    drawSectionLabel (g, "Models", { 12, 4, kModelsW - 16, kSectionLabelH });

    // ---- Selected model details ----
    if (const auto* entry = getSelectedModel())
    {
        auto area = detailArea;
        g.setColour (getTheme().text2);
        g.setFont (IntersectLookAndFeel::makeFont (10.0f, true));
        g.drawText (entry->menuLabel, area.removeFromTop (14), juce::Justification::centredLeft, true);

        juce::StringArray facts;
        facts.add (juce::String (entry->getNumOutputs()) + " stems");
        if (entry->license.isNotEmpty())
            facts.add (entry->license);
        if (entry->attribution.isNotEmpty())
            facts.add (entry->attribution);
        g.setColour (getTheme().text1);
        g.setFont (IntersectLookAndFeel::makeFont (8.5f));
        g.drawText (facts.joinIntoString (separatorDot()), area.removeFromTop (13),
                    juce::Justification::centredLeft, true);

        if (entry->notes.isNotEmpty())
        {
            g.setColour (getTheme().text0);
            g.drawFittedText (entry->notes, area.removeFromTop (26), juce::Justification::topLeft, 2, 1.0f);
        }

        // ---- Requirements: label column + value, one line each ----
        const auto& req = entry->requirements;
        area.removeFromTop (4);
        const auto drawRequirement = [&] (const juce::String& label, const juce::String& value, juce::Colour colour)
        {
            if (value.isEmpty())
                return;
            auto line = area.removeFromTop (12);
            g.setColour (getTheme().text0);
            g.setFont (IntersectLookAndFeel::makeFont (7.5f, true));
            g.drawText (label, line.removeFromLeft (28), juce::Justification::centredLeft, false);
            g.setColour (colour);
            g.setFont (IntersectLookAndFeel::makeFont (8.5f));
            g.drawText (value, line, juce::Justification::centredLeft, true);
        };

        if (req.ramGb > 0.0f)
        {
            // Compared against total memory: if the whole machine has less, it won't run well.
            const int systemGb = getSystemRamGb();
            const bool notEnough = systemGb > 0 && (float) systemGb < req.ramGb;
            juce::String value = juce::String ((int) std::ceil (req.ramGb)) + " GB";
            if (systemGb > 0)
                value << separatorDot() << "this computer " << systemGb << " GB";
            drawRequirement ("RAM", value, notEnough ? getTheme().color5 : getTheme().text1);
        }
        if (req.cpuRealtimeFactor > 0.0f)
            drawRequirement ("CPU", formatCpuTime (req.cpuRealtimeFactor), getTheme().text1);
        drawRequirement ("GPU", formatGpuAdvice (req.gpu), getTheme().text1);

        if (isDownloading (entry->id))
        {
            const auto progress = processor.getUiSliceSnapshot().stemDownloadProgress;
            g.setColour (getTheme().surface3);
            g.fillRoundedRectangle (progressArea.toFloat(), 2.0f);
            g.setColour (getTheme().accent.withAlpha (0.8f));
            g.fillRoundedRectangle (progressArea.withWidth (juce::roundToInt ((float) progressArea.getWidth() * progress))
                                        .toFloat(), 2.0f);
        }
    }

    // ---- Stems header ----
    {
        auto area = headerArea;
        juce::String title = "Stems";
        if (const auto sampleName = getTargetSampleName(); sampleName.isNotEmpty())
            title << separatorDot() << sampleName;
        drawSectionLabel (g, title, area.removeFromLeft (area.getWidth() / 2));

        if (const auto* entry = getSelectedModel(); entry != nullptr && stemViewport.isVisible())
        {
            const auto it = selections.find (entry->id);
            int count = 0;
            if (it != selections.end())
                for (const auto& output : entry->outputs)
                    count += it->second.count (output.name) > 0 ? 1 : 0;

            g.setColour (getTheme().text1);
            g.setFont (IntersectLookAndFeel::makeFont (8.5f));
            g.drawText (juce::String (count) + " / " + juce::String (entry->getNumOutputs()),
                        area.removeFromRight (52), juce::Justification::centredRight, true);
        }
    }

    // ---- Unavailable message ----
    if (! stemViewport.isVisible())
    {
        g.setColour (getTheme().text1);
        g.setFont (IntersectLookAndFeel::makeFont (10.0f));
        g.drawFittedText (getStartBlockReason(), messageArea, juce::Justification::centred, 3);
    }

    // ---- Bottom bar cells ----
    for (const auto* cell : { &deviceCell, &modeCell, &outputCell })
    {
        g.setColour (getTheme().surface3);
        g.fillRoundedRectangle (cell->bounds.toFloat(), 3.0f);

        auto content = cell->bounds.reduced (6, 0);
        g.setFont (IntersectLookAndFeel::makeFont (8.0f, true));
        g.setColour (getTheme().text2.withAlpha (0.6f));
        g.drawText (cell->label, content.removeFromTop (12).translated (0, 2), juce::Justification::centredLeft);

        g.setFont (IntersectLookAndFeel::makeFont (10.0f));
        g.setColour (cell->enabled ? getTheme().text1.brighter (0.4f) : getTheme().text0);
        g.drawText (cell->value, content, juce::Justification::centredLeft, true);
    }

    // Why START is disabled, next to it.
    if (stemViewport.isVisible() && ! startButton.isEnabled())
    {
        g.setColour (getTheme().text0);
        g.setFont (IntersectLookAndFeel::makeFont (8.5f));
        const auto hintArea = juce::Rectangle<int> (startButton.getX() - 180, startButton.getY(),
                                                     176, startButton.getHeight());
        if (hintArea.getX() > browseButton.getRight())
            g.drawFittedText (getStartBlockReason(), hintArea, juce::Justification::centredRight, 2);
    }
}

void StemView::resized()
{
    auto area = getLocalBounds();

    // Bottom bar
    auto bar = area.removeFromBottom (kBottomBarH).reduced (8, 4);
    closeButton.setBounds (bar.removeFromRight (60));
    bar.removeFromRight (6);
    startButton.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (6);
    deviceCell.bounds = bar.removeFromLeft (124);
    bar.removeFromLeft (6);
    modeCell.bounds = bar.removeFromLeft (86);
    bar.removeFromLeft (6);
    // OUTPUT gets what's left, minus room for the START hint.
    const int outputW = juce::jlimit (110, 260, bar.getWidth() - 30 - 186);
    outputCell.bounds = bar.removeFromLeft (outputW);
    bar.removeFromLeft (4);
    browseButton.setBounds (bar.removeFromLeft (24));

    // Left: models + details
    auto left = area.removeFromLeft (kModelsW);
    area.removeFromLeft (1);
    left.removeFromTop (kSectionLabelH + 4);
    auto details = left.removeFromBottom (kDetailH).reduced (12, 6);
    modelViewport.setBounds (left.reduced (0, 2));
    modelList->updateSize (modelViewport.getMaximumVisibleWidth());

    downloadButton.setBounds (details.removeFromBottom (22).removeFromLeft (96));
    details.removeFromBottom (6);
    progressArea = details.removeFromBottom (4);
    details.removeFromBottom (4);
    detailArea = details;

    // Right: header + checklist
    auto right = area.reduced (10, 6);
    auto header = right.removeFromTop (kHeaderRowH);
    noneButton.setBounds (header.removeFromRight (44));
    header.removeFromRight (4);
    allButton.setBounds (header.removeFromRight (36));
    header.removeFromRight (8);
    searchEditor.setBounds (header.removeFromRight (150));
    header.removeFromRight (8);
    headerArea = header;

    right.removeFromTop (6);
    stemViewport.setBounds (right);
    messageArea = right.reduced (20);
    checklist->layout (stemViewport.getMaximumVisibleWidth() - 2);
}

bool StemView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
        return true;
    }
    return false;
}

void StemView::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    if (deviceCell.bounds.contains (pos) && deviceCell.enabled)
    {
        device = device == StemComputeDevice::cpu ? StemComputeDevice::gpu : StemComputeDevice::cpu;
        updateDeviceCell();
        repaint (deviceCell.bounds);
    }
    else if (modeCell.bounds.contains (pos))
    {
        exportMode = exportMode == StemExportMode::combine ? StemExportMode::separate : StemExportMode::combine;
        modeCell.value = stemExportModeToString (exportMode);
        repaint (modeCell.bounds);
    }
    else if (outputCell.bounds.contains (pos))
    {
        // Click resets to "beside sample"; "..." picks a folder.
        if (customOutputFolder != juce::File())
        {
            customOutputFolder = juce::File();
            outputCell.value = "Beside sample";
            repaint (outputCell.bounds);
        }
        else
        {
            chooseOutputFolder();
        }
    }
}

void StemView::visibilityChanged()
{
    if (isVisible())
        startTimerHz (10);
    else
        stopTimer();
}

void StemView::timerCallback()
{
    const auto& ui = processor.getUiSliceSnapshot();

    // The sample went away (deleted / kit replaced).
    if (targetSampleId >= 0 && getTargetSampleName().isEmpty())
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
        return;
    }

    const bool downloading = ui.stemDownloadState == StemModelDownloadState::downloading;
    const bool jobRunning = isStemJobActive (ui.stemJobState);
    if (downloading != wasDownloading)
    {
        wasDownloading = downloading;
        if (! downloading)
            reloadCatalog();   // a finished download can add a model or a newer manifest
        updateControls();
    }
    if (jobRunning != wasJobRunning)
    {
        wasJobRunning = jobRunning;
        updateControls();
    }

    if (downloading)
    {
        modelList->repaint();
        repaint (progressArea.expanded (2));
    }
}
