---
title: Settings file
nav_order: 10
description: "Where INTERSECT writes user settings and what each key does — themes, scale, NRPN, file-browser bookmarks and folders, audition, stem model folder."
---

# Settings file

INTERSECT persists user-wide preferences (the ones that aren't part of a saved session) to a single file: `settings.yaml`. This includes the active theme, UI scale, NRPN settings, file-browser bookmarks, recent and last-used folders, audition settings, the custom presets folder, the stem separation model folder, and the Import from URL save folder and update channel.

## Where it lives

| OS | Settings file |
| --- | --- |
| Windows | `%APPDATA%\Roaming\INTERSECT\settings.yaml` |
| macOS | `~/Library/Application Support/INTERSECT/settings.yaml` |
| Linux | `~/.config/INTERSECT/settings.yaml` |

The folder is created on first launch. `themes/`, `presets/` (your saved presets), `preset-samples/` (audio unpacked from presets exported with samples), downloaded ONNX Runtime bundles, downloaded stem models, and the Import from URL tools (`url-import/`: yt-dlp, ffmpeg and QuickJS) also live alongside it. On Linux, ONNX Runtime bundles and `url-import/` live in `~/.local/share/INTERSECT/` instead. **SET → URL Import → REMOVE** deletes `url-import/`.

## When it's written

`settings.yaml` is rewritten whenever you change any of the persisted preferences from inside the plugin:

- Adjust UI scale, theme or middle-C octave in **SET → General**
- Browse to another folder in the file browser (`FILES`) — the folder is saved straight away, so other open instances and your next session follow it
- Change browser bookmarks, add or load from a new folder (recent folders), or change `AUTO` or the audition volume
- Change the `FILES` default folder in **SET → Files**, or right-click a folder and choose **Set as Default Folder**
- Change any NRPN setting in **SET → MIDI**
- Select a stem model folder or compute device in **SET → Stems**
- Choose or clear a custom presets folder in **SET → Presets**
- Change how imports are saved, the import folder or the yt-dlp builds in **SET → URL Import**

If the settings folder can't be created (sandboxed plugin host, read-only home), the save is skipped silently — INTERSECT still runs with the in-memory defaults.

Every open INTERSECT (several instances in one project, or in different DAWs) shares this one file. Each instance only writes the settings it changed and keeps everything else as the file has it, so closing one instance never undoes a bookmark, theme or folder change made in another.

## Format

The file is a simple line-oriented `key: value` format with one minor structure: bookmarks are written as a nested list.

```yaml
uiScale: 1.50
theme: oc
nrpnEnabled: false
nrpnChannel: 0
nrpnBlockCc: true
middleC: 4
sampleBrowserBookmarks:
  - /home/you/samples/breaks
  - /home/you/samples/oneshots
browserRecentFolders:
  - /home/you/samples/breaks
browserLastFolder: /home/you/samples/breaks
browserDefaultFolderMode: folder
browserDefaultFolder: /home/you/samples
auditionAutoPlay: true
auditionGainDb: -6.0
stemModelFolder: /home/you/.config/INTERSECT/stem-models
presetFolder: /home/you/Sync/intersect-presets
stemComputeDevice: cuda
urlImportSaveMode: folder
urlImportFolder: /home/you/samples/imports
urlImportChannel: stable
```

## Key reference

| Key | Type | Values | Purpose |
| --- | --- | --- | --- |
| `uiScale` | float | 0.5 to 3.0 (in 0.25 steps) | Desired editor scale factor. Mirrors the `uiScale` APVTS parameter on load. The plugin renders at the largest scale that fully fits your display, so on small monitors the on-screen size may be smaller than this value — **SET → General** then notes the scale it is shown at. |
| `theme` | string | filename stem of a theme in your `themes/` folder | Active theme. Falls back to `dark` if the named file isn't present. |
| `nrpnEnabled` | bool | `true` / `false` | Master toggle for NRPN slice editing. |
| `nrpnChannel` | int | 0 to 16 (0 = omni) | MIDI channel filter for NRPN editing. |
| `nrpnBlockCc` | bool | `true` / `false` | If true, NRPN edit CCs are stripped from MIDI output instead of passing through. |
| `middleC` | int | 3, 4, or 5 | Which octave the note name display calls middle C. Doesn't change stored MIDI data. |
| `sampleBrowserBookmarks` | list | YAML-style nested list of paths | Pinned folders shown under `BOOKMARKS` in the file browser. |
| `browserRecentFolders` | list | YAML-style nested list of paths, newest first (max 8) | Folders you recently added or loaded files from, shown under `RECENT`. |
| `browserLastFolder` | string | filesystem path | The folder last browsed in any INTERSECT instance; updated on every folder change. `FILES` opens here when `browserDefaultFolderMode` is `last`. Falls back to your home folder if it no longer exists. |
| `browserDefaultFolderMode` | string | `last` / `folder` | Where the `FILES` button opens the browser: the last folder used (default), or `browserDefaultFolder` every time. |
| `browserDefaultFolder` | string | filesystem path | The chosen default folder. If it no longer exists, `FILES` opens the last folder used instead. Omitted when none is set. |
| `auditionAutoPlay` | bool | `true` / `false` | Whether selecting a file in the browser previews it (`AUTO`). |
| `auditionGainDb` | float | -24.0 to 6.0 | Browser audition volume in dB (`VOL`). |
| `stemModelFolder` | string | filesystem path | Folder INTERSECT scans for ONNX stem models. |
| `presetFolder` | string | filesystem path | Custom presets folder, listed in the browser alongside the default `presets/` folder. Omitted when none is set. |
| `stemComputeDevice` | string | `cpu`, `cuda`, `migraphx`, `directml`, `coreml` (depends on platform) | Active stem-separation device. |
| `urlImportSaveMode` | string | `folder` / `ask` | Import from URL: save every import to `urlImportFolder` (default), or open a save dialog after each download. |
| `urlImportFolder` | string | filesystem path | Where imports are saved. Omitted when it's the default, `Music/INTERSECT Downloads` (created on the first import). |
| `urlImportChannel` | string | `stable` / `nightly` | Which yt-dlp builds to follow: tested releases (default) or nightly builds, which get site fixes sooner. |

## Legacy keys

| Old key | Behaviour |
| --- | --- |
| `sampleBrowserVisible` | Whether the old side-panel browser was open. Ignored: the file browser always starts closed. |
| `stemModelPath` | Single-file path (pre-folder builds). If present and valid, INTERSECT migrates it by treating the file's parent directory as the new `stemModelFolder` and rewriting the file on next save. |

Old keys you no longer need can be left in the file safely — unknown keys are ignored.

## Recipes

### Reset to defaults

Quit your DAW (or the standalone), delete `settings.yaml`, then start INTERSECT again. The file is recreated the next time you change a setting.

### Move your setup to a new machine

Copy the entire INTERSECT settings folder (the parent of `settings.yaml`) to the same path on the new machine. You'll get your themes, scale, NRPN config, bookmarks, downloaded models, and downloaded ONNX Runtime bundle in one move.

### Share a theme without copying settings

Only copy the file from inside `themes/`. The `theme:` line in someone else's `settings.yaml` is irrelevant to you — INTERSECT looks up the file by name.

## Related

- [Themes]({{ site.baseurl }}{% link themes.md %}) — what the `theme:` value points at.
- [Presets]({{ site.baseurl }}{% link presets.md %}) — what the `presetFolder:` value adds to the browser.
- [NRPN MIDI routing]({{ site.baseurl }}{% link nrpn-midi.md %}) — what the `nrpn*` keys control.
- [Installation → Stem separation setup]({{ site.baseurl }}{% link installation.md %}#stem-separation-setup) — what the `stem*` keys point at.
- [Parameter reference]({{ site.baseurl }}{% link parameter-reference.md %}) — for in-session parameters (those live in the saved project, not in `settings.yaml`).
