#include "SettingsView.h"
#include "StemSettingsPage.h"
#include "../IntersectLookAndFeel.h"
#include "../FullViewBar.h"
#include "../../AppFiles.h"
#include "../../PluginProcessor.h"

namespace
{
constexpr int kSidebarW = 150;
constexpr int kSidebarTopH = 28;   // "SETTINGS" heading above the categories
constexpr int kCategoryRowH = 24;
constexpr int kTitleBarH = 32;
}

SettingsView::SettingsView (IntersectProcessor& p, SettingsHost& host)
    : processor (p)
{
    setWantsKeyboardFocus (true);

    auto add = [this] (const juce::String& name, std::unique_ptr<SettingsPage> page)
    {
        page->onContentHeightChanged = [this] { fitActivePage(); };
        categories.push_back ({ name, std::move (page) });
    };
    add ("General", std::make_unique<GeneralSettingsPage> (p, host));
    add ("MIDI",    std::make_unique<MidiSettingsPage> (p, host));
    add ("Files",   std::make_unique<FilesSettingsPage> (p, host));
    add ("Presets", std::make_unique<PresetsSettingsPage> (p, host));
    add ("Stems",   std::make_unique<StemSettingsPage> (p, host));

    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    viewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);

    closeButton.setTooltip ("Close (Esc)");
    closeButton.onClick = [this]
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
    };
    addAndMakeVisible (closeButton);

    openFolderButton.setTooltip ("Show settings.yaml, themes and presets in your file manager");
    openFolderButton.onClick = [this]
    {
        const auto dir = AppFiles::getSettingsDir();
        if (dir.createDirectory().wasOk())
            dir.startAsProcess();
        else
            processor.showTransientStatusMessage ("Couldn't create " + dir.getFullPathName(), true);
    };
    addAndMakeVisible (openFolderButton);

    // No reload here: the editor is still being constructed. The page loads when the view opens.
    viewport.setViewedComponent (categories.front().page.get(), false);
    refreshThemeColours();
}

SettingsView::~SettingsView()
{
    stopTimer();
    viewport.setViewedComponent (nullptr, false);
}

SettingsPage* SettingsView::getActivePage() const
{
    return juce::isPositiveAndBelow (activeIndex, (int) categories.size())
               ? categories[(size_t) activeIndex].page.get()
               : nullptr;
}

void SettingsView::reloadActivePage()
{
    if (auto* page = getActivePage())
    {
        page->reload();
        fitActivePage();
    }
}

void SettingsView::selectCategory (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) categories.size()))
        return;

    activeIndex = index;
    viewport.setViewedComponent (categories[(size_t) index].page.get(), false);
    viewport.setViewPosition (0, 0);
    reloadActivePage();
    repaint();
}

void SettingsView::fitActivePage()
{
    auto* page = getActivePage();
    if (page == nullptr || fitting || viewport.getWidth() <= 0)
        return;

    const juce::ScopedValueSetter<bool> guard (fitting, true);
    const int viewW = viewport.getWidth();
    const int viewH = viewport.getHeight();

    // Layout depends only on width; a few passes settle the scrollbar / height interplay.
    for (int pass = 0; pass < 3; ++pass)
    {
        const bool scrolls = page->getContentHeight() > viewH;
        const int w = scrolls ? viewW - viewport.getScrollBarThickness() : viewW;
        const int h = juce::jmax (viewH, page->getContentHeight());
        if (page->getWidth() == w && page->getHeight() == h)
            break;
        page->setSize (w, h);
    }
}

int SettingsView::categoryAt (juce::Point<int> position) const
{
    for (int i = 0; i < (int) categories.size(); ++i)
        if (getCategoryBounds (i).contains (position))
            return i;
    return -1;
}

juce::Rectangle<int> SettingsView::getCategoryBounds (int index) const
{
    return { sidebarArea.getX() + 6, sidebarArea.getY() + kSidebarTopH + index * kCategoryRowH,
             sidebarArea.getWidth() - 12, kCategoryRowH - 2 };
}

void SettingsView::refreshThemeColours()
{
    FullViewBar::styleButton (closeButton);
    FullViewBar::styleButton (openFolderButton);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, getTheme().accent.withAlpha (0.6f));
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    for (auto& category : categories)
        category.page->refreshThemeColours();
    repaint();
}

void SettingsView::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().surface0);

    g.setColour (getTheme().surface4.withAlpha (0.85f));
    g.fillRect (sidebarArea.getRight(), 0, 1, sidebarArea.getHeight());
    FullViewBar::paintTopLine (g, barArea);
    {
        // Version, plus a note in accent when a newer release is out (visible from every page).
        const auto font = IntersectLookAndFeel::makeFont (8.5f);
        const auto versionText = "INTERSECT v" + juce::String (JucePlugin_VersionString);
        auto area = versionArea;
        g.setFont (font);
        g.setColour (getTheme().text0);
        g.drawText (versionText, area, juce::Justification::centredLeft, true);
        if (updateNote.isNotEmpty())
        {
            area.removeFromLeft (juce::roundToInt (SettingsUi::measureTextWidth (font, versionText)) + 4);
            g.setColour (getTheme().accent);
            g.setFont (IntersectLookAndFeel::makeFont (8.5f, true));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xb7  ")) + updateNote, area,
                        juce::Justification::centredLeft, true);
        }
    }

    g.setColour (getTheme().surface4.withAlpha (0.5f));
    g.fillRect (sidebarArea.getRight() + 1, viewport.getY() - 1, getWidth() - sidebarArea.getRight() - 1, 1);

    // ---- Sidebar ----
    g.setColour (getTheme().text0.withAlpha (0.85f));
    g.setFont (IntersectLookAndFeel::makeFont (8.0f, true));
    g.drawText ("SETTINGS", sidebarArea.withHeight (kSidebarTopH).reduced (14, 0).withTrimmedTop (4),
                juce::Justification::centredLeft, false);

    for (int i = 0; i < (int) categories.size(); ++i)
    {
        const auto row = getCategoryBounds (i);
        const bool active = i == activeIndex;
        if (active)
        {
            g.setColour (getTheme().surface3.interpolatedWith (getTheme().accent, 0.14f));
            g.fillRoundedRectangle (row.toFloat(), 3.0f);
        }
        else if (i == hoveredIndex)
        {
            g.setColour (getTheme().surface3.withAlpha (0.6f));
            g.fillRoundedRectangle (row.toFloat(), 3.0f);
        }

        g.setColour (active ? getTheme().text2 : getTheme().text2.withAlpha (0.78f));
        g.setFont (IntersectLookAndFeel::makeFont (9.5f, active));
        g.drawText (categories[(size_t) i].name, row.reduced (8, 0), juce::Justification::centredLeft, true);
    }

    // ---- Page title ----
    if (juce::isPositiveAndBelow (activeIndex, (int) categories.size()))
    {
        g.setColour (getTheme().text2);
        g.setFont (IntersectLookAndFeel::makeFont (10.0f, true));
        g.drawText (categories[(size_t) activeIndex].name.toUpperCase(), titleArea, juce::Justification::centredLeft, true);
    }
}

void SettingsView::resized()
{
    auto area = getLocalBounds();
    barArea = area.removeFromBottom (FullViewBar::kHeight);
    auto bar = FullViewBar::content (barArea);
    closeButton.setBounds (bar.removeFromRight (FullViewBar::kCloseWidth));
    bar.removeFromRight (6);
    openFolderButton.setBounds (bar.removeFromRight (SettingsUi::buttonWidthFor (openFolderButton.getButtonText(), 140)));
    versionArea = bar.withTrimmedLeft (6);

    sidebarArea = area.removeFromLeft (kSidebarW);
    area.removeFromLeft (1);

    titleArea = area.removeFromTop (kTitleBarH).reduced (18, 6);

    area.removeFromTop (1);
    viewport.setBounds (area);
    fitActivePage();
}

bool SettingsView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onCloseRequested != nullptr)
            onCloseRequested();
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        selectCategory (juce::jlimit (0, (int) categories.size() - 1,
                                      activeIndex + (key == juce::KeyPress::upKey ? -1 : 1)));
        return true;
    }
    return false;
}

void SettingsView::mouseDown (const juce::MouseEvent& e)
{
    if (const int index = categoryAt (e.getPosition()); index >= 0 && index != activeIndex)
        selectCategory (index);
}

void SettingsView::mouseMove (const juce::MouseEvent& e)
{
    if (const int index = categoryAt (e.getPosition()); index != hoveredIndex)
    {
        hoveredIndex = index;
        repaint (sidebarArea);
    }
}

void SettingsView::mouseExit (const juce::MouseEvent&)
{
    if (hoveredIndex >= 0)
    {
        hoveredIndex = -1;
        repaint (sidebarArea);
    }
}

void SettingsView::visibilityChanged()
{
    if (isVisible())
        startTimerHz (10);
    else
        stopTimer();
}

void SettingsView::timerCallback()
{
    if (auto* page = getActivePage())
        page->tick();

    const auto status = processor.getUpdateChecker().getStatus();
    const auto note = status.state == UpdateChecker::State::updateAvailable
                          ? status.latestVersion + " available"
                          : juce::String();
    if (note != updateNote)
    {
        updateNote = note;
        repaint (versionArea);
    }
}
