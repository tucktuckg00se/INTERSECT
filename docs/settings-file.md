---
title: Settings file
nav_order: 10
description: "Where INTERSECT writes user settings and what each key does — themes, scale, NRPN, sample-browser bookmarks, stem model folder."
---

# Settings file

INTERSECT persists user-wide preferences (the ones that aren't part of a saved session) to a single file: `settings.yaml`. This includes the active theme, UI scale, NRPN settings, sample browser bookmarks, and the stem separation model folder.

## Where it lives

| OS | Settings file |
| --- | --- |
| Windows | `%APPDATA%\Roaming\INTERSECT\settings.yaml` |
| macOS | `~/Library/Application Support/INTERSECT/settings.yaml` |
| Linux | `~/.config/INTERSECT/settings.yaml` |

The folder is created on first launch. `themes/`, downloaded ONNX Runtime bundles, and downloaded stem models also live alongside it.

## When it's written

`settings.yaml` is rewritten whenever you change any of the persisted preferences from inside the plugin:

- Adjust UI scale in **SET**
- Pick a different theme in **SET → Themes**
- Change middle-C octave in **SET**
- Toggle the sample browser (`FILES`) or change browser bookmarks
- Change any NRPN setting in **SET → NRPN Settings**
- Select a stem model folder or compute device in **SET → Stem Separation**

If the settings folder can't be created (sandboxed plugin host, read-only home), the save is skipped silently — INTERSECT still runs with the in-memory defaults.

## Format

The file is a simple line-oriented `key: value` format with one minor structure: bookmarks are written as a nested list.

```yaml
uiScale: 1.50
theme: oc
nrpnEnabled: false
nrpnChannel: 0
nrpnBlockCc: true
middleC: 4
sampleBrowserVisible: true
sampleBrowserBookmarks:
  - /home/you/samples/breaks
  - /home/you/samples/oneshots
stemModelFolder: /home/you/.config/INTERSECT/stem-models
stemComputeDevice: cuda
```

## Key reference

| Key | Type | Values | Purpose |
| --- | --- | --- | --- |
| `uiScale` | float | 0.5 to 3.0 (in 0.25 steps) | Desired editor scale factor. Mirrors the `uiScale` APVTS parameter on load. The plugin renders at the largest scale that fully fits your display, so on small monitors the on-screen size may be smaller than this value — the `SET` menu then shows it as "fits …x". |
| `theme` | string | filename stem of a theme in your `themes/` folder | Active theme. Falls back to `dark` if the named file isn't present. |
| `nrpnEnabled` | bool | `true` / `false` | Master toggle for NRPN slice editing. |
| `nrpnChannel` | int | 0 to 16 (0 = omni) | MIDI channel filter for NRPN editing. |
| `nrpnBlockCc` | bool | `true` / `false` | If true, NRPN edit CCs are stripped from MIDI output instead of passing through. |
| `middleC` | int | 3, 4, or 5 | Which octave the note name display calls middle C. Doesn't change stored MIDI data. |
| `sampleBrowserVisible` | bool | `true` / `false` | Whether the `FILES` browser side panel is open. |
| `sampleBrowserBookmarks` | list | YAML-style nested list of paths | Pinned folders shown in the sample browser. |
| `stemModelFolder` | string | filesystem path | Folder INTERSECT scans for ONNX stem models. |
| `stemComputeDevice` | string | `cpu`, `cuda`, `migraphx`, `directml`, `coreml` (depends on platform) | Active stem-separation device. |

## Legacy keys

| Old key | Behaviour |
| --- | --- |
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
- [NRPN MIDI routing]({{ site.baseurl }}{% link nrpn-midi.md %}) — what the `nrpn*` keys control.
- [Installation → Stem separation setup]({{ site.baseurl }}{% link installation.md %}#stem-separation-setup) — what the `stem*` keys point at.
- [Parameter reference]({{ site.baseurl }}{% link parameter-reference.md %}) — for in-session parameters (those live in the saved project, not in `settings.yaml`).
