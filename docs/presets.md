---
title: Presets
nav_order: 6.5
description: "Save and load whole INTERSECT kits as .intersectpreset files from the built-in browser, with optional embedded samples for sharing."
---

# Presets

An INTERSECT preset is a complete kit saved as a single `.intersectpreset` file. It holds the loaded samples, every slice, and all parameters, which is everything your DAW project would restore. Presets are separate from your DAW's own plugin presets: you save, browse and load them inside INTERSECT, so they work the same in every host and in the standalone app.

A preset does **not** change your personal preferences. UI scale, theme, NRPN settings and the stem-separation device stay as they are when you load someone else's preset.

## Where presets live

| OS | Presets folder |
| --- | --- |
| Windows | `%APPDATA%\Roaming\INTERSECT\presets\` |
| macOS | `~/Library/Application Support/INTERSECT/presets/` |
| Linux | `~/.config/INTERSECT/presets/` |

The folder sits next to `themes/` and is created the first time you open it or save into it. To install a preset someone sent you, drop the file into this folder (or any folder you browse to). **SET → Presets → Open Presets Folder** opens it in your file manager, which is handy because it's a hidden folder on macOS and Linux.

You can also add a **custom presets folder**, for example one inside a synced drive, from **SET → Presets → Choose Custom Folder...**. It works alongside the default folder: both appear in the browser. **Clear Custom Folder** removes it from INTERSECT without touching the files, and **Open Custom Presets Folder** shows it in your file manager.

## Saving

Click **SAVE** in the header, next to `FILES`. It works from the editor or the browser:

1. The kit is saved straight into your preset library, named after its first sample (`amen-break`, then `amen-break 2`, and so on).
2. The browser opens on the library with the new preset selected and its name highlighted.
3. Type a name and press `Return`, or click away, to keep it. `Esc` keeps the suggested name.

Library presets record where each sample lives on disk, so they stay small.

To file a preset into a subfolder, browse into that folder of your presets folder (or your custom presets folder) first; SAVE puts the new preset where you are. Everywhere else, SAVE uses the default presets folder.

To rename a preset later, right-click it and choose **Rename**, or select it and press `F2`. A name that's already taken is refused.

## Exporting and sharing

Right-click a preset in the browser:

- **Export...** saves a copy anywhere you like, exactly as it is.
- **Export with Samples...** saves a copy with the audio *inside* the preset, so the single file carries the whole kit. Use this when sending a kit to someone else. If a sample can't be found, the header tells you and that sample is left out.

You can also copy preset files straight out of the presets folder (**SET → Presets → Open Presets Folder**).

## Loading

In the browser, presets show a kit icon and their sample count. Selecting one lists its samples in the preview pane. Any of these loads it:

- Double-click it, or select it and press `Return`.
- Click **LOAD PRESET** in the preview pane, or right-click it and choose **Load Preset**.
- Drag it onto the sample lane, or drag it onto the waveform from your file manager.
- Pick it with the folder button's **Open Files...** in the browser.

Loading **replaces** the current kit; it never appends. Press `UNDO` to get the previous kit back. Search in the browser finds presets as well as audio.

You can't load a preset while stem separation is running. Finish or cancel it first.

## How samples are found

When you load a preset, INTERSECT looks for each sample in this order:

1. The original location it was saved from.
2. The same place relative to the preset file. A preset kept next to its samples folder still works after you move both together.
3. A file with the same name next to the preset.
4. The copy embedded in the preset, if it was exported with samples.

For presets that embed samples, a file found in steps 1–3 is only used if its size matches the embedded copy. Otherwise the embedded copy wins, so you always hear the kit as it was saved.

If a sample can't be found anywhere, the header warns you and the usual **MISSING SAMPLE, RELINK REQUIRED** flow lets you point INTERSECT at it.

### The embedded-sample cache

Embedded samples are unpacked to `preset-samples/` next to the presets folder, in one subfolder per file named after its content hash. Loading the same preset again, or another preset that contains the same sample, reuses the existing copy.

Your DAW project then refers to these cached files. If you delete the cache, projects that used embedded presets will ask you to relink; loading the preset again restores the files.

## Compatibility

- Presets from an older INTERSECT version load in newer versions.
- A preset saved by a newer version than yours shows a "needs a newer version of INTERSECT" message instead of loading.
- A damaged or unrelated file shows an error and leaves your current kit untouched.
- Presets can embed at most 2 GB of audio.

## Related

- [Interface → File browser]({{ site.baseurl }}{% link interface.md %}#file-browser): the browser that lists presets.
- [Settings file]({{ site.baseurl }}{% link settings-file.md %}): where the custom presets folder is remembered.
