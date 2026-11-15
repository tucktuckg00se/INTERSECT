---
title: Concepts
nav_order: 4
description: "The mental model behind INTERSECT — sessions, slices, sample groups, the inheritance/lock system, time/pitch algorithms, filter, and playback."
---

# Concepts

This page explains the core ideas behind INTERSECT. For a hands-on first session, see [Getting started]({{ site.baseurl }}{% link getting-started.md %}). For per-control details, see [Controls and shortcuts]({{ site.baseurl }}{% link controls-reference.md %}).

## Sessions and samples

An INTERSECT instance loads **one or many samples** into a session. The first sample replaces an empty session and resets zoom/scroll; subsequent loads append. Files may be `.wav`, `.ogg`, `.aiff`, `.flac`, `.mp3`, or `.rx2`. REX2 (`.rx2`) loops arrive already sliced: every slice in the file is created for you (with its REX tail) and the loop tempo becomes the BPM.

Loading happens off the audio thread — even a multi-minute file won't drop audio. Files saved at a different sample rate from the project are automatically resampled on load.

You can drag samples directly onto the waveform, or open the file browser with the `FILES` button in the header bar. The browser previews files as you select them; double-click adds a file to the session, and Shift + double-click replaces the session with it. See [File browser]({{ site.baseurl }}{% link interface.md %}#file-browser).

## Slices

A **slice** is a region of one sample with a MIDI mapping and its own parameter overrides. Slices are created manually — INTERSECT never auto-slices on load. Three creation methods:

- **Draw** — press `ADD` (`Shift + A`) and drag on the waveform.
- **Lazy chop** — press `LAZY` (`Shift + Z`) and play MIDI notes; each note-on drops a slice boundary at the current playhead. Stopping closes the last boundary at the sample's end.
- **Auto chop** — select an existing slice and press `AUTO` (`Shift + C`) to split it equally (`SPLIT EQUAL`) or at detected transients (`SPLIT TRANSIENTS`).

Each slice is mapped to a MIDI note. By default the first slice gets C2 (note 36) and subsequent slices receive successive notes. `RESEQ` rewrites those mappings either left-to-right (`BY POSITION`) or by creation order (`AS CREATED`).

![Eight numbered slices across the first half of a sample: coloured blocks in the slice lane and matching regions on the waveform, slice 3 selected]({{ site.baseurl }}/assets/images/concepts-slices.png)
*Slices in the slice lane (numbered blocks) and on the waveform. Click a block to select its slice.*

## Slice note ranges

Each slice can respond to a single MIDI note or a range of notes:

- **Single note** (default): `NOTE` mode. The slice plays only when its assigned note is triggered.
- **Range**: `RANGE` mode. The slice covers `LOW` through `HIGH`, transposing chromatically relative to a `ROOT` note. Filter key-tracking uses the slice's `ROOT` rather than the sample root.

Range mode is what turns INTERSECT from a kit-style slicer into a chromatic instrument. Range transpose is ignored when `ALGO` is `Repitch` with `STRETCH` on — those modes are trigger-zone only.

## Sample groups

A **group** is two or more samples of the same length (within 10 ms) that share one set of slices, like the stems of one song. A group slice plays every sample in the group together on one key, mixed by each sample's mute, level and output. The [Sample groups]({{ site.baseurl }}{% link sample-groups.md %}) guide walks through it with pictures; the rules in short:

- The top sample owns the group's slices, and its sample settings are the group's (the `SAMPLE` tab reads `GROUP`).
- A slice follows the group mix until it gets its own members; then it plays only those, at the group's levels.
- A lane's `M` changes only the slices that follow the group mix. A sample added to the group plays only in those slices too.
- Taking a sample out gives it a copy of the group's slices when there's room; a group left with one sample ends.
- `AUTO`, `ZX`, `LAZY`, Shift+click preview and the `AUDIO` export use the group's mix.
- Up to 16 samples per group and 32 groups. A missing sample plays silence while the rest of the group plays on.

## The inheritance / lock model

This is INTERSECT's central idea, and it's worth a concrete example.

INTERSECT has three levels of settings:

- **Plugin** — shared by everything: `VOICES`, the `ROOT` note for new slices, and your SET preferences.
- **Sample** — each sample in the session has its own settings (BPM, pitch, algorithm, envelope, filter, gain, playback). Changing one sample never affects another.
- **Slice** — a slice uses its sample's settings unless it locks its own value.

The signal chain bar at the bottom of the editor has two tabs: `SAMPLE` (the selected sample's settings) and `SLICE` (overrides for the selected slice).

> **Example.** With no slice selected, set `SAMPLE → PITCH` to `+2`. Every slice of that sample now plays at +2 semitones; other samples in the session are unchanged. Now select one slice, click the `SLICE` tab, and drag its `PITCH` to `+7`. The parameter label turns orange and the bottom row counts the override — that slice has a **lock** on `PITCH`. It plays at +7. All other slices still inherit `+2`.
>
> Change `SAMPLE → PITCH` to `0`. The locked slice still plays at `+7`; everything else now plays at `0`.
>
> Right-click the locked `PITCH` value (or click the highlighted parameter label) to clear the lock. The slice re-inherits the sample default.

![The signal chain on the SLICE tab: PITCH reads +11st with an orange label, and the bottom row reads 1 overrides]({{ site.baseurl }}/assets/images/concepts-lock.png)
*A slice with a lock on `PITCH`: the orange label marks the override.*

This applies to almost every signal-chain parameter, plus per-slice settings like output bus, mute group, and loop mode. The result is that defaults are cheap to set sample-wide, but anything can be locked when a slice needs to differ.

## Time and pitch algorithms

INTERSECT ships three time/pitch engines. Pick one per sample with `ALGO`.

| Algorithm | Independent time & pitch? | Best for | Notable controls |
| --- | --- | --- | --- |
| **Repitch** | No (speed and pitch linked) | Pitched material where the natural time-shift sound is wanted | `MODE` (`Linear` or `Cubic` interpolation) |
| **Signalsmith** | Yes | Transient-rich, full-mix material | `TONAL`, `FMNT`, `FMNT C` |
| **Bungee** | Yes (granular) | Sustained tones; choose `GRAIN` per material | `GRAIN` (`Fast` / `Normal` / `Smooth`) |

**`STRETCH` and `ALGO=Repitch`:** when you enable `STRETCH` on a Repitch sample, `PITCH` and `TUNE` become BPM-derived read-only displays — the engine is now computing pitch from the DAW BPM ÷ slice BPM ratio. Switch to Signalsmith or Bungee if you want independent pitch control while stretched.

## SET BPM

`SET BPM` (in the Time/Pitch module) calculates the sample's BPM from a musical duration. A small box opens: pick "1 Bar", "1/4 Note", "1/8 Note", and so on from the dropdown, and it shows the BPM that length gives before anything changes. `SET` (or `Return`) applies it, computed from the selected slice's length, and auto-locks the BPM value; `CANCEL`, `Esc` or a click outside leaves it as it was. This is the workflow when you know "this loop is 2 bars" but don't know the exact tempo.

Available in both `SAMPLE` and `SLICE` scope.

## Filter

The filter is **per-voice** and resolves its settings at **note-on**. Three implications:

- Live cutoff edits affect the next triggered note, not the currently sounding voice. Automate via note-on events, not mid-note CC sweeps.
- Different slices can have wildly different filter settings without affecting each other.
- The filter envelope is independent from the amp envelope; you can have a fast amp attack with a slow filter sweep.

A few notable parameters:

- **`AMT` is in semitones**, not Hz. `+12 st` means the envelope can push cutoff up by one octave; `-12 st` halves it. This stays consistent across base cutoff values.
- **`KEY` is a percentage** of note tracking. `100%` means cutoff fully follows pitch; `50%` is the conventional "track halfway"; `0%` ignores pitch. In slice range mode, tracking uses the slice's `ROOT` note.
- **`DRIVE` is pre-filter saturation** — it adds harmonics before the filter, so it has tonal character beyond simple gain. **`ASYM`** biases the waveshaper toward even harmonics for a warmer, tube-like tone. When `ASYM > 0`, a one-pole DC blocker engages automatically.

## Playback

- **Loop crossfade (`FADE`)** smooths loop and ping-pong seams with an equal-power crossfade. Active only when `LOOP` is not `Off`. Start at 5–10% for short loops, more for long loops.
- **`1SHOT`** ignores normal note-off — the slice plays through to the end. A host-level "all notes off" still kills it.
- **`REV`** plays the slice backwards. Combine with `LOOP=Ping-Pong` for a back-and-forth pattern.
- **Mute groups** let voices in the same group choke each other (newest wins). Group `0` disables choking. Useful for hi-hat open/closed style pairs.

## Voices and output

INTERSECT has **32 voice slots: 31 playable plus one reserved preview voice** for sample auditioning and lazy chop. When all playable voices are in use, the engine steals the voice whose amp envelope is at the lowest level.

`OUT` (in `SLICE` mode) routes a slice to one of 16 stereo output buses. The main bus (bus 1) is always active; additional buses must be enabled in your DAW's plugin bus layout.

## Undo, redo, MIDI panic

- **Undo/redo** captures snapshots before any change, including drag gestures. Multi-step drags coalesce into a single undo step.
- **`PANIC`** kills all active voices and stops lazy chop immediately.
- **Host stop** — INTERSECT responds to `All Notes Off` (CC 123) and `All Sound Off` (CC 120). If your DAW doesn't send those on stop, use `PANIC`.
