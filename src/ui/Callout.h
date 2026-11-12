#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/**
    Popovers inside the plugin editor (the remove prompt, SET BPM) and the box look they share
    with the Import from URL panel. Being added to the editor, they scale with the UI and never
    open a separate window (issue #41).
*/
namespace Callout
{
constexpr int kShadow = 2;    // the soft drop under the box, inside the component's bounds
constexpr int kPadding = 10;
constexpr int kButtonH = 22;
constexpr int kHeadingH = 14;

/** The shared box: a soft drop below, surface2 body, thin outline. bounds includes the drop. */
void paintBox (juce::Graphics& g, juce::Rectangle<int> bounds);
/** A popover's bold heading (e.g. "SET BPM"). */
void paintHeading (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area);

/**
    A modal popover placed just below anchorArea with right edges aligned, flipped above when
    there's no room and kept inside the editor. A click outside or Esc cancels, Return calls
    returnKeyPressed(), and keyboard focus goes back where it was. Positioned by hand rather than
    with juce::CallOutBox, which can't point at something this close to the window edge.
*/
class Popover : public juce::Component
{
public:
    Popover();

    /** Adds this (heap-allocated) popover to the editor that holds anchor and makes it modal.
        From then on the modal manager owns it and deletes it once dismissed. */
    void show (juce::Component& anchor, juce::Rectangle<int> anchorArea);

    void paint (juce::Graphics& g) override;   // the box; subclasses paint on top
    bool keyPressed (const juce::KeyPress& key) override;
    void inputAttemptWhenModal() override;
    void parentHierarchyChanged() override;

protected:
    virtual void returnKeyPressed() {}
    /** Closes without acting. Safe to call more than once. */
    void dismiss();
    /** Closes, then runs action (this popover is deleted once it leaves modal state). */
    void dismissAndRun (std::function<void()> action);
    /** The area inside the padding, above the drop. */
    juce::Rectangle<int> getContentArea() const;

private:
    juce::Component::SafePointer<juce::Component> previousFocus;
    bool shown = false;
};
} // namespace Callout

/**
    A small "are you sure?" popover just below anchorArea (in anchor's coordinates), right edges
    aligned: the message and CANCEL / confirm buttons. Return confirms; Esc or a click outside
    cancels; keyboard focus goes back where it was.
*/
void showConfirmCallout (juce::Component& anchor, juce::Rectangle<int> anchorArea,
                         const juce::String& message, const juce::String& confirmText,
                         std::function<void()> onConfirm);
