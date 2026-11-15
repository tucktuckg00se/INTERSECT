#include "CalloutMenu.h"
#include "Callout.h"
#include "IntersectLookAndFeel.h"

namespace CalloutMenu
{
namespace
{
constexpr int kRowH = 17;
constexpr int kSeparatorH = 7;
constexpr int kTickW = 13;      // the tick column at the left of every row
constexpr int kArrowW = 14;     // the submenu arrow at the right
constexpr int kMinWidth = 150;
constexpr int kMaxWidth = 360;

juce::Font rowFont() { return IntersectLookAndFeel::makeFont (8.8f); }

class MenuPopover final : public Callout::Popover
{
public:
    MenuPopover (const juce::String& heading, std::vector<Item> items)
    {
        levels.push_back ({ heading, std::move (items) });
        relayout (false);
    }

    void paint (juce::Graphics& g) override
    {
        Popover::paint (g);
        const auto& level = levels.back();
        const bool nested = levels.size() > 1;
        const auto back = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xb9  "));
        if (nested && hoverHeading)
        {
            g.setColour (getTheme().surface3);
            g.fillRoundedRectangle (headingArea.toFloat().expanded (3.0f, 1.0f), 3.0f);
        }
        Callout::paintHeading (g, nested ? back + level.title : level.title, headingArea);

        for (size_t i = 0; i < level.items.size(); ++i)
        {
            const auto& item = level.items[i];
            const auto row = rows[i];
            if (item.separatorBefore)
            {
                g.setColour (getTheme().surface4);
                g.fillRect (row.getX(), row.getY() - kSeparatorH / 2 - 1, row.getWidth(), 1);
            }
            if ((int) i == hover && item.enabled)
            {
                g.setColour (getTheme().surface4);
                g.fillRoundedRectangle (row.toFloat(), 3.0f);
            }

            auto text = row;
            const auto tickArea = text.removeFromLeft (kTickW);
            const auto arrowArea = text.removeFromRight (kArrowW);
            auto colour = item.destructive ? getTheme().color5.brighter (0.25f) : getTheme().text1;
            if (! item.enabled)
                colour = getTheme().text0.withMultipliedAlpha (0.6f);
            g.setColour (colour);
            g.setFont (rowFont());
            if (item.ticked)
                g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93")), tickArea, juce::Justification::centred, false);
            g.drawText (item.text, text, juce::Justification::centredLeft, true);
            if (! item.submenu.empty())
                g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")), arrowArea, juce::Justification::centred, false);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (e.getPosition()); }
    void mouseDrag (const juce::MouseEvent& e) override { setHover (e.getPosition()); }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; hoverHeading = false; repaint(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (levels.size() > 1 && headingArea.expanded (3, 2).contains (e.getPosition()))
        {
            goBack();
            return;
        }
        const int index = rowAt (e.getPosition());
        if (index >= 0)
            activate (index);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto& items = levels.back().items;
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            const int step = key == juce::KeyPress::upKey ? -1 : 1;
            const int count = (int) items.size();
            for (int n = 1; n <= count; ++n)
            {
                const int next = ((hover < 0 ? (step > 0 ? -1 : count) : hover) + step * n + count * 2) % count;
                if (items[(size_t) next].enabled)
                {
                    hover = next;
                    break;
                }
            }
            repaint();
            return true;
        }
        if (key == juce::KeyPress::rightKey && hover >= 0 && ! items[(size_t) hover].submenu.empty())
        {
            activate (hover);
            return true;
        }
        if ((key == juce::KeyPress::leftKey || key == juce::KeyPress::backspaceKey) && levels.size() > 1)
        {
            goBack();
            return true;
        }
        return Popover::keyPressed (key);
    }

    /** Where to show: under `area`, left edges aligned (Popover::show aligns right edges). */
    juce::Rectangle<int> leftAligned (juce::Rectangle<int> area) const
    {
        return { area.getX(), area.getY(), getWidth(), area.getHeight() };
    }

private:
    struct Level
    {
        juce::String title;
        std::vector<Item> items;
    };

    void returnKeyPressed() override
    {
        if (hover >= 0)
            activate (hover);
    }

    void activate (int index)
    {
        auto& items = levels.back().items;
        if (index < 0 || index >= (int) items.size() || ! items[(size_t) index].enabled)
            return;
        auto& item = items[(size_t) index];
        if (! item.submenu.empty())
        {
            levels.push_back ({ item.text, item.submenu });
            hover = -1;
            relayout (true);
            return;
        }
        dismissAndRun (item.action);   // this popover is deleted after; the action owns what it needs
    }

    void goBack()
    {
        levels.pop_back();
        hover = -1;
        relayout (true);
    }

    void setHover (juce::Point<int> p)
    {
        const int index = rowAt (p);
        const bool overHeading = levels.size() > 1 && headingArea.expanded (3, 2).contains (p);
        if (index != hover || overHeading != hoverHeading)
        {
            hover = index;
            hoverHeading = overHeading;
            repaint();
        }
    }

    int rowAt (juce::Point<int> p) const
    {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].contains (p))
                return levels.back().items[i].enabled ? (int) i : -1;
        return -1;
    }

    /** Sizes the box for the current level and lays out its rows. After the first layout the box
        keeps its top-left corner (moved up if it would leave the editor). */
    void relayout (bool keepPosition)
    {
        const auto& level = levels.back();
        int width = kMinWidth;
        const auto headingWidth = juce::GlyphArrangement::getStringWidthInt (IntersectLookAndFeel::makeFont (8.5f, true),
                                                                              level.title + "     ");
        width = juce::jmax (width, headingWidth + 2 * Callout::kPadding);
        for (const auto& item : level.items)
            width = juce::jmax (width, juce::GlyphArrangement::getStringWidthInt (rowFont(), item.text)
                                           + kTickW + kArrowW + 8 + 2 * Callout::kPadding);
        width = juce::jmin (width, kMaxWidth);

        int height = Callout::kHeadingH + 4;
        for (const auto& item : level.items)
            height += kRowH + (item.separatorBefore ? kSeparatorH : 0);
        height += 2 * Callout::kPadding + Callout::kShadow;

        if (keepPosition)
        {
            auto bounds = getBounds().withSize (width, height);
            if (auto* parent = getParentComponent())
                bounds = bounds.constrainedWithin (parent->getLocalBounds().reduced (4));
            setBounds (bounds);
        }
        else
        {
            setSize (width, height);
        }

        rows.clear();
        auto area = getContentArea();
        headingArea = area.removeFromTop (Callout::kHeadingH);
        area.removeFromTop (4);
        for (const auto& item : level.items)
        {
            if (item.separatorBefore)
                area.removeFromTop (kSeparatorH);
            rows.push_back (area.removeFromTop (kRowH).expanded (4, 0));
        }
        repaint();
    }

    std::vector<Level> levels;
    std::vector<juce::Rectangle<int>> rows;
    juce::Rectangle<int> headingArea;
    int hover = -1;
    bool hoverHeading = false;
};
} // namespace

void show (juce::Component& anchor, juce::Rectangle<int> area, const juce::String& heading, std::vector<Item> items)
{
    auto* menu = new MenuPopover (heading, std::move (items));
    menu->show (anchor, menu->leftAligned (area));
}
} // namespace CalloutMenu
