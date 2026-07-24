---
title: Getting started
nav_order: 3
description: "From empty editor to a sliced sample triggered by MIDI in under 10 minutes."
---

# Getting started

This walkthrough takes you from an empty INTERSECT editor to a sliced sample triggered by MIDI from your DAW. It takes about 5–10 minutes.

For the deeper concepts behind what you're doing — multi-sample sessions, the inheritance model, time/pitch algorithms — see [Concepts]({{ site.baseurl }}{% link workflow-basics.md %}) next.

## Before you start

You should already have:

- INTERSECT installed and showing up in your DAW (see [Installation]({{ site.baseurl }}{% link installation.md %}) if not)
- An audio file to play with — a drum loop is ideal, but any sample works (`.wav`, `.ogg`, `.aiff`, `.flac`, `.mp3`)

Add an INTERSECT instance to a MIDI track in your DAW, open the editor, and have your MIDI keyboard or a clip handy.

## 1. Load a sample

Drag your audio file from your file manager onto the waveform area. The waveform fills the editor; the sample lane (the strip above the slice lane) shows a single block representing this sample.

Alternative: click **FILES** in the header to open the built-in sample browser, then double-click a file.

## 2. Draw your first slice

Press `Shift + A` (or click `ADD` in the action bar). A hint appears: "ADD mode: drag on waveform to create a slice."

Drag on the waveform to draw a region. When you release, you have a slice — visible as a coloured region on the waveform and a labelled block in the slice lane.

The first slice gets MIDI note **C2 (note 36)** by default.

## 3. Play the slice

Play C2 on your DAW's keyboard input (or send a note from a MIDI clip). The slice plays back, and you'll see a yellow playhead cursor sweep across it.

If nothing plays, see [Troubleshooting → A slice doesn't make sound]({{ site.baseurl }}{% link troubleshooting.md %}#a-slice-doesnt-make-sound-when-i-play-its-midi-note).

## 4. Adjust a slice parameter (and see how locking works)

The bottom of the editor is the **signal chain bar**. It has two tabs: `SAMPLE` (sample-wide defaults) and `SLICE` (overrides for the selected slice).

1. Click your slice on the waveform to select it.
2. In the signal chain bar, click the `SLICE` tab.
3. Find the `PITCH` parameter in the Time/Pitch module. Drag its value up to `+5`.

The parameter label highlights — that means the slice has a locked override on `PITCH`. Right-click the value (or click the highlighted label) to clear the lock and inherit the global default again.

This is INTERSECT's core idea: edit defaults in `SAMPLE`, then lock per-slice overrides where you want them to differ.

## 5. Chop the sample into even pieces

Manual slicing is fine for a kit, but for a loop you usually want even subdivisions.

1. Click anywhere on your existing slice to select it.
2. Press `Shift + C` (or click `AUTO`). The Auto Chop panel appears at the bottom.
3. Set `DIV` to `8` and click `SPLIT EQUAL`.

You now have 8 slices, each assigned to successive MIDI notes starting at C2.

For transient-based slicing instead, raise `SENS` until the dashed preview markers line up with the onsets in your waveform, then click `SPLIT TRANSIENTS`.

## 6. Resequence MIDI notes by position

If you've been moving slices around, their MIDI notes may no longer match their order on the timeline. Click `RESEQ` in the action bar and pick `BY POSITION` to reassign notes left-to-right.

## 7. Play it from a clip

Back in your DAW, create a MIDI clip on the track and drop a few notes starting at C2. Hit play — each note triggers a slice.

To audition slices while editing, click `FM` (Follow MIDI). The played slice auto-selects in the editor, so the signal chain bar always shows the right slice.

## Next steps

- [Concepts]({{ site.baseurl }}{% link workflow-basics.md %}) — sample sessions, inheritance, the three time/pitch engines, mute groups.
- [Interface]({{ site.baseurl }}{% link interface.md %}) — every panel and area of the editor.
- [Controls and shortcuts]({{ site.baseurl }}{% link controls-reference.md %}) — the full reference, with keyboard shortcuts.
- [Stem separation]({{ site.baseurl }}{% link stem-separation.md %}) — split a sample into drums, bass, vocals, etc. for layered slicing.
