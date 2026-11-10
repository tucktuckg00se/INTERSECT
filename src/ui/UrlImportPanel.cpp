#include "UrlImportPanel.h"
#include "IntersectLookAndFeel.h"
#include "settings/SettingsControls.h"
#include "../PluginProcessor.h"
#include <cmath>

namespace
{
constexpr int kPad = 10;
constexpr int kHeadingH = 14;
constexpr int kFieldH = 24;
constexpr int kButtonH = 22;
constexpr int kLineH = 13;
constexpr int kShadow = 2;

using Status = UrlImport::JobResult::Status;
using Mode = UrlImport::JobResult::Mode;

bool isImporting (UrlImport::UrlImportJob& job)
{
    if (! job.isBusy())
        return false;
    const auto mode = job.getMode();
    return mode == Mode::import || mode == Mode::place;
}
} // namespace

UrlImportPanel::UrlImportPanel (IntersectProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    seenOutcomeSerial = processor.getLastUrlImportOutcome().serial;   // don't replay an old ending

    urlField.setMultiLine (false);
    urlField.setReturnKeyStartsNewLine (false);
    urlField.setScrollbarsShown (false);
    urlField.setJustification (juce::Justification::centredLeft);
    urlField.setIndents (6, 0);
    urlField.setFont (IntersectLookAndFeel::makeFont (9.5f));
    // TextEditor callbacks arrive as async commands: never tear down or hide from inside one.
    urlField.onReturnKey = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<UrlImportPanel> (this)]
        {
            if (safe != nullptr)
                safe->startImport (false);
        });
    };
    urlField.onEscapeKey = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<UrlImportPanel> (this)]
        {
            if (safe != nullptr)
                safe->close();
        });
    };
    urlField.onTextChange = [this]
    {
        if (noteIsWarning)
        {
            updateEntryNote();
            repaint();
        }
    };
    addAndMakeVisible (urlField);

    for (auto* button : { &primaryButton, &secondaryButton, &tertiaryButton })
        addAndMakeVisible (*button);

    primaryButton.onClick = [this]
    {
        if (view == View::entry)
            startImport (false);
        else if (view == View::running)
            processor.cancelUrlImport();
        else if (outcomeStatus == Status::needsLongConfirm)
            startImport (true);
        else
            close();
    };
    secondaryButton.onClick = [this]
    {
        if (view == View::outcome && outcomeStatus == Status::failed)
            startImport (false);   // TRY AGAIN, e.g. after switching to the nightly yt-dlp
        else
            close();               // CANCEL, HIDE (the import keeps running)
    };
    tertiaryButton.onClick = [this] { copyDetails(); };

    refreshThemeColours();
    showView (View::entry);
}

UrlImportPanel::~UrlImportPanel() = default;

//==============================================================================

void UrlImportPanel::open (const juce::String& prefillUrl)
{
    if (! isVisible())
    {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        previousFocus = (focused != nullptr && ! isParentOf (focused)) ? focused : nullptr;
    }

    if (isImporting (processor.getUrlImportJob()))
    {
        showView (View::running);
    }
    else
    {
        if (prefillUrl.isNotEmpty())
            urlField.setText (prefillUrl.trim(), false);
        showView (View::entry);
    }

    setVisible (true);
    toFront (false);

    if (view == View::entry)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<UrlImportPanel> (this)]
        {
            if (safe != nullptr && safe->isShowing())
            {
                safe->urlField.grabKeyboardFocus();
                safe->urlField.selectAll();
            }
        });
}

void UrlImportPanel::close()
{
    if (! isVisible())
        return;

    setVisible (false);
    if (previousFocus != nullptr && previousFocus->isShowing())
        previousFocus->grabKeyboardFocus();
    else if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();
}

void UrlImportPanel::tick()
{
    auto& job = processor.getUrlImportJob();
    const bool importing = isImporting (job);

    const auto& outcome = processor.getLastUrlImportOutcome();
    if (outcome.serial != seenOutcomeSerial)
    {
        seenOutcomeSerial = outcome.serial;
        outcomeStatus = outcome.status;
        outcomeUrl = outcome.url;
        outcomeDetails = outcome.details;
        body = outcome.message;

        const bool needsAnswer = outcome.status == Status::needsLongConfirm || outcome.status == Status::liveRefused;
        if (needsAnswer || (outcome.status == Status::failed && isVisible()))
        {
            showView (View::outcome);
            if (! isVisible())
                open ({});   // an import started from here needs a yes/no even if the panel was hidden
            else
                toFront (false);
        }
        else if (view == View::running)
        {
            close();   // done or cancelled: the header says so
        }
        return;
    }

    if (view == View::running && isVisible())
    {
        if (importing)
        {
            const auto newStage = job.getStage();
            const auto newProgress = job.getProgress();
            indeterminatePhase = std::fmod (indeterminatePhase + 0.012f, 1.0f);
            if (newStage != stage || newProgress != progress || newProgress < 0.0f)
            {
                stage = newStage;
                progress = newProgress;
                repaint();
            }
        }
        else if (! job.isBusy())
        {
            close();   // e.g. "ask each time": the save dialog takes over
        }
    }
}

//==============================================================================

void UrlImportPanel::startImport (bool allowLong)
{
    const auto url = view == View::entry ? urlField.getText().trim() : outcomeUrl;
    if (! UrlImport::isWebUrl (url))
    {
        if (view != View::entry)
            showView (View::entry);
        note = "Paste a link that starts with https://";
        noteIsWarning = true;
        repaint();
        return;
    }

    outcomeUrl = url;
    processor.startUrlImport (url, allowLong);
    if (processor.getUrlImportJob().isBusy())
    {
        stage = {};
        progress = -1.0f;
        showView (View::running);
    }
}

void UrlImportPanel::updateEntryNote()
{
    const auto tools = processor.getUrlImportJob().readInstalled();
    const auto& settings = processor.getUrlImportSettings();
    noteIsWarning = false;
    if (! tools.isComplete())
        note = "The first import downloads yt-dlp (about 25-55 MB) and audio tools (about 5 MB) from GitHub.";
    else if (settings.askEachTime)
        note = "You choose where to save the WAV after it downloads.";
    else
        note = "Saves a WAV to " + processor.getResolvedUrlImportFolder().getFullPathName();
    if (UrlImport::ToolInstall::getTestManifestUrl().isNotEmpty())
        note << " (test tools manifest)";
}

void UrlImportPanel::copyDetails()
{
    const auto tools = processor.getUrlImportJob().readInstalled();
    juce::String text;
    text << "INTERSECT URL import\n"
         << "Link: " << outcomeUrl << "\n"
         << "yt-dlp " << (tools.ytDlpVersion.isNotEmpty() ? tools.ytDlpVersion : juce::String ("(not installed)"))
         << " (" << UrlImport::channelToString (tools.ytDlpChannel) << "), ffmpeg " << tools.ffmpegVersion
         << ", QuickJS " << tools.qjsVersion << "\n"
         << "System: " << juce::SystemStats::getOperatingSystemName() << "\n\n"
         << body << "\n\n" << outcomeDetails;
    juce::SystemClipboard::copyTextToClipboard (text);
    processor.showTransientStatusMessage ("Copied the import details", false);
}

void UrlImportPanel::showView (View newView)
{
    view = newView;
    urlField.setVisible (view == View::entry);
    tertiaryButton.setVisible (false);
    secondaryButton.setVisible (true);
    bool accentPrimary = false;

    switch (view)
    {
        case View::entry:
            heading = "IMPORT FROM URL";
            body = "Only import audio you have the rights to use.";
            secondaryButton.setButtonText ("CANCEL");
            primaryButton.setButtonText ("IMPORT");
            accentPrimary = true;
            updateEntryNote();
            break;

        case View::running:
            heading = "IMPORTING";
            body = processor.getUrlImportJob().getUrl();
            if (body.isEmpty())
                body = outcomeUrl;
            secondaryButton.setButtonText ("HIDE");
            secondaryButton.setTooltip ("The import keeps running; open Import from URL again to see it");
            primaryButton.setButtonText ("CANCEL IMPORT");
            break;

        case View::outcome:
            if (outcomeStatus == Status::needsLongConfirm)
            {
                heading = "LONG IMPORT";
                secondaryButton.setButtonText ("CANCEL");
                primaryButton.setButtonText ("IMPORT ANYWAY");
                accentPrimary = true;
            }
            else if (outcomeStatus == Status::liveRefused)
            {
                heading = "LIVE STREAM";
                secondaryButton.setVisible (false);
                primaryButton.setButtonText ("CLOSE");
            }
            else
            {
                heading = "IMPORT FAILED";
                tertiaryButton.setButtonText ("COPY DETAILS");
                tertiaryButton.setVisible (true);
                secondaryButton.setButtonText ("TRY AGAIN");
                primaryButton.setButtonText ("CLOSE");
            }
            break;
    }

    SettingsUi::styleButton (primaryButton, accentPrimary);
    SettingsUi::styleButton (secondaryButton);
    SettingsUi::styleButton (tertiaryButton);

    const auto height = getPreferredHeight();
    if (getHeight() != height || getWidth() != kWidth)
    {
        setSize (kWidth, height);
        if (onLayoutChanged != nullptr)
            onLayoutChanged();
    }
    resized();
    repaint();
}

int UrlImportPanel::getPreferredHeight() const
{
    const int chrome = kPad + kHeadingH + 6 + 8 + kButtonH + kPad + kShadow;
    switch (view)
    {
        case View::entry:   return chrome + kFieldH + 6 + 2 * kLineH + kLineH;
        case View::running: return chrome + kLineH + 4 + kLineH + 5 + 6;
        case View::outcome: return chrome + 4 * kLineH;
    }
    return chrome;
}

void UrlImportPanel::refreshThemeColours()
{
    const auto& theme = getTheme();
    urlField.setColour (juce::TextEditor::backgroundColourId, theme.surface1.withAlpha (0.92f));
    urlField.setColour (juce::TextEditor::outlineColourId, theme.surface4.withAlpha (0.92f));
    urlField.setColour (juce::TextEditor::focusedOutlineColourId, theme.accent.withAlpha (0.85f));
    urlField.setColour (juce::TextEditor::textColourId, theme.text2);
    urlField.setColour (juce::TextEditor::highlightColourId, theme.accent.withAlpha (0.35f));
    urlField.setTextToShowWhenEmpty ("Paste a link to a video or track", theme.text0);
    if (view == View::entry || view == View::running || view == View::outcome)
    {
        SettingsUi::styleButton (primaryButton, (view == View::entry)
                                                    || (view == View::outcome && outcomeStatus == Status::needsLongConfirm));
        SettingsUi::styleButton (secondaryButton);
        SettingsUi::styleButton (tertiaryButton);
    }
    repaint();
}

//==============================================================================

void UrlImportPanel::paint (juce::Graphics& g)
{
    const auto& theme = getTheme();

    // Same treatment as the confirm popover and the menus: a soft drop, surface2 body, thin outline.
    const auto box = getLocalBounds().toFloat().withTrimmedBottom ((float) kShadow);
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRoundedRectangle (box.translated (0.0f, (float) kShadow), 4.0f);
    g.setColour (theme.surface2.brighter (0.03f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (theme.surface4);
    g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);

    g.setColour (view == View::outcome && outcomeStatus == Status::failed ? theme.color5 : theme.text1);
    g.setFont (IntersectLookAndFeel::makeFont (8.5f, true));
    g.drawText (heading, headingArea, juce::Justification::centredLeft, true);

    switch (view)
    {
        case View::entry:
            g.setColour (noteIsWarning ? theme.color5 : theme.text1);
            g.setFont (IntersectLookAndFeel::makeFont (8.8f));
            g.drawFittedText (note, noteArea, juce::Justification::topLeft, 2, 1.0f);
            g.setColour (theme.text0);
            g.drawFittedText (body, bodyArea, juce::Justification::topLeft, 1, 1.0f);
            break;

        case View::running:
        {
            g.setColour (theme.text1);
            g.setFont (IntersectLookAndFeel::makeFont (8.8f));
            g.drawText (body, bodyArea, juce::Justification::centredLeft, true);

            auto stageText = stage.isNotEmpty() ? stage : juce::String ("Starting");
            if (progress >= 0.0f)
                stageText << "  " << juce::roundToInt (progress * 100.0f) << "%";
            g.setColour (theme.text2);
            g.setFont (IntersectLookAndFeel::makeFont (9.5f));
            g.drawText (stageText, noteArea, juce::Justification::centredLeft, true);

            const auto track = progressArea.toFloat();
            g.setColour (theme.surface4);
            g.fillRoundedRectangle (track, 2.0f);
            g.setColour (theme.accent);
            if (progress >= 0.0f)
            {
                g.fillRoundedRectangle (track.withWidth (juce::jmax (4.0f, track.getWidth() * juce::jlimit (0.0f, 1.0f, progress))), 2.0f);
            }
            else
            {
                // Nothing tells how far along this stage is: a segment sweeping across instead.
                const float segment = track.getWidth() * 0.28f;
                const float x = track.getX() - segment + (track.getWidth() + segment) * indeterminatePhase;
                g.saveState();
                g.reduceClipRegion (progressArea);
                g.fillRoundedRectangle ({ x, track.getY(), segment, track.getHeight() }, 2.0f);
                g.restoreState();
            }
            break;
        }

        case View::outcome:
            g.setColour (theme.text2);
            g.setFont (IntersectLookAndFeel::makeFont (9.5f));
            g.drawFittedText (body, bodyArea, juce::Justification::topLeft, 4, 1.0f);
            break;
    }
}

void UrlImportPanel::resized()
{
    auto area = getLocalBounds().withTrimmedBottom (kShadow).reduced (kPad);
    headingArea = area.removeFromTop (kHeadingH);
    area.removeFromTop (6);

    auto buttons = area.removeFromBottom (kButtonH);
    area.removeFromBottom (8);
    for (auto* button : { &primaryButton, &secondaryButton, &tertiaryButton })
    {
        if (! button->isVisible())
            continue;
        button->setBounds (buttons.removeFromRight (SettingsUi::buttonWidthFor (button->getButtonText(), 64)));
        buttons.removeFromRight (SettingsUi::kButtonGap + 2);
    }

    bodyArea = noteArea = progressArea = {};
    switch (view)
    {
        case View::entry:
            urlField.setBounds (area.removeFromTop (kFieldH));
            area.removeFromTop (6);
            noteArea = area.removeFromTop (2 * kLineH);
            bodyArea = area.removeFromTop (kLineH);
            break;
        case View::running:
            bodyArea = area.removeFromTop (kLineH);
            area.removeFromTop (4);
            noteArea = area.removeFromTop (kLineH);
            area.removeFromTop (5);
            progressArea = area.removeFromTop (6);
            break;
        case View::outcome:
            bodyArea = area;
            break;
    }
}

bool UrlImportPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();   // while running this only hides the panel
        return true;
    }
    if (key == juce::KeyPress::returnKey && view != View::running)
    {
        primaryButton.triggerClick();
        return true;
    }
    return false;
}
