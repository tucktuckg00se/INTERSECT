---
title: Remote control (OSC)
nav_order: 8.5
description: "Drive INTERSECT from a custom controller app (Push 2, Maschine, TouchOSC...) over OSC on localhost: slicing, parameters, waveform, browser."
---

# Remote control protocol (OSC)

Every INTERSECT instance runs a small OSC server on `127.0.0.1`. A controller app such as [Pushtoo](https://github.com/tucktuckg00se/Pushtoo) for Ableton Push 2, a Maschine app, or a TouchOSC layout can use it to do almost everything the editor does without the plugin window open. That covers chopping, slice and sample parameters, the waveform and playheads, browsing and loading samples and presets, auto chop, and stems.

**Notes still travel as MIDI.** Route your controller's MIDI to the INTERSECT track in the DAW as usual. That keeps playing and lazy chop sample-accurate. OSC carries editing and display only.

The protocol is controller-agnostic, so a new controller needs no changes to INTERSECT, only a client.

- Protocol version: **1**
- Transport: OSC 1.0 over UDP, localhost only.
- All addresses start with `/intersect/`. The prefix is left out in the tables below.
- Argument types use OSC tags: `i` int32, `f` float32, `s` string, `b` blob.

Set the environment variable `INTERSECT_REMOTE_CONTROL=0` before starting the host to disable the server.

## Finding an instance

Each instance writes a discovery file and deletes it when the plugin is removed:

| OS | Directory |
| --- | --- |
| Linux | `$XDG_STATE_HOME/intersect/instances/` (default `~/.local/state/intersect/instances/`) |
| macOS / Windows | the INTERSECT settings folder + `instances/` |

Each file is `<instanceId>.json`:

```json
{ "instanceId": "…", "port": 9871, "pid": 12345, "protocolVersion": 1,
  "pluginVersion": "0.15.0", "trackName": "Drums" }
```

`trackName` comes from the host and is empty when the host doesn't report it. Treat files whose `pid` is not running as stale.

**Fallback for sandboxed hosts** (Flatpak, the macOS AU sandbox), whose discovery directory you can't see: servers try ports **9871–9878** first. Send `hello` to each port and wait for `hello/ack`.

## Session

Clients are identified by the UDP port they listen on (`replyPort`). The server always replies to `127.0.0.1:<replyPort>`, from its own port.

| Address | Args | Meaning |
| --- | --- | --- |
| `hello` | `s name, i replyPort` | Subscribe. Reply: `hello/ack`, then the full state. |
| `ping` | `i replyPort` | Keep-alive, at least every 2 s. Clients silent for 6 s are dropped. Reply: `pong`, or `bye` if unknown (say `hello` again). |
| `bye` | `i replyPort` | Unsubscribe. |
| `state/request` | `i replyPort` | Resend the full state. |
| `params/describe` | `i replyPort` | Reply: one `param/info` per parameter, then `param/info/end`. |

Server → client:

| Address | Args |
| --- | --- |
| `hello/ack` | `i protocolVersion, s instanceId, s pluginVersion, s trackName` |
| `pong` | — |
| `bye` | — (server shutting down, or you are unknown) |

## State (server → client)

After `hello` and `state/request` the server sends everything. After that it sends only what changed, polled about 30 times a second. Each batch ends with `state/sync i version`, so redraw when that arrives.

| Address | Args |
| --- | --- |
| `state/global` | `i numSlices, i selectedSlice, i selectedSampleId, i rootNote, i sampleLoaded, i sampleMissing, i totalFrames, f sampleRate, i numSamples, i lazyChopActive, i followMidi, i snapZero` |
| `state/sample` | `i index, i sampleId, i startFrame, i numFrames, s fileName` (one per session sample, `index < numSamples`) |
| `state/slice` | `i index, i sampleId, i startFrame, i endFrame, i midiNote, i highNote, i rootNote, i colourARGB, i lockMaskLo, i lockMaskHi` (one per slice, `index < numSlices`) |
| `state/param` | `s scope, i id, i field, f value, i locked`. Scope `"sample"` has id = sampleId (the selected sample). Scope `"slice"` has id = slice index (the selected slice); the value is what the slice actually uses, its own if `locked`, else its sample's. |
| `state/status` | `s text, i isWarning` (empty text = no message) |
| `state/stems` | `s state, f progress, i sourceSampleId` |
| `state/audition` | `s state, f position0to1, s path` |
| `state/theme` | `s name, s keys, b colours`: the plugin's current theme. `keys` is `;`-separated (`surface0`–`surface5`, `text0`–`text2`, `waveform`, `accent`, `color1`–`color5`, `slice0`–`slice15`); `colours` holds one little-endian uint32 ARGB per key, in the same order. Slice colours are always taken from the `slice*` entries. Before the plugin window has opened once, this is the built-in default theme. |
| `state/sync` | `i version` |
| `playheads` | `b positions, i lazyChopPos`: little-endian int32 absolute frames of every sounding voice; `lazyChopPos` is `-1` when lazy chop is off |

All frame positions are **absolute** on the session timeline: every session sample laid end to end. A sample's frames are `[startFrame, startFrame + numFrames)`.

Lists that shrink are not cleared message by message. Use the counts in `state/global`.

## Waveform

| Address | Args |
| --- | --- |
| `view/peaks` → | `i replyPort, i requestId, i startFrame, i endFrame, i width` (width ≤ 4096) |
| ← `peaks` | `i requestId, i startFrame, i endFrame, b data`: `width` pairs of int8 `(min, max)`, scaled to ±127, mono (both channels combined) |

Request peaks again whenever your view or `totalFrames` changes.

## Parameters

`param/info` describes one parameter:

`i field, s name, s group, s unit, f min, f max, f step, s choices, i levels, i lockable`

- `field` is a stable id (append-only, never renumbered).
- `step` 0 = continuous.
- `choices` is a `;`-separated list for choice and on/off parameters.
- `levels` is a bit mask: 1 = sample, 2 = slice.
- `lockable` 1 = the slice can lock this value. Unlocked slices follow their sample.

**Values always use the units shown in the plugin** (ms, %, dB, Hz, semitones) for both levels.

| Address | Args | Meaning |
| --- | --- | --- |
| `sample/param` | `i sampleId, i field, f value` | Set a sample-level value (`-1` = selected sample) |
| `slice/param` | `i slice, i field, f value` | Set a slice value. It becomes locked unless it equals the sample value (`-1` = selected slice). |
| `slice/lock` | `i slice, i field` | Toggle the lock |
| `slice/setbpm` | `i slice, f bars` | SET BPM: the slice is `bars` long (0.0625 = 1/16 note) |
| `sample/setbpm` | `i sampleId, f bars` | SET BPM for a sample |
| `root/set` | `i note` | Root note for new slices |

Wrap a continuous encoder turn in `gesture/begin` … `gesture/end` to make it one undo step.

## Slices

| Address | Args | Meaning |
| --- | --- | --- |
| `slice/select` | `i slice` | Select (`-1` = none) |
| `slice/create` | `i startFrame, i endFrame` | New slice |
| `slice/bounds` | `i slice, i startFrame, i endFrame` | Move start/end. Previews live and commits as **one undo step** 250 ms after the last message (or on `gesture/end`). Snaps to zero crossings when ZX is on. |
| `slice/delete` | `i slice` | |
| `slice/duplicate` | `i slice` | |
| `slice/split` | `i slice, i count` | Split into `count` equal slices (2–128) |
| `midi/repack` | `i byPosition` | Resequence slice notes |
| `undo` / `redo` | — | |
| `gesture/begin` / `gesture/end` | — | Group edits into one undo step |
| `panic` | — | Stop all voices |
| `toggle/followmidi` | `i on` | FM |
| `toggle/snapzero` | `i on` | ZX |

## Chopping

| Address | Args | Meaning |
| --- | --- | --- |
| `chop/lazy` | `i on` | Start/stop lazy chop. While it runs, notes you play (over MIDI) place slice boundaries. |
| `chop/analyze` | `i slice, [f sensitivity 0–100, f minMs 20–500]` | Start auto-chop analysis of a slice. Reply: `chop/analyzing i slice`, then `chop/preview`. |
| `chop/settings` | `f sensitivity, f minMs` | Update the preview |
| `chop/apply` | — | Split at the previewed markers |
| `chop/cancel` | — | |
| ← `chop/preview` | `i slice, b positions` | Little-endian int32 absolute frames (`slice` = -1, empty blob = cleared) |

## Samples, presets and browsing

| Address | Args | Meaning |
| --- | --- | --- |
| `files/list` | `i replyPort, i requestId, s dir` | List a folder (empty `dir` = top-level places) |
| ← `files/entry` | `i requestId, i index, s kind, s name, s path` | `kind`: `dir`, `audio`, `preset` |
| ← `files/end` | `i requestId, i count, s dir, s parent` | |
| `sample/load` | `i append, s path…` | Load audio. `append` 1 adds to the session, 0 replaces it. Undoable. |
| `sample/select` | `i sampleId` | |
| `sample/delete` | `i sampleId` | |
| `sample/reorder` | `i sampleId, i newIndex` | |
| `sample/relink` | `s path` | Relink a missing sample |
| `audition/play` | `s path` | Preview a file |
| `audition/stop` | — | |
| `preset/load` | `s path` | |
| `preset/save` | `[s path]` | Without a path, saves to the preset library under the first sample's name |
| `stems/separate` | `i sampleId, [i stemMask, i separateFiles]` | Uses the first installed model. Install models from the plugin's STEMS panel. |
| `stems/cancel` | — | |

Settings (theme, scale, NRPN, stem folder and device, downloads) stay in the plugin window.

## Notes for client authors

- Ignore addresses and trailing arguments you don't know. New ones are compatible additions, and `protocolVersion` changes only on breaking changes.
- Prefer explicit slice indices and sample ids over `-1` ("the selected one"). The server resolves `-1` from its last published state, which trails its command queue by about one audio block. So `slice/select 3` followed at once by `chop/analyze -1` can still analyze the previous selection.
- Commands are applied at the start of the next audio block, so a host that has fully suspended the plugin applies them when audio resumes.
- A minimal Python client is in `tools/remote/osc_probe.py`.
