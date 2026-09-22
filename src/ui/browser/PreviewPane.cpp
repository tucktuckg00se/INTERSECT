#include "PreviewPane.h"
#include "BrowserIcons.h"
#include "FileInfoCache.h"
#include "../IntersectLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace
{
juce::String channelsText (int channels)
{
    switch (channels)
    {
        case 1:  return "Mono";
        case 2:  return "Stereo";
        default: return juce::String (channels) + " ch";
    }
}

void drawLabelValue (juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& label, const juce::String& value)
{
    if (value.isEmpty() || area.getHeight() < 14)
        return;

    auto row = area.removeFromTop (15);
    g.setFont (IntersectLookAndFeel::makeFont (7.8f, true));
    g.setColour (getTheme().text0);
    g.drawText (label.toUpperCase(), row.removeFromLeft (58), juce::Justification::centredLeft, false);
    g.setFont (IntersectLookAndFeel::makeFont (9.5f));
    g.setColour (getTheme().text2.withAlpha (0.9f));
    g.drawText (value, row, juce::Justification::centredLeft, true);
}
}

PreviewPane::PreviewPane (FileInfoCache& infoCache) : info (infoCache)
{
    for (auto* button : { &playButton, &autoButton, &primaryButton, &secondaryButton })
    {
        button->getProperties().set (IntersectLookAndFeel::outlineOnlyButtonProperty, true);
        addChildComponent (*button);
    }
    addChildComponent (gainStrip);

    playButton.setTooltip ("Play or pause the preview (Space). Right arrow restarts it from the top.");
    autoButton.setTooltip ("Preview files automatically as you select them");
    autoButton.setClickingTogglesState (false);
    autoButton.setToggleState (autoPlay, juce::dontSendNotification);
    gainStrip.setTooltip ("Preview volume (double-click to reset)");

    playButton.onClick = [this]
    {
        if (onPlayToggle != nullptr)
            onPlayToggle();
    };
    autoButton.onClick = [this]
    {
        setAutoPlay (! autoPlay);
        if (onAutoPlayChanged != nullptr)
            onAutoPlayChanged (autoPlay);
    };
    primaryButton.onClick = [this]
    {
        switch (mode)
        {
            case Mode::audio:
            case Mode::multi:  if (onAdd != nullptr) onAdd(); break;
            case Mode::preset: if (onLoadPreset != nullptr) onLoadPreset(); break;
            case Mode::folder: if (onOpenFolder != nullptr) onOpenFolder(); break;
            case Mode::none:   break;
        }
    };
    secondaryButton.onClick = [this]
    {
        if ((mode == Mode::audio || mode == Mode::multi) && onLoad != nullptr)
            onLoad();
    };

    refreshThemeColours();
}

void PreviewPane::setSelection (std::vector<Browser::FileRow> selected, const Browser::FileRow* focused)
{
    selection = std::move (selected);
    hasFocus = focused != nullptr;
    focus = hasFocus ? *focused : Browser::FileRow {};

    if (selection.size() > 1)
        mode = Mode::multi;
    else if (! hasFocus)
        mode = Mode::none;
    else if (focus.kind == Browser::FileKind::directory)
        mode = Mode::folder;
    else if (focus.kind == Browser::FileKind::preset)
        mode = Mode::preset;
    else
        mode = Mode::audio;

    if (hasFocus && focus.kind != Browser::FileKind::directory)
        info.request (focus.file);

    updateButtons();
    resized();
    repaint();
}

void PreviewPane::setAuditionStatus (const AuditionStatus& status)
{
    const bool changed = status.state != audition.state
        || status.file != audition.file
        || status.clip != audition.clip
        || std::abs (status.position - audition.position) > 0.001f;
    if (! changed)
        return;

    audition = status;
    updateButtons();
    repaint (waveArea.expanded (2));
}

void PreviewPane::infoUpdated()
{
    repaint();
}

void PreviewPane::setAutoPlay (bool shouldAutoPlay)
{
    autoPlay = shouldAutoPlay;
    autoButton.setToggleState (autoPlay, juce::dontSendNotification);
    refreshThemeColours();
}

void PreviewPane::setGainDb (float db)
{
    gainDb = juce::jlimit (kMinGainDb, kMaxGainDb, db);
    gainStrip.repaint();
}

void PreviewPane::setGainFromUser (float db)
{
    setGainDb (db);
    if (onGainChanged != nullptr)
        onGainChanged (gainDb);
}

int PreviewPane::countSelectedAudio() const
{
    return (int) std::count_if (selection.begin(), selection.end(), [] (const Browser::FileRow& row)
    {
        return row.kind == Browser::FileKind::audio;
    });
}

bool PreviewPane::auditionMatchesFocus() const
{
    return hasFocus && Browser::samePath (audition.file, focus.file);
}

void PreviewPane::updateButtons()
{
    const bool focusIsAudio = hasFocus && focus.kind == Browser::FileKind::audio;
    const bool showAudition = focusIsAudio && (mode == Mode::audio || mode == Mode::multi);
    playButton.setVisible (showAudition);
    autoButton.setVisible (showAudition);
    gainStrip.setVisible (showAudition);

    const bool active = auditionMatchesFocus()
        && (audition.state == AuditionStatus::State::playing || audition.state == AuditionStatus::State::decoding);
    playButton.setButtonText (active ? "PAUSE" : "PLAY");

    switch (mode)
    {
        case Mode::audio:
        case Mode::multi:
        {
            const bool anyAudio = countSelectedAudio() > 0 || focusIsAudio;
            primaryButton.setButtonText ("ADD");
            secondaryButton.setButtonText ("LOAD");
            primaryButton.setTooltip ("Add to the kit (double-click / Return)");
            secondaryButton.setTooltip ("Load, replacing the kit (Shift + double-click / Shift + Return)");
            primaryButton.setVisible (anyAudio);
            secondaryButton.setVisible (anyAudio);
            break;
        }
        case Mode::preset:
            primaryButton.setButtonText ("LOAD PRESET");
            primaryButton.setTooltip ("Load this preset, replacing the kit");
            primaryButton.setVisible (true);
            secondaryButton.setVisible (false);
            break;
        case Mode::folder:
            primaryButton.setButtonText ("OPEN");
            primaryButton.setTooltip ("Open this folder");
            primaryButton.setVisible (true);
            secondaryButton.setVisible (false);
            break;
        case Mode::none:
            primaryButton.setVisible (false);
            secondaryButton.setVisible (false);
            break;
    }
}

void PreviewPane::refreshThemeColours()
{
    for (auto* button : { &playButton, &autoButton, &primaryButton, &secondaryButton })
    {
        button->setColour (juce::TextButton::buttonColourId, getTheme().surface4.withAlpha (0.95f));
        button->setColour (juce::TextButton::textColourOffId, getTheme().text2.withAlpha (0.88f));
        button->setColour (juce::TextButton::textColourOnId, getTheme().accent);
    }
    // The default action reads as the primary one.
    primaryButton.setColour (juce::TextButton::buttonColourId, getTheme().accent.withAlpha (0.55f));
    primaryButton.setColour (juce::TextButton::textColourOffId, getTheme().accent);
    repaint();
}

void PreviewPane::resized()
{
    auto area = getLocalBounds().reduced (10, 10);
    titleArea = area.removeFromTop (28);
    subtitleArea = area.removeFromTop (13);
    area.removeFromTop (8);

    auto actions = area.removeFromBottom (22);
    if (secondaryButton.isVisible())
    {
        const int half = (actions.getWidth() - 6) / 2;
        primaryButton.setBounds (actions.removeFromLeft (half));
        actions.removeFromLeft (6);
        secondaryButton.setBounds (actions);
    }
    else
    {
        primaryButton.setBounds (actions);
    }

    area.removeFromBottom (10);
    gainStrip.setBounds (area.removeFromBottom (16));
    area.removeFromBottom (6);
    auto playRow = area.removeFromBottom (20);
    playButton.setBounds (playRow.removeFromLeft (64));
    playRow.removeFromLeft (4);
    autoButton.setBounds (playRow.removeFromLeft (52));
    area.removeFromBottom (10);

    waveArea = mode == Mode::audio || mode == Mode::multi
        ? area.removeFromTop (juce::jmin (72, area.getHeight() / 2))
        : juce::Rectangle<int>();
    if (! waveArea.isEmpty())
        area.removeFromTop (8);
    metaArea = area;
}

void PreviewPane::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().surface0);

    if (mode == Mode::none)
    {
        g.setColour (getTheme().text0.withAlpha (0.9f));
        g.setFont (IntersectLookAndFeel::makeFont (9.5f));
        g.drawFittedText ("Select a file to see and hear it.\n\n"
                          "Double-click or Return adds audio to the kit. Hold Shift to load it instead, replacing the kit.\n\n"
                          "Drag files onto the sample lane to add them.",
                          getLocalBounds().reduced (16, 24), juce::Justification::centredTop, 12, 1.0f);
        return;
    }

    // Title
    juce::String title;
    juce::String subtitle;
    switch (mode)
    {
        case Mode::multi:
        {
            title = juce::String ((int) selection.size()) + " items selected";
            const int audio = countSelectedAudio();
            subtitle = audio > 0 ? juce::String (audio) + (audio == 1 ? " audio file" : " audio files") : "No audio files";
            break;
        }
        case Mode::preset:
            title = focus.file.getFileNameWithoutExtension();
            subtitle = "INTERSECT preset";
            break;
        case Mode::folder:
            title = focus.file.getFileName().isNotEmpty() ? focus.file.getFileName() : focus.file.getFullPathName();
            subtitle = "Folder";
            break;
        case Mode::audio:
            title = focus.file.getFileName();
            subtitle = focus.file.getFileExtension().substring (1).toUpperCase() + " audio";
            break;
        case Mode::none:
            break;
    }

    const auto titleFont = IntersectLookAndFeel::makeFont (11.0f, true);
    const bool twoLineTitle = juce::GlyphArrangement::getStringWidth (titleFont, title) > (float) titleArea.getWidth();
    g.setColour (getTheme().text2);
    g.setFont (titleFont);
    g.drawFittedText (title, titleArea, juce::Justification::topLeft, 2, 0.9f);

    // A one-line title leaves its second line free; tuck the subtitle up under it.
    const auto subtitleBounds = twoLineTitle ? subtitleArea : subtitleArea.withY (titleArea.getY() + 15);
    g.setColour (getTheme().text1);
    g.setFont (IntersectLookAndFeel::makeFont (8.8f));
    g.drawText (subtitle, subtitleBounds, juce::Justification::centredLeft, true);

    if (! waveArea.isEmpty())
        paintWaveform (g, waveArea);

    paintMeta (g, metaArea);
}

void PreviewPane::paintWaveform (juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto box = area.toFloat();
    g.setColour (getTheme().surface1);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (getTheme().surface4.withAlpha (0.8f));
    g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);

    const bool focusIsAudio = hasFocus && focus.kind == Browser::FileKind::audio;
    const bool clipMatches = focusIsAudio && audition.clip != nullptr
                             && Browser::samePath (audition.clip->file, focus.file);

    if (clipMatches)
    {
        const auto& peaks = audition.clip->peaks;
        const auto inner = area.reduced (4, 5);
        const float midY = (float) inner.getCentreY();
        const float halfH = (float) inner.getHeight() * 0.5f;
        const int columns = juce::jmax (1, inner.getWidth());
        const bool showPlayhead = auditionMatchesFocus()
                                  && (audition.state == AuditionStatus::State::playing
                                      || audition.state == AuditionStatus::State::paused);
        const int playedColumns = showPlayhead ? juce::roundToInt (audition.position * (float) columns) : 0;

        for (int x = 0; x < columns; ++x)
        {
            const size_t first = peaks.size() * (size_t) x / (size_t) columns;
            const size_t last = juce::jmax (first + 1, peaks.size() * (size_t) (x + 1) / (size_t) columns);
            float peak = 0.0f;
            for (size_t i = first; i < juce::jmin (last, peaks.size()); ++i)
                peak = juce::jmax (peak, peaks[i]);

            const float h = juce::jmax (0.5f, peak * halfH);
            g.setColour (x < playedColumns ? getTheme().accent.withAlpha (0.9f) : getTheme().waveform.withAlpha (0.75f));
            g.fillRect ((float) (inner.getX() + x), midY - h, 1.0f, h * 2.0f);
        }

        if (showPlayhead)
        {
            g.setColour (getTheme().accent);
            g.fillRect ((float) (inner.getX() + playedColumns), (float) inner.getY(), 1.0f, (float) inner.getHeight());
        }

        if (audition.clip->truncated)
        {
            g.setFont (IntersectLookAndFeel::makeFont (7.8f));
            g.setColour (getTheme().text1);
            g.drawText ("first " + Browser::formatLength (audition.clip->previewSeconds),
                        area.reduced (5, 2), juce::Justification::topRight, false);
        }
        return;
    }

    juce::String message;
    if (auditionMatchesFocus())
    {
        switch (audition.state)
        {
            case AuditionStatus::State::decoding: message = "Loading preview..."; break;
            case AuditionStatus::State::failed:   message = "Can't preview this file"; break;
            case AuditionStatus::State::idle:
            case AuditionStatus::State::playing:
            case AuditionStatus::State::paused:
            case AuditionStatus::State::stopped:  break;
        }
    }
    if (message.isEmpty() && focusIsAudio)
        message = autoPlay ? "" : "Press PLAY to preview";

    g.setColour (getTheme().text0.withAlpha (0.4f));
    g.fillRect (area.reduced (6, 0).withHeight (1).withY (area.getCentreY()));
    if (message.isNotEmpty())
    {
        g.setColour (getTheme().text1);
        g.setFont (IntersectLookAndFeel::makeFont (9.0f));
        g.drawText (message, area.reduced (6, 0).withTrimmedBottom (area.getHeight() / 2 + 2),
                    juce::Justification::centred, true);
    }
}

void PreviewPane::paintMeta (juce::Graphics& g, juce::Rectangle<int> area) const
{
    if (! hasFocus || mode == Mode::multi)
        return;

    if (mode == Mode::folder)
    {
        g.setColour (getTheme().text0);
        g.setFont (IntersectLookAndFeel::makeFont (8.8f));
        g.drawFittedText (focus.file.getFullPathName(), area.removeFromTop (48), juce::Justification::topLeft, 4, 1.0f);
        return;
    }

    const auto* fileInfo = info.find (focus.file);
    if (fileInfo == nullptr)
    {
        g.setColour (getTheme().text0);
        g.setFont (IntersectLookAndFeel::makeFont (9.0f));
        g.drawText ("Reading...", area.removeFromTop (15), juce::Justification::centredLeft, false);
        return;
    }

    if (! fileInfo->readable)
    {
        g.setColour (getTheme().color5);
        g.setFont (IntersectLookAndFeel::makeFont (9.0f));
        g.drawText (mode == Mode::preset ? "Not a readable preset" : "Not a readable audio file",
                    area.removeFromTop (15), juce::Justification::centredLeft, false);
        return;
    }

    if (mode == Mode::preset)
    {
        drawLabelValue (g, area, "Samples", juce::String (fileInfo->presetSampleCount));
        drawLabelValue (g, area, "Embedded", fileInfo->presetEmbedsSamples ? "Yes" : "No");
        drawLabelValue (g, area, "Size", Browser::formatSize (fileInfo->sizeBytes));
        area.removeFromTop (6);

        g.setFont (IntersectLookAndFeel::makeFont (9.0f));
        const int maxLines = juce::jmax (0, area.getHeight() / 14);
        const int count = fileInfo->presetSampleNames.size();
        for (int i = 0; i < count && i < maxLines; ++i)
        {
            auto line = area.removeFromTop (14);
            const bool lastSlot = i == maxLines - 1 && count > maxLines;
            g.setColour (getTheme().text1);
            if (lastSlot)
            {
                g.drawText ("+ " + juce::String (count - i) + " more", line, juce::Justification::centredLeft, true);
                break;
            }
            BrowserIcons::draw (g, BrowserIcons::Icon::audio, line.removeFromLeft (12).toFloat().reduced (1.0f),
                                getTheme().waveform.withAlpha (0.6f));
            line.removeFromLeft (5);
            g.drawText (fileInfo->presetSampleNames[i], line, juce::Justification::centredLeft, true);
        }
        return;
    }

    drawLabelValue (g, area, "Length", Browser::formatLength (fileInfo->lengthSeconds));
    drawLabelValue (g, area, "Rate", juce::String (fileInfo->sampleRate / 1000.0, 1) + " kHz");
    juce::String format = channelsText (fileInfo->numChannels);
    if (fileInfo->bitsPerSample > 0)
        format << ", " << fileInfo->bitsPerSample << "-bit" << (fileInfo->floatingPoint ? " float" : "");
    drawLabelValue (g, area, "Format", format);
    drawLabelValue (g, area, "Size", Browser::formatSize (fileInfo->sizeBytes));
}

void PreviewPane::GainStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float norm = (owner.gainDb - kMinGainDb) / (kMaxGainDb - kMinGainDb);

    g.setColour (getTheme().surface1);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (getTheme().accent.withAlpha (0.28f));
    g.fillRoundedRectangle (bounds.withWidth (bounds.getWidth() * norm), 3.0f);
    g.setColour (getTheme().surface4.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    auto text = getLocalBounds().reduced (6, 0);
    g.setFont (IntersectLookAndFeel::makeFont (7.8f, true));
    g.setColour (getTheme().text1);
    g.drawText ("VOL", text, juce::Justification::centredLeft, false);
    g.setFont (IntersectLookAndFeel::makeFont (8.8f));
    g.setColour (getTheme().text2.withAlpha (0.9f));
    g.drawText (juce::String (owner.gainDb, 1) + " dB", text, juce::Justification::centredRight, false);
}

void PreviewPane::GainStrip::setFromX (int x)
{
    const float norm = juce::jlimit (0.0f, 1.0f, (float) x / (float) juce::jmax (1, getWidth()));
    const float db = kMinGainDb + norm * (kMaxGainDb - kMinGainDb);
    owner.setGainFromUser (std::round (db * 2.0f) * 0.5f);
}

void PreviewPane::GainStrip::mouseDown (const juce::MouseEvent& e)
{
    setFromX (e.x);
}

void PreviewPane::GainStrip::mouseDrag (const juce::MouseEvent& e)
{
    setFromX (e.x);
}

void PreviewPane::GainStrip::mouseDoubleClick (const juce::MouseEvent&)
{
    owner.setGainFromUser (kDefaultGainDb);
}

void PreviewPane::GainStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const float delta = std::abs (wheel.deltaY) > 1.0e-6f ? wheel.deltaY : wheel.deltaX;
    if (std::abs (delta) > 1.0e-6f)
        owner.setGainFromUser (owner.gainDb + (delta > 0.0f ? 0.5f : -0.5f));
}
