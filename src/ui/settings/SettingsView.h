#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SettingsPages.h"
#include <functional>
#include <memory>
#include <vector>

class IntersectProcessor;
class SettingsHost;

/**
    The full-view settings screen (header SET), shown over everything below the sample lane like
    the file browser and the stem view.

    Category list on the left | the selected page on the right (scrolls when taller than the view).
    Every change applies and is saved straight away; there is no OK / Cancel.
*/
class SettingsView : public juce::Component,
                     private juce::Timer
{
public:
    SettingsView (IntersectProcessor& p, SettingsHost& host);
    ~SettingsView() override;

    /** Re-reads the shown page; call when the view opens. */
    void reloadActivePage();
    void refreshThemeColours();

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void visibilityChanged() override;

    std::function<void()> onCloseRequested;

private:
    struct Category
    {
        juce::String name;
        std::unique_ptr<SettingsPage> page;
    };

    void timerCallback() override;
    void selectCategory (int index);
    void fitActivePage();
    int categoryAt (juce::Point<int> position) const;
    juce::Rectangle<int> getCategoryBounds (int index) const;
    SettingsPage* getActivePage() const;

    // Declared before the viewport, which only borrows the active page.
    std::vector<Category> categories;
    juce::Viewport viewport;
    IntersectProcessor& processor;
    juce::TextButton closeButton { "CLOSE" };
    juce::TextButton openFolderButton { "OPEN SETTINGS FOLDER" };
    juce::Rectangle<int> sidebarArea;
    juce::Rectangle<int> titleArea;
    juce::Rectangle<int> barArea;
    juce::Rectangle<int> versionArea;
    juce::String updateNote;   // "0.19.1 available" in the bottom bar while an update is waiting
    int activeIndex = 0;
    int hoveredIndex = -1;
    bool fitting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsView)
};
