# INTERSECT

**Support development:** [Sponsor on GitHub](https://github.com/sponsors/tucktuckg00se) · [Buy me a coffee](https://buymeacoffee.com/tucktuckgoose)

INTERSECT is a sample slicer instrument plugin (VST3/AU/Standalone) with multi-sample sessions, per-slice locking, slice note ranges, multiple time/pitch algorithms, and MIDI-triggered slice playback.

![INTERSECT screenshot](.github/assets/screenshot.png)
*Theme shown: Open Color (`oc.intersectstyle`)*

## Table of Contents

- [Quick Start](#quick-start)
- [Installation](#installation)
- [Workflow Basics](#workflow-basics)
- [Interface Layout](#interface-layout)
- [Controls and Shortcuts Reference](#controls-and-shortcuts-reference)
- [MIDI Controller Routing (NRPN)](#midi-controller-routing-nrpn)
- [Theme Customization](#theme-customization)
- [Build from Source](#build-from-source)
- [Dependencies](#dependencies)
- [License](#license)
- [Support / Known Limitations](#support--known-limitations)

## Quick Start

[Watch the Quick Start Guide on YouTube](https://youtu.be/zsdtyIff2PQ)

## Installation

Download the latest release zip from [Releases](https://github.com/tucktuckg00se/INTERSECT/releases), then place plugin files in your system plugin folders.

### Try the experimental GPU stem separation pre-release

A v0.14.0 release candidate with downloadable GPU runtime bundles is available for testing: **[INTERSECT v0.14.0-rc.1](https://github.com/tucktuckg00se/INTERSECT/releases/tag/v0.14.0-rc.1)**.

This pre-release adds GPU stem separation via on-demand ONNX Runtime bundles. It has only been verified on macOS, Windows CPU, and Linux NVIDIA CUDA / CPU. If you try it on Windows DirectML, AMD MIGraphX, Intel macOS, or CoreML on Apple Silicon, please report your OS, GPU, runtime selection, and whether it worked on [issue #31](https://github.com/tucktuckg00se/INTERSECT/issues/31).

### Release package contents

| Platform | Minimum OS | Stem separation | Included binaries |
| --- | --- | --- | --- |
| Windows x64 | Windows 10 | On-demand ONNX Runtime bundle (CPU or DirectML GPU) | `INTERSECT.vst3`, `INTERSECT.exe` |
| Linux x64 | Ubuntu 22.04+ (glibc 2.35+) | On-demand ONNX Runtime bundle (CPU / NVIDIA CUDA / AMD MIGraphX) | `INTERSECT.vst3`, `INTERSECT` (standalone) |
| macOS arm64 | macOS 10.13 (High Sierra) | On-demand ONNX Runtime bundle (CoreML) | `INTERSECT.vst3`, `INTERSECT.component`, `INTERSECT.app` |
| macOS x64 | macOS 10.13 (High Sierra) | Not available | `INTERSECT.vst3`, `INTERSECT.component`, `INTERSECT.app` |

Plugin zips are small (single-digit MB); the ONNX Runtime required for stem separation is downloaded on demand from inside the plugin — see [Stem separation setup](#stem-separation-setup).

### Plugin install paths

| Format | Windows | macOS | Linux |
| --- | --- | --- | --- |
| VST3 | `C:\Program Files\Common Files\VST3\` | `~/Library/Audio/Plug-Ins/VST3/` | `~/.vst3/` |
| AU | n/a | `~/Library/Audio/Plug-Ins/Components/` | n/a |

After copying files, rescan plugins in your DAW.

### Stem separation setup

Stem separation requires two on-demand downloads performed from inside the plugin. Both live under **SET → Stem Separation** in the header bar.

1. **ONNX Runtime bundle** — open **SET → Stem Separation → ONNX Runtime** and pick the bundle that matches your platform and (optionally) your GPU. Only one bundle is active at a time; installing a different one switches which runtime the plugin loads. Restart INTERSECT after the install completes so the new runtime is picked up.
2. **Stem model** — open **SET → Stem Separation → Download Models** and pick a BS-RoFormer model.

After both are installed, open a sample's `STEMS` button to export.

### GPU runtime requirements (per ONNX Runtime bundle)

- **Windows — DirectML** — no extra installs required. Works with any GPU vendor (NVIDIA, AMD, Intel) on Windows 10+ with a recent DirectX 12-capable driver.
- **macOS arm64 — CoreML** — no extra installs required. Uses the built-in Apple Neural Engine / GPU.
- **macOS x64 (Intel Mac)** — stem separation is not available. ONNX Runtime 1.24 dropped x86_64 macOS support.
- **Linux — NVIDIA CUDA** — requires:
  - NVIDIA proprietary driver
  - CUDA runtime 12.x or 13.x (`libcudart.so.12` or `libcudart.so.13`) — pick the CUDA 12 or CUDA 13 bundle accordingly
  - cuDNN 9 (`libcudnn.so.9`)

  Install on Arch: `sudo pacman -S cuda cudnn`
  Install on Ubuntu: `sudo apt install nvidia-cuda-toolkit libcudnn9-cuda-12` (adjust the cuDNN package name to match your CUDA version)
- **Linux — AMD MIGraphX** — requires ROCm installed. See [AMD's ROCm install guide](https://rocm.docs.amd.com/projects/install-on-linux/en/latest/). Experimental.
- **Linux — CPU** — no extra installs required.

If a GPU runtime is missing or fails to load at export time, INTERSECT shows the error in the header status bar instead of silently failing; you can then switch `DEVICE` to CPU or install the missing runtime.

### macOS unsigned build note

If macOS reports that INTERSECT is damaged or blocked, clear quarantine flags:

```bash
xattr -cr ~/Library/Audio/Plug-Ins/VST3/INTERSECT.vst3
xattr -cr ~/Library/Audio/Plug-Ins/Components/INTERSECT.component
xattr -cr /Applications/INTERSECT.app
```

## Workflow Basics

1. **Multi-sample session:** INTERSECT can load and concatenate multiple audio files per instance (`.wav`, `.ogg`, `.aiff`, `.flac`, `.mp3`). `LOAD` replaces the session, `APPEND` adds more files to the current session.
2. **Current editor layout:** header bar, sample lane, slice lane, waveform, time/zoom bar, action bar, and bottom signal-chain editor.
3. **Slice creation:** draw slices manually, chop live with **LAZY**, or split a selected slice via **AUTO**.
4. **Inheritance model:** `GLOBAL` in the Signal Chain edits sample defaults. `SLICE` edits the selected slice and locks fields that diverge from the global value.
5. **Playback model:** MIDI triggers slices by note mapping. A slice belongs to one loaded sample, but playback and editing happen on the concatenated session timeline. A slice can respond to one note or a `LOW`-to-`HIGH` range, with `ROOT` defining the transposition center for that slice. Mute groups can choke voices in the same group.
6. **Algorithms:**
   - `Repitch`: pitch and speed are linked. `MODE` switches the playback interpolation between `Linear` and `Cubic`.
   - `Signalsmith`: independent time/pitch via Signalsmith Stretch (`TONAL`, `FMNT`, `FMNT C`).
   - `Bungee`: granular stretch mode with `GRAIN` choices (`Fast`, `Normal`, `Smooth`).
7. **Repitch + Stretch interaction:** when `ALGO=Repitch` and `STRETCH=ON`, `PITCH` and `TUNE` become BPM-driven read-only displays.
8. **SET BPM:** available in the Time/Pitch module for both `GLOBAL` and `SLICE`; it calculates BPM from musical duration.
9. **Filter model:** the filter is per-voice and resolves its settings at note-on. Cutoff changes affect newly triggered notes immediately, but do not retarget voices that are already playing.
10. **Filter envelope amount:** `AMT` is measured in semitones, so `+12 st` means the envelope can push cutoff up by one octave and `-12 st` means one octave down. This stays consistent across low and high base cutoff values.
11. **Key tracking:** `KEY` is a percentage of note tracking. `0%` ignores note pitch, `100%` makes cutoff follow pitch at full keyboard scaling, and intermediate values blend between them. In slice range mode, tracking follows the slice `ROOT` note.
12. **Drive:** `DRIVE` is pre-filter saturation. It adds harmonics before the filter rather than simply turning the signal up.
13. **Drive asymmetry:** `ASYM` biases the drive waveshaper to produce even-harmonic saturation, adding a warmer, tube-like character. A DC blocker engages automatically when asymmetry is above zero.
14. **Loop crossfade:** `FADE` smooths loop and ping-pong seams with equal-power crossfading. It is active only when `LOOP` is not `OFF`.
15. **Load behavior:** file decoding/loading is asynchronous (off the audio thread).
16. **Undo/redo:** snapshot-based history for slice and parameter edits.
17. **MIDI host stop handling:** responds to `All Notes Off (CC 123)` and `All Sound Off (CC 120)`.

## Interface Layout

### Header Bar

| Area | Function | Notes |
| --- | --- | --- |
| Status text | Shows warnings, errors, and missing-file notices | Click warning/error text to copy the message |
| `UNDO` / `REDO` | History navigation | Same as `Ctrl/Cmd + Z` and `Ctrl/Cmd + Shift + Z` |
| `PANIC` | Kills active voices immediately | Also stops lazy chop |
| `LOAD` | Open file browser | Replaces the current session |
| `APPEND` | Open file browser | Adds files to the current session |
| `SET` | Popup for theme, UI scale, and NRPN settings | Also shows current plugin version |

### Sample Lane, Slice Lane, and Waveform

| Area | Function | Notes |
| --- | --- | --- |
| Sample lane | Compact session-sample overview above the slice lane | Reflects selection and zoom; drag to reorder samples; includes per-sample `STEMS` / `CANCEL` and delete buttons |
| Slice lane | Compact slice-region overview above the waveform | Reflects selection and zoom |
| Waveform | Main editing surface | Drag-and-drop loading/appending, slice selection, boundary editing, move/duplicate, preview |
| Overlay hints | Contextual help and action prompts | Used by `ADD`, `AUTO`, and other actions |
| Playback cursors | Voice-position display | Shows active playheads |
| Transient preview markers | Auto Chop preview | Dashed markers shown before applying transient split |

## Stem Separation

1. Click a sample's `STEMS` button in the sample lane.
2. Choose the model, output folder, device, and which stems to export.
3. Start the export from the overlay panel.

Notes:
- Stem separation runs on the selected session sample and writes the exported stems to the chosen folder.
- While a stem export is running, that sample's `STEMS` button changes to `CANCEL`.
- `DEVICE` defaults to CPU. GPU can be selected when the build and local runtime support it.
- If INTERSECT cannot use the selected GPU path, it warns before export so you can switch devices instead of silently exporting on CPU.
- Stem separation is compute-heavy. On older PCs, especially on CPU exports, startup and processing can be noticeably slower.
- macOS x64 (Intel Mac) builds do not include stem separation due to a platform limitation in the underlying inference runtime. The stem panel on these builds shows a "Not available" state.

### Time / Zoom Bar

| Area | Function | Notes |
| --- | --- | --- |
| Time ruler | Shows time markings for the current view | Updates with zoom level |
| Drag horizontally | Scroll | Uses the current zoom level |
| Drag vertically | Zoom | Anchored to the drag start position |

### Action Bar

| Button | Function | Notes |
| --- | --- | --- |
| `ADD` | Toggle draw-slice mode | Drag on the waveform to create a slice |
| `LAZY` / `STOP` | Start/stop real-time lazy chopping | Label changes while active |
| `AUTO` | Open/close Auto Chop panel | Requires a selected slice |
| `COPY` | Duplicate selected slice | Equivalent to duplicate command |
| `DEL` | Delete selected slice or selected sample | Uses the last lane you interacted with |
| `ZX` | Snap edits to nearest zero crossing | Toggle |
| `FM` | Follow MIDI note selection | Auto-selects the played slice |
| `RESEQ` | Resequence slice MIDI notes | Opens overlay with `BY POSITION` and `AS CREATED` modes; requires 2+ slices |

### Signal Chain Bar

The bottom bar is the main parameter editor. It has four modules: `TIME/PITCH`, `FILTER`, `AMP`, and `PLAYBACK`.

**Collapsed mode** (default): `GLOBAL` and `SLICE` tabs switch between scopes, with one parameter strip visible at a time.

**Expanded mode**: shows both strips simultaneously — slice on top, global below — with no tabs. Click the chevron toggle on the right edge of the context bar to switch between modes.

**Context bar** (bottom edge):
- `SLICES` count and the global `ROOT` note are always visible on the right. The global `ROOT` is editable only when no slices exist.
- When a slice is selected: slice sample range, length, a `NOTE`/`RANGE` toggle, numeric note controls, read-only note names, and override count.

General behavior:
- Drag up/down on a value to edit it.
- Double-click a value to type it directly.
- In `SLICE` mode, editing a field locks that field for the selected slice when it differs from the global value.
- In `SLICE` mode, clicking a locked field label or right-clicking the field clears that override.

## Controls and Shortcuts Reference

### Header Bar

| Control | Function | Notes |
| --- | --- | --- |
| Status text | Copy warning/error message | Only active when a warning or error is being shown |
| `UNDO / REDO` | History navigation | Buttons in the header |
| `PANIC` | Kill active voices immediately | Also stops lazy chop |
| `LOAD` | Open file chooser | Replaces the current session |
| `APPEND` | Open file chooser | Adds files to the current session |
| `SET` | Theme, scale, and NRPN popup | Theme chooser, `+/- 0.25` scale, and NRPN settings |

### Signal Chain Bar

#### Context Bar

| Control | Function | Notes |
| --- | --- | --- |
| `SLICES` | Slice count | Always visible on the right side of the context bar |
| Global `ROOT` | Root note for new slices | Always visible on the right side of the context bar; editable only before any slices exist |

#### Time/Pitch Module

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

#### Slice Context Row

| Control | Function | Notes |
| --- | --- | --- |
| `NOTE / RANGE` | Switch between single-note and note-range triggering | Slice-only, shown in the context bar |
| `NOTE` | Single trigger note | Shown only in note mode; drag to edit |
| `LOW` | Lowest note in the trigger range | Shown only in range mode |
| `HIGH` | Highest note in the trigger range | Shown only in range mode |
| `ROOT` | Transposition and filter key-track reference note for the slice | Shown only in range mode |
| Note name text | Read-only pitch name display | Appears after the numeric field(s) and is not draggable |

#### Filter Module

| Control | Function | Notes |
| --- | --- | --- |
| `ON` | Enable/disable the filter | Per-voice filter toggle |
| `TYPE` | Filter mode | `LP`, `HP`, `BP`, `NT` |
| `SLOPE` | Filter steepness | `12 dB` or `24 dB` |
| `CUT` | Base cutoff frequency | Displayed in Hz |
| `RESO` | Resonance amount | Higher values emphasize the cutoff region |
| `DRIVE` | Pre-filter saturation | Adds harmonics before filtering |
| `ASYM` | Drive asymmetry | Biases waveshaper toward even harmonics for a warmer tone |
| `KEY` | Key tracking amount | `0-100%`, relative to the slice/root note mapping |
| `ATK / DEC / SUS / REL` | Filter envelope shape | Separate from the amp envelope |
| `AMT` | Filter envelope depth | Bipolar semitone offset (`st`) applied to cutoff |

Filter notes:
- Start with `ON`, `TYPE=LP`, modest `RESO`, and a lower `CUT` to hear the filter clearly.
- Raise `DRIVE` if you want a dirtier or more aggressive tone before the filter stage. Add `ASYM` to bias the saturation toward even harmonics for a warmer, tube-like character.
- Use `KEY` when you want higher MIDI notes to sound brighter and lower notes darker. In slice range mode, the filter tracks from that slice's `ROOT` note.
- Use positive `AMT` for a classic opening filter envelope and negative `AMT` for an inverted sweep.
- `AMT` is in semitones because it controls octave-style movement of cutoff. `+12 st` doubles the cutoff, `-12 st` halves it.
- Filter settings resolve at note-on, so changing cutoff while a note is already playing affects the next note rather than re-tuning the current voice.

#### Amp Module

| Control | Function | Notes |
| --- | --- | --- |
| `ATK / DEC / SUS / REL` | Amp envelope | Standard ADSR for voice level |
| `TAIL` | Release-tail toggle | Allows playback to continue past slice boundary during release |
| `GAIN` | Output gain | `-100` to `+24 dB` |

#### Playback Module

| Control | Function | Notes |
| --- | --- | --- |
| `REV` | Reverse playback | Toggle |
| `LOOP` | Loop mode | `OFF`, `LOOP`, `PP` |
| `FADE` | Loop crossfade amount | Active when `LOOP` is `LOOP` or `PP` |
| `MUTE` | Mute group | Voices in the same group choke each other |
| `1SHOT` | One-shot playback | Ignores note-off until the slice ends |
| `OUT` | Output bus | `SLICE` mode only, `1` to `16` |
| `VOICES` | Max playable voices | `GLOBAL` mode only, `1` to `31` |

### Action Bar

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

### Auto Chop Panel

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

### Waveform and Mouse Gestures

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

### Keyboard Shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl/Cmd + Z` | Undo |
| `Ctrl/Cmd + Shift + Z` | Redo |
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

Single-letter action shortcuts are intentionally unbound so DAW keyboard-MIDI note entry remains available.

## MIDI Controller Routing (NRPN)

INTERSECT supports NRPN-based slice editing from a hardware or software MIDI controller. Enable it via the **SET** button in the header bar — the popup contains an NRPN section with channel and consume options.

**Controller requirements:** The controller must have **endless rotary encoders** (not fixed-range knobs) and must support NRPN mode with configurable MSB/LSB address values. Crucially, it must send **relative** data bytes — CC 96 (Data Increment) and CC 97 (Data Decrement) — one step per click. Controllers that only send absolute values (CC 6 Data Entry) will not work, nor will standard fixed-range knobs. Examples include the Akai MPD32 and MPD218. Results may vary.

Select the slice to edit by enabling **FM** (Follow MIDI) and playing its MIDI note. Then use the start/end knobs to adjust the slice boundaries. Commit happens automatically ~300ms after the knob stops moving — the same feel as releasing a parameter slider. Zoom in for finer control; each knob step moves `viewWidth / 16383` samples.

### NRPN Parameter Table

NRPN numbers use CC 99 (MSB address) / CC 98 (LSB address) to select the parameter, then CC 96 (Data Increment) or CC 97 (Data Decrement) to send a ±1 step. No absolute-value data bytes (CC 6/38) are used.

When programming a hardware controller, set the knob to **NRPN mode** with **MSB 64** and the LSB from the table below.

| NRPN | MSB | LSB | Name | Direction | Notes |
| --- | --- | --- | --- | --- | --- |
| 8193 | 64 | 1 | Zoom | CC 96 / CC 97 | Zoom in / out |
| 8194 | 64 | 2 | Slice start | CC 96 / CC 97 | Each step = viewWidth / 16383 samples |
| 8195 | 64 | 3 | Slice end | CC 96 / CC 97 | Each step = viewWidth / 16383 samples |

### SET Button — NRPN Settings

Open with the **SET** button in the header bar.

| Control | Function |
| --- | --- |
| `NRPN` | Enable/disable NRPN slice editing |
| `CONSUME CCs` | Strip NRPN edit CCs from MIDI output so they don't reach downstream instruments |
| `CH −` / `CH +` | MIDI channel filter (0 = omni) |

Settings are saved to the INTERSECT settings file alongside theme and UI scale.

### DAW-specific notes

**Ableton Live:** NRPN control does not work in Ableton Live. Ableton intercepts the CC 96/97 data-increment bytes before they reach the plugin, so the plugin only ever sees the address bytes (CC 98/99) and can never fire an event. This is an Ableton routing limitation with no known workaround on the plugin side.

**REAPER / Bitwig:** Route a MIDI track to the plugin instance as normal. No additional configuration is needed; all CC messages are forwarded to the plugin.

## Theme Customization

INTERSECT supports custom `.intersectstyle` themes. On first launch it creates default `dark.intersectstyle` and `light.intersectstyle` in the user theme directory.

| OS | Theme folder |
| --- | --- |
| Windows | `%APPDATA%\Roaming\INTERSECT\themes\` |
| macOS | `~/Library/Application Support/INTERSECT/themes/` |
| Linux | `~/.config/INTERSECT/themes/` |

Create a custom theme:

1. Copy one of the starter files from [`themes/`](themes/) and rename it, for example `mytheme.intersectstyle`.
2. Set a unique `name:` value (used in the UI theme list).
3. Edit colors as 6-digit hex `RRGGBB`.
4. Place the file in your user theme folder.
5. Restart the plugin, then use the **SET** button in the header to select the theme.

The **SET** button popup also controls interface scale (`0.5x` to `3.0x` in `0.25` steps).

## Build from Source

### Prerequisites

- CMake `3.22+`
- C++20 compiler
- Git

### Platform setup

Pick your platform below to install the required toolchain and libraries, then follow the shared **Clone and build** steps.

<details>
<summary><strong>Windows</strong></summary>

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/vs/community/) (Community edition is free).
   During installation, select the **"Desktop development with C++"** workload.
2. Install [CMake](https://cmake.org/download/) (add to PATH during install) and [Git](https://git-scm.com/download/win).

</details>

<details>
<summary><strong>macOS</strong></summary>

1. Install Xcode Command Line Tools:
   ```bash
   xcode-select --install
   ```
2. Install CMake via [Homebrew](https://brew.sh/):
   ```bash
   brew install cmake
   ```

</details>

<details>
<summary><strong>Linux — Debian / Ubuntu</strong></summary>

```bash
sudo apt update
sudo apt install -y build-essential cmake git libasound2-dev libfreetype-dev \
  libx11-dev libxrandr-dev libxcursor-dev libxinerama-dev \
  libwebkit2gtk-4.1-dev libcurl4-openssl-dev
```

</details>

<details>
<summary><strong>Linux — Fedora</strong></summary>

```bash
sudo dnf install -y gcc-c++ cmake git alsa-lib-devel freetype-devel \
  libX11-devel libXrandr-devel libXcursor-devel libXinerama-devel \
  webkit2gtk4.1-devel libcurl-devel
```

</details>

<details>
<summary><strong>Linux — Arch</strong></summary>

```bash
sudo pacman -S --needed base-devel cmake git alsa-lib freetype2 \
  libx11 libxrandr libxcursor libxinerama webkit2gtk-4.1 curl
```

</details>

### Clone and build

```bash
git clone --recursive https://github.com/tucktuckg00se/INTERSECT.git
cd INTERSECT
cmake -B build
cmake --build build --config Release
```

### ONNX Runtime

The build does not bundle any ONNX Runtime shared library. Only the ORT C++ headers are pulled in at configure time (via CMake `FetchContent`) so the plugin compiles against the published ORT API. The shared library itself is downloaded at runtime via **SET → Stem Separation → ONNX Runtime** — see [Stem separation setup](#stem-separation-setup). This keeps each plugin zip small and lets users pick their GPU (CPU / CUDA / MIGraphX / DirectML / CoreML) without needing a separate build.

### Build outputs

- VST3: `build/Intersect_artefacts/Release/VST3/INTERSECT.vst3`
- Standalone:
  - Windows: `build/Intersect_artefacts/Release/Standalone/INTERSECT.exe`
  - Linux: `build/Intersect_artefacts/Release/Standalone/INTERSECT`
  - macOS: `build/Intersect_artefacts/Release/Standalone/INTERSECT.app`
- AU (macOS): `build/Intersect_artefacts/Release/AU/INTERSECT.component`

### Release workflow (repo maintainers)

Pushing a tag matching `v*` triggers the GitHub Actions release workflow, which builds and packages four small plugin zips:

- Windows x64
- Linux x64
- macOS arm64
- macOS x64 (no stem separation — ONNX Runtime 1.24 dropped x86_64 macOS)

ONNX Runtime bundles are published separately from the [intersect-ort-providers](https://github.com/tucktuckg00se/intersect-ort-providers) repo and downloaded on demand from the plugin.

## Dependencies

- [JUCE](https://github.com/juce-framework/JUCE) (git submodule)
- [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) (MIT)
- [Signalsmith Linear](https://github.com/Signalsmith-Audio/linear) (dependency of Signalsmith Stretch)
- [Bungee](https://github.com/bungee-audio-stretch/bungee) (MPL-2.0)

## License

INTERSECT is licensed under the [GNU General Public License v3.0](LICENSE).

## Support / Known Limitations

- INTERSECT project recall stores sample file paths for every file in the session; if files move, relink is required.
- Builds are unsigned; platform security prompts (especially macOS) may require manual trust/quarantine removal.
- Report bugs or request features via GitHub Issues on this repository.
