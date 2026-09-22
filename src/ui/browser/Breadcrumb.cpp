#include "Breadcrumb.h"
#include "BrowserIcons.h"
#include "../IntersectLookAndFeel.h"
#include "../../AppFiles.h"

namespace
{
constexpr int kSegmentPadX = 5;
constexpr int kChevronW = 10;
constexpr float kFontSize = 9.5f;

// Measured width plus a little slack: drawText's ellipsis check is slightly stricter than
// GlyphArrangement's measurement, which otherwise truncates labels that do fit.
int textWidth (const juce::String& text)
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (IntersectLookAndFeel::makeFont (kFontSize), text)) + 4;
}
}

Breadcrumb::Breadcrumb()
{
    editor.setJustification (juce::Justification::centredLeft);
    editor.setSelectAllWhenFocused (true);
    editor.setIndents (5, 0);
    editor.setMultiLine (false);
    editor.setReturnKeyStartsNewLine (false);
    editor.setScrollbarsShown (false);
    editor.setFont (IntersectLookAndFeel::makeFont (9.0f));
    editor.onReturnKey = [this] { commitEdit(); };
    editor.onEscapeKey = [this] { endEdit(); };
    editor.onFocusLost = [this]
    {
        if (editing)
            endEdit();
    };
    addChildComponent (editor);
    refreshThemeColours();
}

void Breadcrumb::setDirectory (const juce::File& dir)
{
    directory = dir;
    rebuildSegments();
    layoutSegments();
    repaint();
}

void Breadcrumb::rebuildSegments()
{
    segments.clear();

    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto presets = AppFiles::getPresetsDir();
    std::vector<Segment> reversed;
    for (auto dir = directory; dir != juce::File(); dir = dir.getParentDirectory())
    {
        // Short roots instead of long hidden paths (e.g. ~/.config/INTERSECT/presets).
        if (dir == presets)
        {
            reversed.push_back ({ "Presets", dir, {} });
            break;
        }
        if (dir == home)
        {
            reversed.push_back ({ "Home", dir, {} });
            break;
        }

        auto label = dir.getFileName();
        if (label.isEmpty())
            label = dir.getFullPathName();   // filesystem root, e.g. "/" or "C:\"
        reversed.push_back ({ label, dir, {} });

        if (dir.getParentDirectory() == dir)
            break;
    }

    segments.assign (reversed.rbegin(), reversed.rend());
}

void Breadcrumb::layoutSegments()
{
    const auto area = getLocalBounds().reduced (4, 0);

    // Fit from the deepest folder backwards; whatever doesn't fit collapses into "...".
    const int ellipsisW = textWidth ("...") + kSegmentPadX * 2 + kChevronW;
    int used = 0;
    firstVisibleSegment = (int) segments.size();
    for (int i = (int) segments.size() - 1; i >= 0; --i)
    {
        const int w = textWidth (segments[(size_t) i].label) + kSegmentPadX * 2 + (i > 0 ? kChevronW : 0);
        const int reserve = i > 0 ? ellipsisW : 0;
        if (used + w + reserve > area.getWidth() && firstVisibleSegment < (int) segments.size())
            break;
        used += w;
        firstVisibleSegment = i;
    }

    // "..." plus its chevron occupy ellipsisW; after that, a chevron gap precedes every
    // visible segment except the first one.
    int x = area.getX() + (firstVisibleSegment > 0 ? ellipsisW : 0);
    for (int i = 0; i < (int) segments.size(); ++i)
    {
        auto& segment = segments[(size_t) i];
        if (i < firstVisibleSegment)
        {
            segment.bounds = {};
            continue;
        }

        if (i > firstVisibleSegment)
            x += kChevronW;
        const int w = juce::jmin (textWidth (segment.label) + kSegmentPadX * 2, area.getRight() - x);
        segment.bounds = { x, area.getY() + 2, juce::jmax (0, w), area.getHeight() - 4 };
        x += w;
    }
}

void Breadcrumb::refreshThemeColours()
{
    const auto warning = getTheme().color5;
    editor.setColour (juce::TextEditor::backgroundColourId,
                      errorTicks > 0 ? getTheme().surface1.interpolatedWith (warning, 0.18f) : getTheme().surface1);
    editor.setColour (juce::TextEditor::outlineColourId, getTheme().surface4);
    editor.setColour (juce::TextEditor::focusedOutlineColourId,
                      errorTicks > 0 ? warning.withAlpha (0.9f) : getTheme().accent.withAlpha (0.85f));
    editor.setColour (juce::TextEditor::textColourId, getTheme().text2);
    editor.setColour (juce::TextEditor::highlightColourId, getTheme().accent.withAlpha (0.35f));
    repaint();
}

void Breadcrumb::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (getTheme().surface1.withAlpha (0.92f));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (getTheme().surface4.withAlpha (0.92f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    if (editing)
        return;

    g.setFont (IntersectLookAndFeel::makeFont (kFontSize));

    if (firstVisibleSegment > 0 && ! segments.empty())
    {
        const auto first = segments[(size_t) firstVisibleSegment].bounds;
        const auto ellipsis = juce::Rectangle<int> (4, first.getY(), textWidth ("...") + kSegmentPadX * 2, first.getHeight());
        g.setColour (getTheme().text0);
        g.drawText ("...", ellipsis, juce::Justification::centred, false);
        BrowserIcons::drawChevron (g, juce::Rectangle<float> ((float) ellipsis.getRight(), (float) first.getY(),
                                                              (float) kChevronW, (float) first.getHeight()),
                                   getTheme().text0);
    }

    for (int i = firstVisibleSegment; i < (int) segments.size(); ++i)
    {
        const auto& segment = segments[(size_t) i];
        const bool last = i == (int) segments.size() - 1;

        if (i == hoverSegment && ! last)
        {
            g.setColour (getTheme().surface3);
            g.fillRoundedRectangle (segment.bounds.toFloat(), 2.5f);
        }

        g.setColour (last ? getTheme().text2 : getTheme().text1);
        g.drawText (segment.label, segment.bounds.reduced (kSegmentPadX, 0), juce::Justification::centredLeft, true);

        if (i > firstVisibleSegment)
            BrowserIcons::drawChevron (g, juce::Rectangle<float> ((float) (segment.bounds.getX() - kChevronW),
                                                                  (float) segment.bounds.getY(),
                                                                  (float) kChevronW, (float) segment.bounds.getHeight()),
                                       getTheme().text0);
    }
}

void Breadcrumb::resized()
{
    editor.setBounds (getLocalBounds());
    layoutSegments();
}

int Breadcrumb::hitTestSegment (juce::Point<int> pos) const
{
    for (int i = firstVisibleSegment; i < (int) segments.size(); ++i)
        if (segments[(size_t) i].bounds.contains (pos))
            return i;
    return -1;
}

void Breadcrumb::mouseDown (const juce::MouseEvent& e)
{
    const int segment = hitTestSegment (e.getPosition());
    if (segment >= 0 && e.getNumberOfClicks() < 2)
    {
        if (segment < (int) segments.size() - 1 && onNavigate != nullptr)
            onNavigate (segments[(size_t) segment].dir);
        return;
    }

    beginEditing();
}

void Breadcrumb::mouseMove (const juce::MouseEvent& e)
{
    const int segment = hitTestSegment (e.getPosition());
    if (segment != hoverSegment)
    {
        hoverSegment = segment;
        setMouseCursor (segment >= 0 && segment < (int) segments.size() - 1 ? juce::MouseCursor::PointingHandCursor
                                                                             : juce::MouseCursor::IBeamCursor);
        repaint();
    }
}

void Breadcrumb::mouseExit (const juce::MouseEvent&)
{
    hoverSegment = -1;
    repaint();
}

void Breadcrumb::beginEditing()
{
    if (editing)
        return;

    editing = true;
    editor.setText (directory.getFullPathName(), juce::dontSendNotification);
    editor.setVisible (true);
    editor.grabKeyboardFocus();
    editor.selectAll();
    repaint();
}

void Breadcrumb::commitEdit()
{
    const auto text = editor.getText().trim().unquoted();
    if (juce::File::isAbsolutePath (text) && juce::File (text).isDirectory())
    {
        const juce::File dir (text);
        endEdit();
        if (onNavigate != nullptr)
            onNavigate (dir);
        return;
    }

    flashError();
}

void Breadcrumb::endEdit()
{
    editing = false;
    editor.setVisible (false);
    repaint();
}

void Breadcrumb::flashError()
{
    errorTicks = 1;
    refreshThemeColours();
    juce::Timer::callAfterDelay (700, [safe = juce::Component::SafePointer<Breadcrumb> (this)]
    {
        if (safe == nullptr)
            return;

        safe->errorTicks = 0;
        safe->refreshThemeColours();
        if (safe->editing)
            safe->editor.setText (safe->directory.getFullPathName(), juce::dontSendNotification);
    });
}
