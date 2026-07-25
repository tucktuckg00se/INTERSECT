---
title: Troubleshooting
nav_order: 12
description: "Symptom-first troubleshooting for INTERSECT — install, audio, MIDI, stem separation, and theme problems."
---

# Troubleshooting

Find your symptom in the list, then follow the fix. If you don't see your issue, please [open an issue](https://github.com/tucktuckg00se/INTERSECT/issues) with your OS, DAW, INTERSECT version (visible at the bottom of **SET**), and a short description of what you tried.

## Install & loading

### My DAW doesn't see the plugin after I install it
- Confirm the plugin landed in the correct folder for your format. See [Installation → Plugin install paths]({{ site.baseurl }}{% link installation.md %}#plugin-install-paths).
- Rescan plugins in your DAW (most DAWs have a "Rescan VST3" or "Scan for new plug-ins" command).
- Restart the DAW.

### macOS says "INTERSECT is damaged and can't be opened"
INTERSECT release builds are unsigned. macOS quarantines unsigned plugins on first launch. Clear the flags:

```bash
xattr -cr ~/Library/Audio/Plug-Ins/VST3/INTERSECT.vst3
xattr -cr ~/Library/Audio/Plug-Ins/Components/INTERSECT.component
xattr -cr /Applications/INTERSECT.app
```

Then relaunch the DAW (or the standalone app).

### The UI is too small or too large
Open **SET** in the header bar and use **Scale Up / Scale Down**. The UI scales from `0.5x` to `3.0x` in `0.25` steps. The chosen scale is saved per user, not per session.

### Plugin loads but the editor shows missing samples
INTERSECT stores sample file paths in the session, not the audio data itself. If a file has moved or been renamed, the editor shows a missing-sample warning. Either move the file back to its original path, or load the file again into the same session slot.

## Audio & playback

### A slice doesn't make sound when I play its MIDI note
Check, in order:

1. **MIDI routing.** Confirm your DAW track is routed to INTERSECT and the track is armed.
2. **Note mapping.** The slice's `NOTE` (or `LOW`–`HIGH` range) must include the note you're playing. The **FM** button auto-selects the slice for the note you just played — turn it on temporarily to confirm.
3. **Mute group.** If two slices share the same mute group, the latest note-on chokes earlier ones.
4. **Voice budget.** If many notes are held, low-priority voices get stolen. Raise the `VOICES` parameter (1–31).
5. **Master gain.** Check the Amp module's `GAIN` and the filter `CUT` — if cutoff is at the floor and the filter is on, the slice will be silent.
6. **PANIC.** If something appears stuck, press `PANIC` in the header bar.

### The sample plays at the wrong tempo when I load a session
Two common causes:

- `STRETCH` is on, but the sample's `BPM` field doesn't match the file's real tempo. Either turn `STRETCH` off, or pick a slice that loops at a known length and use `SET BPM` to recalculate the `BPM` value automatically.
- `ALGO` is `Repitch` with `STRETCH` on, in which case `PITCH` and `TUNE` become BPM-derived read-only displays. This is intentional — switch to `Signalsmith` or `Bungee` if you want independent pitch control.

### Bungee playback sounds metallic
Bungee is a granular engine. For sustained, tonal material, set `GRAIN` to `Smooth`. `Fast` is best for transient/percussive material.

### Changing the filter cutoff while a note is held doesn't change the sound
Filter parameters resolve at note-on, so live cutoff edits only affect the next note triggered. This is by design — automate via your DAW's note-on events instead of mid-note CC sweeps.

### Notes don't stop when the host stops transport
INTERSECT responds to `All Notes Off` (CC 123) and `All Sound Off` (CC 120). Some hosts don't send these on stop. If you hit this, the `PANIC` button is the manual equivalent.

### A loop has a click or seam at the loop point
Raise the `FADE` parameter in the Playback module — it sets the equal-power crossfade length as a percentage of the slice. Start around 5–10% for short loops; longer loops can tolerate more.

## MIDI

### NRPN editing doesn't work in Ableton Live
This is a known host-side limitation. Ableton intercepts the NRPN data bytes (CC 96/97) before the plugin sees them, so the plugin never gets the increment/decrement events. There is no workaround on the INTERSECT side. See [NRPN MIDI routing]({{ site.baseurl }}{% link nrpn-midi.md %}#daw-specific-notes) for details.

### NRPN edits work in standalone but not when hosted
Some DAWs strip NRPN CCs from MIDI tracks routed to plugins. Try sending the same MIDI directly to the standalone build first — if that works, the issue is host MIDI routing, not INTERSECT.

### Slice doesn't trigger from MIDI even though everything looks right
- Is the slice you expect actually selected? Toggle `FM` and play the note to confirm.
- Check whether another slice with the same mute group fired immediately after the one you intended.
- If you're using slice note ranges, the slice's `LOW`–`HIGH` must contain the note. Slice ranges are ignored for `Repitch` + `STRETCH` (those are trigger-zone only).

## Stem separation

### "ONNX Runtime not found" or START is disabled
You need to download and activate an ONNX Runtime bundle. Open **SET → Stem Separation → ONNX Runtime** and pick the bundle for your platform (CPU is the simplest if you're unsure). Restart INTERSECT after the bundle installs. See [Installation → Stem separation setup]({{ site.baseurl }}{% link installation.md %}#stem-separation-setup).

### START says "No models"
After the runtime is installed, you still need to download at least one stem model. Open **SET → Stem Separation → Download Models** and pick a BS-RoFormer model.

### CUDA bundle fails to load on Linux
Verify that `libcudart.so.12` (or `.so.13`, depending on the bundle you picked) and `libcudnn.so.9` are present and findable by the dynamic loader:

```bash
ldconfig -p | grep -E 'cudart|cudnn'
```

If the standalone INTERSECT app can use CUDA but a Flatpak-hosted DAW can't, see [Installation → Linux NVIDIA CUDA]({{ site.baseurl }}{% link installation.md %}#gpu-runtime-requirements-per-onnx-runtime-bundle) for the Flatseal `LD_LIBRARY_PATH` workaround.

### Stem export is greyed out on Intel macOS
ONNX Runtime 1.24 dropped x86_64 macOS support, so stem separation isn't available on Intel Macs. Apple Silicon is fully supported. The stem panel surfaces this as "Not available" instead of failing on START.

### Stem export is slow on CPU
That's expected — BS-RoFormer is compute-heavy. On a modern desktop CPU, expect minutes per minute of audio. A supported GPU bundle is dramatically faster; see [Installation → Stem separation setup]({{ site.baseurl }}{% link installation.md %}#stem-separation-setup).

## Themes

### My custom theme doesn't appear in the SET → Themes list
- The file must end in `.intersectstyle`.
- The file must be in the user themes folder, not a subfolder. See [Themes → Where themes live]({{ site.baseurl }}{% link themes.md %}#where-themes-live).
- **Restart the plugin** — themes are read once at startup, not live.

### My theme loads but the colours look wrong
You may have mixed legacy keys (`background:`, `lockActive:`, etc.) with modern keys (`surface1:`, `color5:`). When both are present, the modern key wins. Settle on one set — preferably the modern keys. See [Themes → Legacy keys]({{ site.baseurl }}{% link themes.md %}#legacy-keys).

### Colours look fine in the editor but wrong in the popup menus
Popup menus draw on `surface2`. If your theme has a similar `surface2` and `surface4`, hover highlights look flat. Increase the contrast between those two values.

## Sessions

### A session I saved no longer recalls
INTERSECT writes a state version on every save. Loads accept the current version and several versions back (currently v19 through v24). If you've downgraded the plugin to an older build than the one that saved the session, the old build may refuse to load it. Upgrade to the newer build and the session will load.

### After loading a session, the speed/pitch of a sample is off
This happens when the project sample rate differs from what the sample was recorded at. INTERSECT resamples on load and corrects for this — make sure your project sample rate is set before opening the session, not after.

## Still stuck

[Open a GitHub issue](https://github.com/tucktuckg00se/INTERSECT/issues) with:

- INTERSECT version (visible at the bottom of the **SET** popup)
- Operating system + version
- DAW + version (or "standalone")
- A short description of the steps that reproduce the problem
- Any text from the header status bar (click warning/error text to copy it to the clipboard)
