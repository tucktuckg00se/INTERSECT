---
title: Interface
nav_order: 7
description: "Tour of every visible area of the INTERSECT editor — header bar, sample lane, slice lane, waveform, file browser, time/zoom bar, action bar, and signal chain."
---

# Interface layout

## Header bar

| Area | Function | Notes |
| --- | --- | --- |
| Status text | Shows warnings, errors, and missing-file notices | Click warning/error text to copy the message |
| `UNDO` / `REDO` | History navigation | Same as `Ctrl/Cmd + Z` and `Ctrl/Cmd + Shift + Z` |
| `PANIC` | Kills active voices immediately | Also stops lazy chop |
| `FILES` | Open or close the file browser | Lit while the browser is open. See [File browser](#file-browser) |
| `SAVE` | Save the kit to your preset library | Opens the browser on the new preset with its name ready to edit. See [Presets]({{ site.baseurl }}{% link presets.md %}) |
| `SET` | Open or close the settings view | Lit while open. See [Settings](#settings) |

## Sample lane, slice lane, and waveform

| Area | Function | Notes |
| --- | --- | --- |
| Sample lane | Compact session-sample overview above the slice lane | Reflects selection and zoom; drag to reorder samples; includes per-sample `MIDI`, `STEMS` / `CANCEL` and an `X` that removes the sample (a small question pops up first: `REMOVE` or `Return` confirms, `CANCEL` / `Esc` keeps it; hold `Shift` to skip the question) |
| Slice lane | Compact slice-region overview above the waveform | Reflects selection and zoom |
| Waveform | Main editing surface | Drag-and-drop loading/appending, slice selection, boundary editing, move/duplicate, preview |
| Overlay hints | Contextual help and action prompts | Used by `ADD`, `AUTO`, and other actions |
| Playback cursors | Voice-position display | Shows active playheads |
| Transient preview markers | Auto Chop preview | Dashed markers shown before applying transient split |

## File browser

Click `FILES` in the header to open the browser. It takes over everything below the sample lane, so the header and the sample lane stay visible while you browse. Click `FILES` again, press `CLOSE` (bottom right) or press `Esc` to go back to the editor. The bar along the bottom shows how many items are listed and selected, search progress, and short notes such as where `FILES` will open. The browser always starts closed. By default `FILES` opens it in the last folder you browsed, in this or any other open INTERSECT, and that carries over to your next session. To have it open in the same folder every time, right-click that folder (in the list or the sidebar) and choose **Set as Default Folder**, or pick it in **SET → Files**. `SAVE` and relinking a missing file still open the browser where they need to.

The browser has three columns.

### Places (left)

| Section | What's there |
| --- | --- |
| `PRESETS` | The default presets folder, plus your custom presets folder if one is set in **SET → Presets** |
| `LOCATIONS` | Home, Desktop, Documents, Music and Downloads |
| `BOOKMARKS` | Folders you pinned. Right-click a folder to **Add Bookmark**; right-click a bookmark to remove it |
| `RECENT` | The last 8 folders you added or loaded files from. Right-click one to remove it |
| `DRIVES` | Disks and mounted volumes |

### Files (middle)

| Control | Function | Notes |
| --- | --- | --- |
| `←` / `→` / `↑` / `↻` | Back, forward, up one folder, refresh | Refresh also re-reads file details |
| Path | Click a folder name to jump to it | Click the empty space (or double-click) to type a path |
| Folder button (right of the path) | **Open Files...** picks audio or presets with the system dialog; **Open Folder...** browses to a folder you pick | Audio you open is added to the kit; a preset is loaded |
| Search | Filters the browser down to what matches, in this folder and every subfolder | The search stays on while you browse: each folder shows its matching files plus the subfolders that lead to a match, with a count ("214 matches"), so you click down to them. Folders with no matching sounds inside (empty, or only artwork and documents) are hidden. Linked folders (shortcuts / symlinks) are searched too. A folder whose name matches counts for everything inside it, so `kick` also finds "BD 01.wav" inside "Kicks". Going up past where you started searches from there. Every word must appear, in any order (`808 kick` finds "Kick 808 Hard.wav"); quotes match an exact phrase (`"808 kick"`). `/` or `Ctrl/Cmd + F` jumps here; `Esc` or `×` clears it |
| `NAME` / `LEN` / `RATE` | Click a column header to sort by it; click again to reverse | Folders always stay on top |

Presets show a kit icon and their sample count instead of a length. Right-click a preset to **Load**, **Rename** (or press `F2`), **Export...**, or **Export with Samples...**.

### Preview (right)

Shows what's selected: the file's waveform, length, sample rate, channels, bit depth and size. For a preset, its samples and whether their audio is embedded. For a folder, its path.

| Control | Function | Notes |
| --- | --- | --- |
| `PLAY` / `PAUSE` | Hear the selected file without adding it | Pausing keeps your place; `PLAY` continues from there. Plays on the main output at the audition volume |
| `AUTO` | Play files automatically as you select them | On by default; remembered |
| `VOL` | Audition volume, −24 to +6 dB | Drag or scroll; double-click resets to −6 dB. Remembered |
| `ADD` | Add the selected audio to the kit | The default action |
| `LOAD` | Replace the kit with the selected audio and return to the editor | Clears the current samples and slices; `UNDO` brings them back |

Files longer than two minutes preview their first two minutes. The audition stops when you add or load, change folder, close the browser, or press `PANIC`. Previews only play while your DAW is processing audio.

### Adding and loading

- **Double-click** a file, or select it and press `Return`, to **add** it to the kit. Adding to an empty kit loads it.
- **Shift + double-click** or **Shift + Return** to **load** instead, replacing the kit and closing the browser.
- **Drag** files onto the sample lane to add them. Dropping a preset there loads it.
- Select several files (`Shift`/`Ctrl` + click, or `Shift` + arrows) and use `ADD` or `LOAD` to act on all of them.
- Adding and loading can both be undone with `UNDO`.

### Keyboard

| Key | Action |
| --- | --- |
| `↑` / `↓` | Move through files (previews them when `AUTO` is on) |
| `Space` | Play / pause the selected file's preview |
| `→` | Restart the preview from the top, like a one-shot (never pauses). On a folder, opens it |
| `←` / `Backspace` | Up one folder, landing on the folder you came from |
| `Return` / `Shift + Return` | Add / load the selection |
| `F2` | Rename the selected preset |
| `/` or `Ctrl/Cmd + F` | Search |
| `Esc` | Clear the search, then close the browser |

On a folder or preset with nothing playing, `Space` isn't used by the browser, so it still reaches your DAW (usually its transport).

## MIDI export

Each sample in the sample lane has a `MIDI` button (`M` when the sample is narrow). Drag it into your DAW to drop a MIDI clip of that sample's slices, or click it to save a `.mid` file. Each slice becomes one note on its MIDI key, timed from its position in the sample at the sample's BPM, so playing the clip back through INTERSECT replays the original pattern. REX2 (`.rx2`) loops keep their original groove even though their slices carry extra tails.

## Stem separation

Each sample in the sample lane has a `STEMS` button that opens the stem view for that sample. Like the `FILES` browser, it takes over the editing area below the sample lane: models on the left, the model's stems (grouped, with search) on the right, and device / mode / output / `START` along the bottom. While a job is running on that sample, the button reads `CANCEL`.

For an end-to-end walkthrough — picking a model, device, and mode, then running the export — see the dedicated [Stem separation]({{ site.baseurl }}{% link stem-separation.md %}) page.

## Settings

Click `SET` in the header to open the settings view. Like the browser, it takes over everything below the sample lane; click `SET` again, press `CLOSE` (bottom right) or press `Esc` to go back. The bottom bar also shows the plugin version and has `OPEN SETTINGS FOLDER`, which opens the folder holding `settings.yaml`, themes and presets. Pick a category on the left (the arrow keys also step through them). Changes apply and are saved straight away.

| Page | What's there |
| --- | --- |
| General | UI scale (`−` / `+`, with a note when the window is shown smaller to fit your display), theme, which octave is called middle C, whether a newer INTERSECT is out (`VIEW RELEASE` opens its page), and Discord / support links |
| MIDI | NRPN editing on/off, consume NRPN CCs, and the NRPN channel (`OMNI` = all channels). See [NRPN MIDI routing]({{ site.baseurl }}{% link nrpn-midi.md %}) |
| Files | The default folder for the `FILES` button: **Last used** or a **Chosen folder** |
| Presets | The default presets folder and an optional custom one. See [Presets]({{ site.baseurl }}{% link presets.md %}) |
| Stems | Model folder, the device `STEMS` starts with, ONNX Runtime bundles and model downloads, with progress. See [Stem separation]({{ site.baseurl }}{% link stem-separation.md %}) |

Opening settings asks GitHub whether a newer release exists (at most every 6 hours); nothing else is sent, and INTERSECT never checks at any other time. Everything here is saved in the [settings file]({{ site.baseurl }}{% link settings-file.md %}), not in your session.

## Time / zoom bar

| Area | Function | Notes |
| --- | --- | --- |
| Time ruler | Shows time markings for the current view | Updates with zoom level |
| Drag horizontally | Scroll | Uses the current zoom level |
| Drag vertically | Zoom | Anchored to the drag start position |

## Action bar

| Button | Function | Notes |
| --- | --- | --- |
| `ADD` | Toggle draw-slice mode | Drag on the waveform to create a slice |
| `LAZY` / `STOP` | Start/stop real-time lazy chopping | Label changes while active |
| `AUTO` | Open/close Auto Chop panel | Requires a selected slice |
| `COPY` | Duplicate selected slice | Equivalent to duplicate command |
| `DEL` | Delete selected slice or selected sample | Uses the last lane you interacted with. Removing a sample asks first; `Shift`-click skips the question |
| `ZX` | Snap edits to nearest zero crossing | Toggle |
| `FM` | Follow MIDI note selection | Auto-selects the played slice |
| `RESEQ` | Resequence slice MIDI notes | Opens overlay with `BY POSITION` and `AS CREATED` modes; requires 2+ slices |

## Signal chain bar

The bottom bar is the main parameter editor. It has four modules: `TIME/PITCH`, `FILTER`, `AMP`, and `PLAYBACK`.

**Collapsed mode** (default): `SAMPLE` and `SLICE` tabs switch between scopes, with one parameter strip visible at a time. `SAMPLE` edits the selected sample only: every sample in the session keeps its own settings.

**Expanded mode**: shows both strips simultaneously — slice on top, sample below — with no tabs. Click the chevron toggle on the right edge of the context bar to switch between modes.

**Context bar** (bottom edge):
- `SLICES` count, the sample `ROOT` note and `VOICES` (the plugin-wide voice limit) are always visible on the right. The sample `ROOT` is editable only when no slices exist.
- When a slice is selected: slice sample range, length, a `NOTE`/`RANGE` toggle, numeric note controls, read-only note names, and override count.

```text
┌─[ Tab: SAMPLE | SLICE ]─────────[ slice range · length · NOTE/RANGE ]──────[ SLICES: 8  ROOT: C2  VOICES: 16 ]┐
│  TIME/PITCH        │  FILTER             │  AMP               │  PLAYBACK                                  │
│  BPM PITCH ALGO …  │  TYPE CUT RESO …    │  ATK DEC SUS REL … │  REV LOOP FADE MUTE OUT …                  │
└────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

General behavior:
- Drag up/down on a value to edit it.
- Double-click a value to type it directly.
- In `SLICE` mode, editing a field locks that field for the selected slice when it differs from the sample value. The parameter label highlights to show the lock.
- In `SLICE` mode, clicking a locked field label or right-clicking the field clears that override and re-inherits the sample value.

For each module's individual controls, see the [Controls and shortcuts reference]({{ site.baseurl }}{% link controls-reference.md %}). For the underlying mental model, see [Concepts → The inheritance / lock model]({{ site.baseurl }}{% link workflow-basics.md %}#the-inheritance--lock-model).
