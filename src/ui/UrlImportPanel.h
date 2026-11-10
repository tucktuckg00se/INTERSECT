#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../urlimport/UrlImportJob.h"
#include <functional>

class IntersectProcessor;

/**
    Import from URL: a small panel at the top of the editor (not modal, so the editor keeps working
    while a download runs). Views: the link field, the running import's progress, and how an import
    ended when it needs the user (failed, longer than an hour, a live stream). A finished import
    just closes it; the header says what was imported.

    The panel follows the processor's import job, so closing it (HIDE) never stops an import, and
    opening it again shows the import still running.
*/
class UrlImportPanel final : public juce::Component
{
public:
    static constexpr int kWidth = 400;

    explicit UrlImportPanel (IntersectProcessor& processor);
    ~UrlImportPanel() override;

    /** Shows the link field (prefilled) or, while an import runs, its progress. */
    void open (const juce::String& prefillUrl);
    /** Starts importing url straight away and shows its progress (a link typed into the path bar). */
    void startWithUrl (const juce::String& url);
    void close();
    /** Editor timer: follows the job; opens itself when an import needs an answer. */
    void tick();

    int getPreferredHeight() const;
    /** The preferred height changed: the editor positions the panel again. */
    std::function<void()> onLayoutChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void refreshThemeColours();

private:
    enum class View { entry, running, outcome };

    void showView (View newView);
    void startImport (bool allowLong);
    void updateEntryNote();
    void copyDetails();

    IntersectProcessor& processor;
    View view = View::entry;

    juce::TextEditor urlField;
    juce::TextButton primaryButton;
    juce::TextButton secondaryButton;
    juce::TextButton tertiaryButton;

    juce::String heading;
    juce::String body;
    juce::String note;
    bool noteIsWarning = false;
    juce::String stage;
    float progress = -1.0f;
    float indeterminatePhase = 0.0f;
    UrlImport::JobResult::Status outcomeStatus = UrlImport::JobResult::Status::failed;
    juce::String outcomeUrl;
    juce::String outcomeDetails;
    int seenOutcomeSerial = 0;

    juce::Rectangle<int> headingArea, bodyArea, noteArea, progressArea;
    juce::Component::SafePointer<juce::Component> previousFocus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UrlImportPanel)
};
