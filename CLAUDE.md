# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

INTERSECT is a JUCE-based audio sampler plugin (VST3/AU/Standalone). It loads one sample at a time, slices it into regions, and triggers slices via MIDI with per-slice parameter control. Uses Signalsmith Stretch for independent pitch and time-stretch.

## Build

```bash
# Requires JUCE and signalsmith-stretch as git submodules
git submodule update --init --recursive
cmake -B build -S project
cmake --build build --config Release
```

**Important — build commands must run from the repo root** (`C:\Users\m_t_w\Documents\claude-projects\tuckers-sampler`). The `build/` directory lives at the repo root, not inside `project/`. Use `cd` to the repo root first, then run cmake:

```bash
cd "C:\Users\m_t_w\Documents\claude-projects\tuckers-sampler" && "C:\Program Files\CMake\bin\cmake.exe" --build build --config Release
```
cppcheck location: C:\Program Files\Cppcheck\cppcheck.exe

On Windows, cmake is not on PATH — always use the full path: `"C:\Program Files\CMake\bin\cmake.exe"`

Targets: VST3(Windows/Linux/Linux), AU (macOS), Standalone. Base window: 750x550 (scalable via hi-DPI controls).

## Architecture

### Core Concepts

- **One sample loaded at a time** — WAV/OGG/AIFF/FLAC loaded via drag-and-drop
- **Sample-level parameters** — BPM, pitch, algorithm (Repitch/Stretch), ADSR envelope, mute group, ping-pong, stretch toggle; these serve as defaults for all slices
- **Slices** — subdivisions of the sample with start/end points, each assigned a MIDI note (starting at C2/36). No slices are created automatically on load; user creates them manually
- **Slice-level parameters** — same set as sample-level plus MIDI note; each can be independently "locked" (overridden) or left to inherit from the sample defaults
- **Inheritance** — `SliceManager::resolveParam()` checks `lockMask` bit to choose slice override vs global default
- **Stretch workflow** — SET BPM button (in HeaderBar or SliceControlBar) lets user pick a time unit, calculates BPM from selected slice length, auto-locks the parameter
- **Lazy chop** — sample plays continuously from the start; pressing any MIDI key places a slice boundary at the current playhead position. Stopping lazy chop closes the last slice at the end of the sample
- **MIDI-selects-slice** — optional toggle ("M" button in SliceLane) that auto-selects a slice when its MIDI note is played
- **Mute groups** — all slices default to mute group 1; voices in the same group cut each other off

### File Structure

```
project/
├── CMakeLists.txt
├── src/
│   ├── PluginProcessor.h/.cpp    — IntersectProcessor: APVTS, command FIFO, MIDI routing, audio rendering, serialization (v4)
│   ├── PluginEditor.h/.cpp       — Editor: 4-band layout, 30Hz timer, hi-DPI scaling via AffineTransform
│   ├── audio/
│   │   ├── SampleData.h/.cpp     — Stereo audio buffer, file loading with resampling, linear interpolation
│   │   ├── Slice.h               — Slice struct with LockBit enum for per-field lock mask
│   │   ├── SliceManager.h/.cpp   — 128-slot slice array, MIDI map, colour palette, inheritance resolution
│   │   ├── Voice.h               — Voice struct: ADSR fields, Signalsmith stretch instance + ring buffers
│   │   ├── VoicePool.h/.cpp      — 16-voice pool, allocation/stealing, per-sample rendering, Signalsmith block processing
│   │   ├── AdsrEnvelope.h        — Linear ADSR envelope (header-only)
│   │   ├── WsolaEngine.h/.cpp    — BPM calculation utility (calcStretchBpm); legacy WSOLA grain engine
│   │   └── LazyChopEngine.h/.cpp — Continuous preview playback + real-time slice boundary placement
│   ├── params/
│   │   ├── ParamIds.h            — APVTS parameter ID strings (includes uiScale)
│   │   └── ParamLayout.h/.cpp    — APVTS parameter layout with ranges
│   └── ui/
│       ├── IntersectLookAndFeel.h/.cpp — Theme colour palette + button styling
│       ├── HeaderBar.h/.cpp          — Band 1: 2-row teal header (Row1: BPM/PITCH/ALGO/SET BPM + scale buttons; Row2: ATK/DEC/SUS/REL)
│       ├── SliceControlBar.h/.cpp    — Band 2: selected slice params with lock icons + SET BPM
│       ├── WaveformView.h/.cpp       — Band 3: waveform display, slice overlays, playback cursors, drag-and-drop
│       ├── WaveformCache.h/.cpp      — Min/max peak cache per pixel
│       ├── ScrollZoomBar.h/.cpp      — Scroll (horizontal drag) + zoom (vertical drag) thumb bar with buttons
│       ├── SliceLane.h/.cpp          — Slice region bars (zoom-synced) + MIDI-selects-slice toggle button
│       ├── SliceListPanel.h/.cpp     — Band 4 left: vertical slice buttons
│       └── ActionPanel.h/.cpp        — Band 4 right: +SLICE, DELETE, LAZY CHOP
```

### Key Design Patterns

- **Command FIFO** — UI pushes `Command` structs via `AbstractFifo`; processor drains them at block start. Thread-safe UI->audio communication. Commands defined in `PluginProcessor.h`.
- **Lock mask inheritance** — each slice has a `uint32_t lockMask`; `SliceManager::resolveParam()` checks the relevant `LockBit` to choose slice override vs global APVTS default
- **Voice stealing** — prefers releasing voices with lowest envelope level
- **Atomic voice positions** — `std::atomic<float> voicePositions[16]` for lock-free UI cursor display
- **Signalsmith Stretch** — per-voice block-based processing (128 samples). Each voice gets its own `SignalsmithStretch<float>` instance initialized at note-on. Time ratio = `dawBpm / sliceBpm`.
- **Hi-DPI scaling** — `uiScale` APVTS param (0.5-3.0), applied via `AffineTransform::scale()` in the editor

### Serialization

State version is currently **v4**. Backward-compatible with v2/v3 (reads and discards legacy `sampleStart`/`sampleEnd` fields). Saves: APVTS state, zoom/scroll, selected slice, midiSelectsSlice, all slice data (including colour as ARGB), and full audio PCM (stereo interleaved floats).

### Dependencies

- **JUCE** — git submodule in `JUCE/`
- **Signalsmith Stretch** — git submodule in `signalsmith-stretch/`. Include paths set in CMakeLists.txt.
- **Signalsmith Linear** — git submodule in `signalsmith-linear/` (dependency of Signalsmith Stretch; the repo root is on the include path so `#include "signalsmith-linear/stft.h"` resolves correctly)

## Design Assets

The `info/` directory (local only, not tracked in git) contains the original architecture flowchart used as reference for the UI layout.
