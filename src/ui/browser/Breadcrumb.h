#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

/**
    Clickable path for the browser toolbar: "Home > Samples > Breaks". Clicking a segment
    navigates there; clicking the empty space (or double-clicking) turns it into a text field
    for typing a path. When the path doesn't fit, leading segments collapse into "...".

    The TextEditor is a permanent child that is only shown and hidden, never destroyed from
    inside its own callbacks (see the TextEditor lifecycle note in CLAUDE.md).
*/
class Breadcrumb : public juce::Component
{
public:
    Breadcrumb();

    void setDirectory (const juce::File& dir);
    void beginEditing();
    bool isEditing() const noexcept { return editing; }
    void refreshThemeColours();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    std::function<void (const juce::File&)> onNavigate;

private:
    struct Segment
    {
        juce::String label;
        juce::File dir;
        juce::Rectangle<int> bounds;
    };

    void rebuildSegments();
    void layoutSegments();
    int hitTestSegment (juce::Point<int> pos) const;
    void commitEdit();
    void endEdit();
    void flashError();

    juce::File directory;
    std::vector<Segment> segments;
    int firstVisibleSegment = 0;
    int hoverSegment = -1;
    bool editing = false;
    int errorTicks = 0;
    juce::TextEditor editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Breadcrumb)
};
