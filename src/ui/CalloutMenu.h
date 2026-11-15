#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

/**
    A right-click menu in the popovers' look (SET BPM, the remove prompt) instead of the system
    menu: the shared box, a bold heading, rows of small text, ticks, separators, and submenus that
    open in the same box with a back arrow on the heading. Up/Down/Return work, Left or a click on
    the heading goes back, Esc or a click outside closes. It lives inside the editor, so it scales
    with the UI like the other popovers.
*/
namespace CalloutMenu
{
struct Item
{
    juce::String text;
    std::function<void()> action;   // runs once the menu has closed
    bool enabled = true;
    bool ticked = false;
    bool destructive = false;       // removes something: drawn in the warning colour
    bool separatorBefore = false;
    std::vector<Item> submenu;      // non-empty: opens these in place, titled `text`

    static Item separatorThen (Item item) { item.separatorBefore = true; return item; }
};

/** Opens the menu under `area` (in anchor's coordinates), left edges aligned, flipped above when
    there's no room. */
void show (juce::Component& anchor, juce::Rectangle<int> area, const juce::String& heading, std::vector<Item> items);
}
