---
title: Controls and shortcuts
nav_order: 5
description: "Every button, parameter, gesture, and keyboard shortcut in the INTERSECT editor."
---

# Controls and shortcuts reference

## Header bar

| Control | Function | Notes |
| --- | --- | --- |
| Status text | Copy warning/error message | Only active when a warning or error is being shown |
| `UNDO / REDO` | History navigation | Buttons in the header |
| `PANIC` | Kill active voices immediately | Also stops lazy chop |
| `FILES` | Toggle the built-in sample browser side panel | See [Sample browser]({{ site.baseurl }}{% link interface.md %}#sample-browser) |
| `SET` | Theme, scale, NRPN, middle-C, and stem-separation popup | See [NRPN MIDI routing]({{ site.baseurl }}{% link nrpn-midi.md %}#set-button--nrpn-settings) and the [Settings file]({{ site.baseurl }}{% link settings-file.md %}) reference for what gets persisted |

## Signal chain bar

### Context bar

| Control | Function | Notes |
| --- | --- | --- |
| `SLICES` | Slice count | Always visible on the right side of the context bar |
| Global `ROOT` | Root note for new slices | Always visible on the right side of the context bar; editable only before any slices exist |

### Time/Pitch module

| Control | Function | Notes |
| --- | --- | --- |
| `BPM` | Tempo reference | `20` to `999` |
| `SET BPM` | Calculate BPM from duration menu | 16 bars to 1/16 note |
| `PITCH` | Semitone shift | `-48` to `+48 st` |
| `TUNE` | Fine detune | `-100` to `+100 ct` |
| `ALGO` | Playback algorithm | `Repitch`, `Signalsmith`, `Bungee` |
| `MODE` | Repitch interpolation mode | `Repitch` only: `Linear` or `Cubic` |
| `TONAL` | Tonality limit | Signalsmith only |
| `FMNT` | Formant shift | Signalsmith only |
| `FMNT C` | Formant compensation | Signalsmith only |
| `GRAIN` | Grain mode | Bungee only: `Fast`, `Normal`, `Smooth` |
| `STRETCH` | Tempo-sync stretch toggle | Works with the selected algorithm |

Time/Pitch notes:
- When `ALGO=Repitch` and `STRETCH=ON`, `PITCH` and `TUNE` become BPM-derived read-only displays.
- In `SLICE` mode, the context row also exposes the selected slice's `NOTE`/`RANGE` mapping.

### Slice context row

| Control | Function | Notes |
| --- | --- | --- |
| `NOTE / RANGE` | Switch between single-note and note-range triggering | Slice-only, shown in the context bar |
| `NOTE` | Single trigger note | Shown only in note mode; drag to edit |
| `LOW` | Lowest note in the trigger range | Shown only in range mode |
| `HIGH` | Highest note in the trigger range | Shown only in range mode |
| `ROOT` | Transposition and filter key-track reference note for the slice | Shown only in range mode |
| Note name text | Read-only pitch name display | Appears after the numeric field(s) and is not draggable |

### Filter module

| Control | Function | Notes |
| --- | --- | --- |
| `ON` | Enable/disable the filter | Per-voice filter toggle |
| `TYPE` | Filter mode | `LP`, `HP`, `BP`, `NT` |
| `SLOPE` | Filter steepness | `12 dB` or `24 dB` |
| `CUT` | Base cutoff frequency | `20 Hz – 20 kHz`, log-skewed at 1 kHz |
| `RESO` | Resonance amount | `0–100%`. Higher values emphasize the cutoff region |
| `DRIVE` | Pre-filter saturation | `0–100 dB`. Adds harmonics before filtering |
| `ASYM` | Drive asymmetry | `0–100%`. Biases waveshaper toward even harmonics for a warmer tone |
| `KEY` | Key tracking amount | `0–100%`, relative to the slice/root note mapping |
| `ATK / DEC / SUS / REL` | Filter envelope shape | A/D/R `0–10000 ms`, S `0–100%`. Separate from the amp envelope |
| `AMT` | Filter envelope depth | `−96 to +96 st`. Bipolar semitone offset applied to cutoff |

Filter notes:
- Start with `ON`, `TYPE=LP`, modest `RESO`, and a lower `CUT` to hear the filter clearly.
- Raise `DRIVE` if you want a dirtier or more aggressive tone before the filter stage. Add `ASYM` to bias the saturation toward even harmonics for a warmer, tube-like character.
- Use `KEY` when you want higher MIDI notes to sound brighter and lower notes darker. In slice range mode, the filter tracks from that slice's `ROOT` note.
- Use positive `AMT` for a classic opening filter envelope and negative `AMT` for an inverted sweep.
- `AMT` is in semitones because it controls octave-style movement of cutoff. `+12 st` doubles the cutoff, `-12 st` halves it.
- Filter settings resolve at note-on, so changing cutoff while a note is already playing affects the next note rather than re-tuning the current voice.

### Amp module

| Control | Function | Notes |
| --- | --- | --- |
| `ATK / DEC / SUS / REL` | Amp envelope | ATK `0–1000 ms`, DEC/REL `0–5000 ms`, SUS `0–100%` |
| `TAIL` | Release-tail toggle | Allows playback to continue past slice boundary during release |
| `GAIN` | Output gain | `−100` to `+24 dB` |

### Playback module

| Control | Function | Notes |
| --- | --- | --- |
| `REV` | Reverse playback | Toggle |
| `LOOP` | Loop mode | `OFF`, `LOOP`, `PP` |
| `FADE` | Loop crossfade amount | Active when `LOOP` is `LOOP` or `PP` |
| `MUTE` | Mute group | `0–32`, `0` = off. Voices in the same group choke each other |
| `1SHOT` | One-shot playback | Ignores note-off until the slice ends |
| `OUT` | Output bus | `SLICE` mode only, `1` to `16` |
| `VOICES` | Max playable voices | `SAMPLE` mode only, `1` to `31` (voice 32 is reserved for the preview voice) |

## Master and global controls

These live outside the four signal-chain modules but are exposed as APVTS parameters for DAW automation. See the [Parameter reference]({{ site.baseurl }}{% link parameter-reference.md %}) for the full list.

| Parameter | Function | Range / values |
| --- | --- | --- |
| `masterVolume` | Master output gain | `−100` to `+24 dB` |
| `maxVoices` | Active voice limit (same as `VOICES` above) | `1` to `31` |
| `uiScale` | Editor scaling factor | `0.5` to `3.0`, step `0.25`. Edit via **SET → Scale Up / Scale Down**. |

## Action bar

| Button | Function |
| --- | --- |
| `ADD` | Toggle draw-slice mode (shows a waveform hint to drag and create a slice) |
| `LAZY` / `STOP` | Start/stop real-time lazy chopping |
| `AUTO` | Open Auto Chop panel for the selected slice (prompts you to select a slice first if none is selected) |
| `COPY` | Duplicate selected slice |
| `DEL` | Delete the selected slice, or the selected sample when the sample lane was the last thing clicked |
| `ZX` | Snap edits to nearest zero crossing |
| `FM` | Follow MIDI (auto-select played slice) |
| `RESEQ` | Resequence MIDI note assignments (opens overlay with `BY POSITION` or `AS CREATED`) |

## Auto chop panel

Requires a selected slice before opening.

All three parameter cells support drag-to-edit (drag up/down) and double-click text entry.

| Control | Function |
| --- | --- |
| `SENS` | Transient detection threshold (`0–100%`) with live marker preview |
| `MIN` | Minimum slice length (`20–500 ms`) — suppresses transients too close together |
| `SPLIT TRANSIENTS` | Split selected slice at detected transients |
| `DIV` | Equal split count (`2–128`) |
| `SPLIT EQUAL` | Split selected slice into equal divisions |
| `CANCEL` | Close panel without applying |

## Waveform and mouse gestures

| Gesture | Result |
| --- | --- |
| Drag-and-drop file | Load sample session |
| Drag-and-drop file onto loaded session | Append to current session |
| Click sample in sample lane | Select sample |
| Drag sample in sample lane | Reorder session samples |
| Click slice | Select slice |
| Click empty waveform in `ADD` mode | Begin draw-slice gesture |
| Drag `S` / `E` edge handles | Resize selected slice |
| Drag inside selected slice | Move slice |
| `Ctrl` + drag selected slice | Duplicate slice to new position |
| `Alt` + drag waveform | Temporary draw-slice gesture |
| Press `ADD` / `Shift + A` | Enters draw mode and shows an on-waveform hint |
| `Shift` + click waveform | Preview from clicked sample position |
| Mouse wheel | Cursor-anchored zoom |
| `Shift` + mouse wheel | Horizontal scroll |
| Middle-button drag | Combined horizontal scroll + vertical zoom |
| Drag in time / zoom bar | Horizontal drag scrolls, vertical drag zooms |

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl/Cmd + Z` | Undo |
| `Ctrl/Cmd + Shift + Z` | Redo |
| `Ctrl/Cmd + =` | UI scale up (0.25 steps) |
| `Ctrl/Cmd + -` | UI scale down (0.25 steps) |
| `Shift + A` | Toggle `ADD` mode |
| `Shift + Z` | Toggle `LAZY` / `STOP` |
| `Shift + C` | Toggle Auto Chop panel |
| `Shift + D` | Duplicate selected slice |
| `Delete` / `Backspace` | Delete selected slice, or selected sample when the sample lane was the last thing clicked |
| `Shift + X` | Toggle `ZX` |
| `Shift + F` | Toggle `FM` |
| `Right Arrow` or `Tab` | Select next slice |
| `Left Arrow` or `Shift + Tab` | Select previous slice |
| `Esc` | Close Auto Chop panel |
| `Return` | Load the selected file(s) (when the sample browser has focus) |
| `Backspace` | Browser: navigate up one directory level (when the sample browser has focus) |

Single-letter action shortcuts are intentionally unbound so DAW keyboard-MIDI note entry remains available.
